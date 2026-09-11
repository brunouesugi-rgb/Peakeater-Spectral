#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pe::processor {

class LookaheadGainScheduler {
public:
    using Frame = std::array<float, 2>;

    void prepare(double rate) {
        sampleRate = std::clamp(rate, 8000.0, 768000.0);
        delay = static_cast<int>(std::lround(sampleRate * 0.005));
        window = delay - firDelay + 1;
        audio.assign(std::bit_ceil(static_cast<size_t>(delay + 2)), {});
        audioMask = audio.size() - 1;
        gains.assign(static_cast<size_t>(window), 1.0f);
        queue.assign(static_cast<size_t>(window + 1), {});
        lowCoefficient = static_cast<float>(std::exp(-6.283185307179586 * 180.0 / sampleRate));
        energyCoefficient = static_cast<float>(std::exp(-1.0 / (0.04 * sampleRate)));
        for (size_t phase = 0; phase < taps.size(); ++phase) {
            double sum = 0.0;
            for (int j = 0; j < tapCount; ++j) {
                auto const x = static_cast<double>(j - firDelay) + (phase + 1) * 0.25;
                auto const sinc = std::sin(3.141592653589793 * x) / (3.141592653589793 * x);
                auto const hann = 0.5 + 0.5 * std::cos(3.141592653589793 * x / (firDelay + 1));
                taps[phase][j] = static_cast<float>(sinc * hann);
                sum += taps[phase][j];
            }
            for (auto& tap : taps[phase]) tap = static_cast<float>(tap / sum);
        }
        reset();
    }

    void reset() noexcept {
        std::fill(audio.begin(), audio.end(), Frame{});
        std::fill(gains.begin(), gains.end(), 1.0f);
        firHistory.fill({});
        firWrite = 0;
        write = gainWrite = head = count = 0;
        clock = 0;
        gainSum = static_cast<double>(gains.size());
        releaseGain = 1.0f;
        cycles = {};
        confidence = 0.0f;
        effectiveRelease = releaseMs;
        releaseCoefficient = static_cast<float>(std::exp(-1.0 / (releaseMs * 0.001 * sampleRate)));
    }

    void configure(float ceiling, float release, bool truePeak, float adaptive) noexcept {
        ceilingGain = std::clamp(std::isfinite(ceiling) ? ceiling : 1.0f, 1e-6f, 1.0f);
        releaseMs = std::clamp(std::isfinite(release) ? release : 80.0f, 2.0f, 2000.0f);
        useTruePeak = truePeak;
        cycleAmount = std::clamp(std::isfinite(adaptive) ? adaptive : 0.0f, 0.0f, 1.0f);
    }

    [[nodiscard]] int latencySamples() const noexcept { return delay; }
    [[nodiscard]] float cycleConfidence() const noexcept { return confidence; }
    [[nodiscard]] float effectiveReleaseMs() const noexcept { return effectiveRelease; }

    Frame process(Frame input, bool limit = true) noexcept {
        if (audio.empty()) return input;
        for (auto& value : input) if (!std::isfinite(value)) value = 0.0f;
        audio[write] = input;
        firHistory[firWrite] = firHistory[firWrite + tapCount] = input;
        updateCycles(input);
        auto const centre = history(firDelay);
        auto peak = std::max(std::abs(centre[0]), std::abs(centre[1]));
        if (useTruePeak) {
            std::array<Frame, 3> reconstructed{};
            auto const* samples = firHistory.data() + firWrite;
            for (int j = 0; j < tapCount; ++j) {
                auto const frame = samples[j];
                for (size_t phase = 0; phase < taps.size(); ++phase) {
                    reconstructed[phase][0] += taps[phase][j] * frame[0];
                    reconstructed[phase][1] += taps[phase][j] * frame[1];
                }
            }
            for (auto const& frame : reconstructed)
                peak = std::max({peak, std::abs(frame[0]), std::abs(frame[1])});
        }
        auto const safeCeiling = ceilingGain * (useTruePeak ? 0.99426007f : 1.0f);
        auto const target = peak > safeCeiling ? safeCeiling / peak : 1.0f;
        while (count && queue[head].time + static_cast<uint64_t>(window) <= clock) {
            head = queueIndex(head + 1);
            --count;
        }
        while (count && queue[queueIndex(head + count - 1)].gain >= target) --count;
        queue[queueIndex(head + count)] = {clock, target};
        ++count;
        auto const minimum = queue[head].gain;
        releaseGain = minimum < releaseGain ? minimum
            : releaseCoefficient * releaseGain + (1.0f - releaseCoefficient) * minimum;
        // A W-sample minimum followed by a W-sample average never exceeds the
        // target at n-W+1. The detector delay completes the fixed 5 ms latency.
        gainSum += static_cast<double>(releaseGain) - gains[gainWrite];
        gains[gainWrite] = releaseGain;
        if (++gainWrite == gains.size()) gainWrite = 0;
        auto const gain = static_cast<float>(std::clamp(gainSum / window, 0.0, 1.0));
        auto output = history(delay);
        output[0] *= limit ? gain : 1.0f;
        output[1] *= limit ? gain : 1.0f;
        write = (write + 1) & audioMask;
        firWrite = firWrite == 0 ? tapCount - 1 : firWrite - 1;
        ++clock;
        return output;
    }

private:
    static constexpr int firDelay = 16;
    static constexpr int tapCount = 33;
    struct Entry { uint64_t time = 0; float gain = 1.0f; };
    struct Cycle {
        float low = 0.0f, lowEnergy = 0.0f, energy = 0.0f;
        float period = 0.0f, confidence = 0.0f;
        uint64_t lastCrossing = 0;
    };
    Frame history(int age) const noexcept {
        return audio[(write - static_cast<size_t>(age)) & audioMask];
    }
    size_t queueIndex(size_t index) const noexcept { return index < queue.size() ? index : index - queue.size(); }
    void updateCycles(Frame const& input) noexcept {
        for (size_t channel = 0; channel < cycles.size(); ++channel) {
            auto& c = cycles[channel];
            auto const previous = c.low;
            c.low = lowCoefficient * c.low + (1.0f - lowCoefficient) * input[channel];
            c.lowEnergy = energyCoefficient * c.lowEnergy + (1.0f - energyCoefficient) * c.low * c.low;
            c.energy = energyCoefficient * c.energy + (1.0f - energyCoefficient) * input[channel] * input[channel];
            if (previous <= 0.0f && c.low > 0.0f) {
                auto const interval = static_cast<float>(clock - c.lastCrossing);
                if (interval >= sampleRate / 250.0 && interval <= sampleRate / 20.0 && c.lowEnergy > 1e-8f) {
                    auto const stable = c.period > 0.0f && std::abs(interval - c.period) < c.period * 0.15f;
                    c.confidence = stable ? std::min(1.0f, c.confidence + 0.2f) : c.confidence * 0.5f;
                    c.period = stable ? c.period + 0.2f * (interval - c.period) : interval;
                } else c.confidence *= 0.5f;
                c.lastCrossing = clock;
            }
            if (clock - c.lastCrossing > sampleRate / 20.0) c.confidence *= 0.99f;
        }
        if ((clock & 15u) == 0) {
            auto const& c = cycles[cycles[1].lowEnergy > cycles[0].lowEnergy ? 1 : 0];
            auto const lowShare = std::clamp(c.lowEnergy / std::max(c.energy, 1e-12f), 0.0f, 1.0f);
            confidence = c.confidence * std::clamp((lowShare - 0.2f) / 0.5f, 0.0f, 1.0f);
            auto const floor = std::min(releaseMs * 4.0f, static_cast<float>(c.period * 2000.0 / sampleRate));
            auto const desired = releaseMs + std::max(0.0f, floor - releaseMs) * confidence * cycleAmount;
            effectiveRelease += 0.05f * (desired - effectiveRelease);
            releaseCoefficient = static_cast<float>(std::exp(-1.0 / (effectiveRelease * 0.001 * sampleRate)));
        }
    }
    double sampleRate = 48000.0, gainSum = 0.0;
    int delay = 240, window = 225;
    size_t write = 0, gainWrite = 0, head = 0, count = 0;
    size_t audioMask = 0, firWrite = 0;
    uint64_t clock = 0;
    float ceilingGain = 1.0f, releaseMs = 80.0f, cycleAmount = 1.0f;
    float lowCoefficient = 0.0f, energyCoefficient = 0.0f, releaseCoefficient = 0.0f;
    float releaseGain = 1.0f, confidence = 0.0f, effectiveRelease = 80.0f;
    bool useTruePeak = true;
    std::array<std::array<float, tapCount>, 3> taps{};
    std::array<Frame, tapCount * 2> firHistory{};
    std::array<Cycle, 2> cycles{};
    std::vector<Frame> audio;
    std::vector<float> gains;
    std::vector<Entry> queue;
};

class ParallelAlignmentDelay {
public:
    void prepare(int samples) { frames.assign(static_cast<size_t>(samples + 1), {}); write = 0; }
    void reset() noexcept { std::fill(frames.begin(), frames.end(), LookaheadGainScheduler::Frame{}); write = 0; }
    LookaheadGainScheduler::Frame process(LookaheadGainScheduler::Frame input, bool delayed) noexcept {
        if (frames.empty()) return input;
        frames[write] = input;
        auto const next = write + 1 == frames.size() ? 0 : write + 1;
        auto const output = delayed ? frames[next] : input;
        write = next;
        return output;
    }
private:
    std::vector<LookaheadGainScheduler::Frame> frames;
    size_t write = 0;
};
}

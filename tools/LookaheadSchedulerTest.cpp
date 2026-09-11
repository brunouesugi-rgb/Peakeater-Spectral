#include "processor/LookaheadGainScheduler.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

void require(bool condition, char const* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    using pe::processor::LookaheadGainScheduler;
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        LookaheadGainScheduler dsp;
        dsp.prepare(rate);
        dsp.configure(0.8f, 20.0f, true, 1.0f);
        auto const delay = dsp.latencySamples();
        require(delay == static_cast<int>(std::lround(rate * 0.005)), "latency");
        for (int i = 0; i < delay * 3; ++i) {
            auto frame = dsp.process({i == 0 ? 0.1f : 0.0f, 0.0f});
            require(std::abs(frame[0] - (i == delay ? 0.1f : 0.0f)) < 1e-6f, "impulse/tail");
        }
        dsp.reset();
        for (int i = 0; i < static_cast<int>(rate); ++i) {
            auto const x = static_cast<float>(1.5 * std::sin(6.283185307179586 * 50.0 * i / rate));
            auto frame = dsp.process({x, -x});
            require(std::isfinite(frame[0]) && std::abs(frame[0]) <= 0.80001f, "ceiling");
            require(std::abs(frame[0] + frame[1]) < 1e-6f, "stereo link");
        }
        require(dsp.cycleConfidence() > 0.5f, "cycle confidence");
        require(dsp.effectiveReleaseMs() > 20.0f, "cycle release floor");
        dsp.reset();
        require(dsp.cycleConfidence() == 0.0f, "reset");
        pe::processor::ParallelAlignmentDelay dry;
        dry.prepare(delay);
        for (int i = 0; i < delay * 3; ++i) {
            auto const input = i == 0 ? 0.125f : 0.0f;
            auto const wet = dsp.process({input, input}, false);
            auto const aligned = dry.process({input, input}, true);
            require(wet == aligned, "dry/wet alignment");
        }
        for (double frequency : {50.0, 997.0, rate * 0.25, rate * 0.45}) {
            dsp.reset();
            std::vector<float> output(8192);
            for (int i = 0; i < static_cast<int>(output.size()); ++i) {
                auto const x = static_cast<float>(1.5 * std::sin(6.283185307179586 * frequency * i / rate + 0.37));
                output[i] = dsp.process({x, x})[0];
            }
            double peak = 0.0;
            for (int i = 2048; i < 4096; ++i) {
                for (int phase = 0; phase < 16; ++phase) {
                    double value = 0.0, sum = 0.0;
                    for (int j = -64; j <= 64; ++j) {
                        auto const x = j - phase / 16.0;
                        auto const tap = (std::abs(x) < 1e-12 ? 1.0 : std::sin(3.141592653589793 * x) / (3.141592653589793 * x))
                            * (0.5 + 0.5 * std::cos(3.141592653589793 * x / 65.0));
                        value += output[i + j] * tap;
                        sum += tap;
                    }
                    peak = std::max(peak, std::abs(value / sum));
                }
            }
            std::cout << "frequency=" << frequency << " reconstructed_peak=" << peak << '\n';
            require(peak <= 0.802f, "independent reconstructed peak");
        }
        std::cout << "PASS rate=" << rate << " latency=" << delay << '\n';
    }
}

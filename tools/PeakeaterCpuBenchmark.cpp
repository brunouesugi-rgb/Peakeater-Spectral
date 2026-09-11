#include "processor/SpectralMaximizer.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace {
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 128;
constexpr int channels = 2;
constexpr int warmupBlocks = 256;
constexpr int measuredBlocks = 5000;
constexpr int validationBlocks = 512;

void fillInput(juce::AudioBuffer<float>& buffer, int64_t startSample) {
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
        auto* samples = buffer.getWritePointer(channel);
        auto const phaseOffset = channel == 0 ? 0.0 : 0.31;
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            auto const time = static_cast<double>(startSample + sample) / sampleRate;
            auto const kickPhase = std::fmod(time, 0.5);
            auto const kick = std::exp(-kickPhase * 22.0) * std::sin(2.0 * juce::MathConstants<double>::pi * 54.0 * time);
            auto const body = 0.24 * std::sin(2.0 * juce::MathConstants<double>::pi * 137.0 * time + phaseOffset);
            auto const mid = 0.20 * std::sin(2.0 * juce::MathConstants<double>::pi * 997.0 * time + phaseOffset * 0.4);
            auto const high = 0.10 * std::sin(2.0 * juce::MathConstants<double>::pi * 7311.0 * time + phaseOffset * 1.7);
            samples[sample] = static_cast<float>((kick * 0.72) + body + mid + high);
        }
    }
}

void configure(pe::processor::SpectralMaximizer& maximizer, size_t bandMultiplier, std::string_view profile = {}) {
    constexpr std::string_view maskingOffSuffix = "_masking_off";
    constexpr std::string_view allocatorOffSuffix = "_allocator_off";
    auto const perceptualAllocatorEnabled = !profile.ends_with(allocatorOffSuffix);
    if (!perceptualAllocatorEnabled) {
        profile.remove_suffix(allocatorOffSuffix.size());
    }
    auto const maskingResidualEnabled = !profile.ends_with(maskingOffSuffix);
    if (!maskingResidualEnabled) {
        profile.remove_suffix(maskingOffSuffix.size());
    }
    maximizer.setMaskingResidualEnabled(maskingResidualEnabled);
    maximizer.setPerceptualAllocatorEnabled(perceptualAllocatorEnabled);
    maximizer.setMode(pe::processor::SpectralMaximizer::Mode::EDM);
    maximizer.setThreshold(-12.0f);
    maximizer.setCeiling(-1.0f);
    maximizer.setAttack(1.0f);
    maximizer.setHold(0.0f);
    maximizer.setRelease(80.0f);
    maximizer.setTransientRecovery(0.35f);
    maximizer.setLookahead(2.0f);
    maximizer.setBandMultiplier(bandMultiplier);
    maximizer.setDetectorHp(45.0f);
    maximizer.setAdaptiveRelease(0.65f);
    maximizer.setStereoLink(1.0f);
    maximizer.setLowProtect(0.5f);
    maximizer.setGrLimit(12.0f);
    maximizer.setPunchProtect(0.25f);
    maximizer.setReleaseShape(0.5f);
    maximizer.setHfGuard(0.2f);
    maximizer.setTruePeakLimit(false);

    if (profile == "saturation_min") {
        maximizer.setSaturationStyle(pe::processor::SpectralMaximizer::SaturationStyle::Tube);
        maximizer.setSaturation(0.0f);
        maximizer.setSaturationDensity(0.75f);
    } else if (profile == "saturation_max") {
        maximizer.setSaturationStyle(pe::processor::SpectralMaximizer::SaturationStyle::Tube);
        maximizer.setSaturation(1.0f);
        maximizer.setSaturationDensity(0.75f);
    } else if (profile == "density_min" || profile == "density_max") {
        maximizer.setSaturationStyle(pe::processor::SpectralMaximizer::SaturationStyle::Tube);
        maximizer.setSaturation(0.85f);
        maximizer.setSaturationDensity(profile == "density_min" ? 0.0f : 1.0f);
    } else if (profile == "sat_tone_min" || profile == "sat_tone_max") {
        maximizer.setSaturationStyle(pe::processor::SpectralMaximizer::SaturationStyle::Tube);
        maximizer.setSaturation(0.85f);
        maximizer.setSaturationDensity(0.8f);
        maximizer.setSaturationTone(profile == "sat_tone_min" ? -1.0f : 1.0f);
    } else if (profile == "bass_safe_min" || profile == "bass_safe_max") {
        maximizer.setSaturationStyle(pe::processor::SpectralMaximizer::SaturationStyle::Tube);
        maximizer.setSaturation(1.0f);
        maximizer.setSaturationDensity(0.9f);
        maximizer.setBassSafe(profile == "bass_safe_min" ? 0.0f : 1.0f);
    } else if (profile == "detector_hp_min" || profile == "detector_hp_max") {
        maximizer.setDetectorHp(profile == "detector_hp_min" ? 0.0f : 250.0f);
    } else if (profile == "attack_min" || profile == "attack_max") {
        maximizer.setAttack(profile == "attack_min" ? 0.01f : 1000.0f);
    } else if (profile == "hold_min" || profile == "hold_max") {
        maximizer.setHold(profile == "hold_min" ? 0.0f : 500.0f);
    } else if (profile == "release_min" || profile == "release_max") {
        maximizer.setRelease(profile == "release_min" ? 1.0f : 2000.0f);
    } else if (profile == "transient_min" || profile == "transient_max") {
        maximizer.setTransientRecovery(profile == "transient_min" ? 0.0f : 1.0f);
    } else if (profile == "mode_one_shot") {
        maximizer.setMode(pe::processor::SpectralMaximizer::Mode::OneShot);
    } else if (profile == "mode_one_shot_clean") {
        maximizer.setMode(pe::processor::SpectralMaximizer::Mode::OneShotClean);
    } else if (profile == "drive_clip_edm") {
        maximizer.setMode(pe::processor::SpectralMaximizer::Mode::EDM);
        maximizer.setThreshold(-24.0f);
    } else if (profile == "drive_clip_hiphop") {
        maximizer.setMode(pe::processor::SpectralMaximizer::Mode::HipHop);
        maximizer.setThreshold(-24.0f);
    } else if (profile == "drive_clip_dubstep") {
        maximizer.setMode(pe::processor::SpectralMaximizer::Mode::Dubstep);
        maximizer.setThreshold(-24.0f);
    } else if (profile == "drive_clip_drumnbass") {
        maximizer.setMode(pe::processor::SpectralMaximizer::Mode::DrumNBass);
        maximizer.setThreshold(-24.0f);
    } else if (profile == "drive_clip_trap") {
        maximizer.setMode(pe::processor::SpectralMaximizer::Mode::Trap);
        maximizer.setThreshold(-24.0f);
    } else if (profile == "drive_clip_house") {
        maximizer.setMode(pe::processor::SpectralMaximizer::Mode::House);
        maximizer.setThreshold(-24.0f);
    } else if (profile == "drive_clip_drums") {
        maximizer.setMode(pe::processor::SpectralMaximizer::Mode::Drums);
        maximizer.setThreshold(-24.0f);
    } else if (profile == "drive_clip_one_shot") {
        maximizer.setMode(pe::processor::SpectralMaximizer::Mode::OneShot);
        maximizer.setThreshold(-24.0f);
    } else if (profile == "drive_clip_one_shot_clean") {
        maximizer.setMode(pe::processor::SpectralMaximizer::Mode::OneShotClean);
        maximizer.setThreshold(-24.0f);
    } else if (profile == "drive_clip_percs") {
        maximizer.setMode(pe::processor::SpectralMaximizer::Mode::Percs);
        maximizer.setThreshold(-24.0f);
    } else if (profile == "drive_clip_bass") {
        maximizer.setMode(pe::processor::SpectralMaximizer::Mode::Bass);
        maximizer.setThreshold(-24.0f);
    } else if (profile == "drive_clip_kick808") {
        maximizer.setMode(pe::processor::SpectralMaximizer::Mode::Kick808);
        maximizer.setThreshold(-24.0f);
    } else if (profile == "drive_clip_bright") {
        maximizer.setMode(pe::processor::SpectralMaximizer::Mode::Bright);
        maximizer.setThreshold(-24.0f);
    } else if (profile == "drive_clip_glue") {
        maximizer.setMode(pe::processor::SpectralMaximizer::Mode::Glue);
        maximizer.setThreshold(-24.0f);
    } else if (profile == "drive_clip_acoustic") {
        maximizer.setMode(pe::processor::SpectralMaximizer::Mode::Acoustic);
        maximizer.setThreshold(-24.0f);
    } else if (profile == "drive_clip_vocal") {
        maximizer.setMode(pe::processor::SpectralMaximizer::Mode::Vocal);
        maximizer.setThreshold(-24.0f);
    } else if (profile == "drive_clip_clean") {
        maximizer.setMode(pe::processor::SpectralMaximizer::Mode::Clean);
        maximizer.setThreshold(-24.0f);
    }
}

uint64_t renderHash(size_t bandMultiplier, std::string_view profile = {}) {
    pe::processor::SpectralMaximizer maximizer;
    maximizer.prepare({sampleRate, static_cast<juce::uint32>(blockSize), static_cast<juce::uint32>(channels)});
    configure(maximizer, bandMultiplier, profile);

    juce::AudioBuffer<float> buffer(channels, blockSize);
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    int64_t samplePosition = 0;
    for (int blockIndex = 0; blockIndex < warmupBlocks; ++blockIndex) {
        fillInput(buffer, samplePosition);
        maximizer.process(context);
        samplePosition += blockSize;
    }

    uint64_t hash = 1469598103934665603ULL;
    for (int blockIndex = 0; blockIndex < validationBlocks; ++blockIndex) {
        fillInput(buffer, samplePosition);
        maximizer.process(context);
        samplePosition += blockSize;
        for (int channel = 0; channel < channels; ++channel) {
            auto const* samples = buffer.getReadPointer(channel);
            for (int sample = 0; sample < blockSize; ++sample) {
                hash ^= std::bit_cast<uint32_t>(samples[sample]);
                hash *= 1099511628211ULL;
            }
        }
    }
    return hash;
}

void renderOutput(size_t bandMultiplier, char const* outputPath, std::string_view profile = {}) {
    pe::processor::SpectralMaximizer maximizer;
    maximizer.prepare({sampleRate, static_cast<juce::uint32>(blockSize), static_cast<juce::uint32>(channels)});
    configure(maximizer, bandMultiplier, profile);
    juce::AudioBuffer<float> buffer(channels, blockSize);
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    int64_t samplePosition = 0;
    for (int blockIndex = 0; blockIndex < warmupBlocks; ++blockIndex) {
        fillInput(buffer, samplePosition);
        maximizer.process(context);
        samplePosition += blockSize;
    }
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    float maxClipAmountDb = 0.0f;
    float maxGainReductionDb = 0.0f;
    double sumClipAmountDb = 0.0;
    double sumGainReductionDb = 0.0;
    float maxPeak = 0.0f;
    double sumSquares = 0.0;
    size_t renderedSamples = 0;
    for (int blockIndex = 0; blockIndex < validationBlocks; ++blockIndex) {
        fillInput(buffer, samplePosition);
        maximizer.process(context);
        samplePosition += blockSize;
        maxClipAmountDb = std::max(maxClipAmountDb, maximizer.getClipAmountDb());
        maxGainReductionDb = std::max(maxGainReductionDb, maximizer.getGainReductionDb());
        sumClipAmountDb += maximizer.getClipAmountDb();
        sumGainReductionDb += maximizer.getGainReductionDb();
        for (int sample = 0; sample < blockSize; ++sample) {
            for (int channel = 0; channel < channels; ++channel) {
                auto const value = buffer.getSample(channel, sample);
                maxPeak = std::max(maxPeak, std::abs(value));
                sumSquares += static_cast<double>(value) * static_cast<double>(value);
                ++renderedSamples;
                output.write(reinterpret_cast<char const*>(&value), sizeof(value));
            }
        }
    }
    auto const renderedRms = std::sqrt(sumSquares / static_cast<double>(std::max<size_t>(1, renderedSamples)));
    std::cout << std::fixed << std::setprecision(6)
              << "clip_amount_db=" << maxClipAmountDb << '\n'
              << "gain_reduction_db=" << maxGainReductionDb << '\n'
              << "average_clip_db=" << sumClipAmountDb / validationBlocks << '\n'
              << "average_gain_reduction_db=" << sumGainReductionDb / validationBlocks << '\n'
              << "perceptual_allocator_updates=" << maximizer.getPerceptualAllocatorUpdateCount() << '\n'
              << "peak=" << maxPeak << '\n'
              << "rms=" << renderedRms << '\n';
}
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--drive24") {
        std::cout << "type,clip_db,rms_db\n";
        for (int type = 0; type < 17; ++type) {
            pe::processor::SpectralMaximizer dsp;
            configure(dsp, 4);
            dsp.setMode(static_cast<pe::processor::SpectralMaximizer::Mode>(type));
            dsp.setThreshold(-24.0f);
            dsp.prepare({sampleRate, blockSize, channels});
            juce::AudioBuffer<float> buffer(channels, blockSize);
            float clip = 0.0f;
            double energy = 0.0;
            for (int b = 0; b < 512; ++b) {
                fillInput(buffer, static_cast<int64_t>(b) * blockSize);
                juce::dsp::AudioBlock<float> block(buffer);
                dsp.process(juce::dsp::ProcessContextReplacing<float>(block));
                if (b >= 128) {
                    clip = std::max(clip, dsp.getClipAmountDb());
                    for (int ch = 0; ch < channels; ++ch)
                        for (int i = 0; i < blockSize; ++i) {
                            auto x = buffer.getSample(ch, i);
                            if (!std::isfinite(x)) return 2;
                            energy += x * x;
                        }
                }
            }
            std::cout << type << ',' << clip << ',' << 10.0 * std::log10(energy / (384 * channels * blockSize)) << '\n';
        }
        return 0;
    }
#if defined(_WIN32)
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR{1} << 2);
#endif
    auto const bandMultiplier = argc > 1 ? static_cast<size_t>(std::max(1, std::atoi(argv[1]))) : size_t{1};
    auto const profile = argc > 3 ? std::string_view{argv[3]} : std::string_view{};
    if (argc > 2) {
        renderOutput(bandMultiplier, argv[2], profile);
        return 0;
    }
    pe::processor::SpectralMaximizer maximizer;
    maximizer.prepare({sampleRate, static_cast<juce::uint32>(blockSize), static_cast<juce::uint32>(channels)});
    configure(maximizer, bandMultiplier, profile);

    juce::AudioBuffer<float> buffer(channels, blockSize);
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    int64_t samplePosition = 0;

    for (int blockIndex = 0; blockIndex < warmupBlocks; ++blockIndex) {
        fillInput(buffer, samplePosition);
        maximizer.process(context);
        samplePosition += blockSize;
    }

    std::vector<double> passes;
    passes.reserve(5);
    double checksum = 0.0;
    juce::AudioBuffer<float> inputBuffer(channels, blockSize);
    for (int pass = 0; pass < 5; ++pass) {
        double elapsedMs = 0.0;
        for (int blockIndex = 0; blockIndex < measuredBlocks; ++blockIndex) {
            fillInput(inputBuffer, samplePosition);
            for (int channel = 0; channel < channels; ++channel) {
                buffer.copyFrom(channel, 0, inputBuffer, channel, 0, blockSize);
            }
            auto const started = std::chrono::steady_clock::now();
            maximizer.process(context);
            auto const finished = std::chrono::steady_clock::now();
            elapsedMs += std::chrono::duration<double, std::milli>(finished - started).count();
            checksum += buffer.getSample(0, blockIndex % blockSize);
            samplePosition += blockSize;
        }
        passes.push_back(elapsedMs);
    }

    std::sort(passes.begin(), passes.end());
    auto const medianMs = passes[passes.size() / 2];
    auto const audioSeconds = static_cast<double>(measuredBlocks * blockSize) / sampleRate;
    auto const realtimePercent = medianMs / (audioSeconds * 1000.0) * 100.0;
    auto const nanosecondsPerSample = medianMs * 1.0e6 / static_cast<double>(measuredBlocks * blockSize * channels);

    std::cout << std::fixed << std::setprecision(4)
              << "band_multiplier=" << bandMultiplier << '\n'
              << "median_ms=" << medianMs << '\n'
              << "realtime_percent=" << realtimePercent << '\n'
              << "ns_per_sample=" << nanosecondsPerSample << '\n'
              << "checksum=" << checksum << '\n'
#if defined(PEAKEATER_CPU_BENCHMARK)
              << "descriptor_updates=" << maximizer.getSpectralDescriptorUpdateCount() << '\n'
              << "descriptor_event_updates=" << maximizer.getSpectralDescriptorEventUpdateCount() << '\n'
#endif
              << "output_hash=" << renderHash(bandMultiplier, profile) << '\n';
    return 0;
}

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <chrono>
#include <fstream>
#include <iomanip>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

void require(bool condition, char const* message) {
    if (!condition) throw std::runtime_error(message);
}

void set(juce::AudioProcessor& plugin, juce::String const& id, float value) {
    for (auto* parameter : plugin.getParameters()) {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
        if (ranged && ranged->paramID == id) {
            ranged->setValueNotifyingHost(ranged->convertTo0to1(value));
            return;
        }
    }
    throw std::runtime_error("parameter missing");
}

std::vector<float> render(float mix, int quality, bool bypass = false, int blockSize = 64) {
    std::unique_ptr<juce::AudioProcessor> plugin(createPluginFilter());
    set(*plugin, "OversampleRate", static_cast<float>(quality));
    set(*plugin, "DryWet", mix);
    set(*plugin, "ClippingType", 9.0f);
    plugin->setPlayConfigDetails(2, 2, 48000.0, blockSize);
    plugin->prepareToPlay(48000.0, blockSize);
    require(plugin->getLatencySamples() == (quality >= 2 ? 240 : 0), "reported latency");
    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    std::vector<float> result;
    for (int offset = 0; offset < 4096; offset += blockSize) {
        for (int i = 0; i < blockSize; ++i) {
            auto const x = static_cast<float>(0.01 * std::sin(0.071 * (offset + i)));
            buffer.setSample(0, i, x);
            buffer.setSample(1, i, -x);
        }
        if (bypass) plugin->processBlockBypassed(buffer, midi);
        else plugin->processBlock(buffer, midi);
        for (int i = 0; i < blockSize; ++i) {
            require(std::isfinite(buffer.getSample(0, i)), "finite output");
            result.push_back(buffer.getSample(0, i));
        }
    }
    plugin->releaseResources();
    return result;
}

int benchmark(char const* outputPath) {
    std::ofstream output;
    if (outputPath) output.open(outputPath, std::ios::binary);
    constexpr int blockSize = 128;
    juce::AudioBuffer<float> input(2, 32768), buffer(2, blockSize);
    for (int i = 0; i < input.getNumSamples(); ++i) {
        auto const t = i / 48000.0;
        auto const kick = 0.9 * std::exp(-std::fmod(t, 0.25) * 30.0) * std::sin(t * 339.292);
        for (int ch = 0; ch < 2; ++ch)
            input.setSample(ch, i, static_cast<float>(kick + 0.3 * std::sin(t * 860.796 + ch * 0.2)
                + 0.2 * std::sin(t * 6264.33) + 0.12 * std::sin(t * 45936.37 + ch * 0.5)));
    }
    std::cout << "quality,median_ns,p95_ns\n";
    for (int quality = 0; quality < 6; ++quality) {
        std::unique_ptr<juce::AudioProcessor> plugin(createPluginFilter());
        set(*plugin, "OversampleRate", static_cast<float>(quality));
        set(*plugin, "InputGain", 12.0f);
        plugin->setPlayConfigDetails(2, 2, 48000.0, blockSize);
        plugin->prepareToPlay(48000.0, blockSize);
        juce::MidiBuffer midi;
        std::vector<double> elapsed;
        for (int b = 0; b < 1800; ++b) {
            auto const offset = (b * blockSize) % input.getNumSamples();
            for (int ch = 0; ch < 2; ++ch) buffer.copyFrom(ch, 0, input, ch, offset, blockSize);
            auto const start = std::chrono::steady_clock::now();
            plugin->processBlock(buffer, midi);
            auto const end = std::chrono::steady_clock::now();
            if (b >= 300) elapsed.push_back(std::chrono::duration<double, std::nano>(end - start).count());
            if (output && b >= 300 && b < 364)
                for (int ch = 0; ch < 2; ++ch)
                    output.write(reinterpret_cast<char const*>(buffer.getReadPointer(ch)), blockSize * sizeof(float));
        }
        std::sort(elapsed.begin(), elapsed.end());
        std::cout << quality << ',' << std::fixed << std::setprecision(1) << elapsed[elapsed.size()/2]
                  << ',' << elapsed[elapsed.size()*95/100] << '\n';
        plugin->releaseResources();
    }
    return 0;
}

int renderLoudness(char const* prefix) {
    constexpr int blockSize = 128;
    for (int type : {0, 2, 6, 9, 11, 5}) {
        std::unique_ptr<juce::AudioProcessor> plugin(createPluginFilter());
        set(*plugin, "OversampleRate", 2.0f);
        set(*plugin, "ClippingType", static_cast<float>(type));
        set(*plugin, "Threshold", -24.0f);
        set(*plugin, "Ceiling", -1.0f);
        set(*plugin, "TruePeakLimit", 1.0f);
        set(*plugin, "DryWet", 1.0f);
        plugin->setPlayConfigDetails(2, 2, 48000.0, blockSize);
        plugin->prepareToPlay(48000.0, blockSize);
        std::ofstream output(std::string(prefix) + "-" + std::to_string(type) + ".f32", std::ios::binary);
        require(output.is_open(), "loudness output file");
        juce::AudioBuffer<float> buffer(2, blockSize);
        juce::MidiBuffer midi;
        float peak = 0.0f;
        for (int offset = 0; offset < 48000 * 9; offset += blockSize) {
            for (int i = 0; i < blockSize; ++i) {
                auto const t = (offset + i) / 48000.0;
                auto const kick = 0.72 * std::exp(-std::fmod(t, 0.5) * 22.0) * std::sin(339.292 * t);
                for (int ch = 0; ch < 2; ++ch)
                    buffer.setSample(ch, i, static_cast<float>(kick + 0.24 * std::sin(860.796 * t + ch * 0.31)
                        + 0.20 * std::sin(6264.33 * t + ch * 0.124) + 0.10 * std::sin(45936.37 * t + ch * 0.527)));
            }
            plugin->processBlock(buffer, midi);
            for (int i = 0; i < blockSize; ++i) {
                for (int ch = 0; ch < 2; ++ch) {
                    auto const value = buffer.getSample(ch, i);
                    require(std::isfinite(value), "finite loudness render");
                    peak = std::max(peak, std::abs(value));
                    if (offset >= 48000) output.write(reinterpret_cast<char const*>(&value), sizeof(value));
                }
            }
        }
        require(output.good(), "loudness output write");
        require(peak <= std::pow(10.0f, -1.0f / 20.0f) + 0.00001f, "full processor sample ceiling");
        std::cout << "type=" << type << " peak=" << peak << '\n';
        plugin->releaseResources();
    }
    return 0;
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (argc == 3 && std::string(argv[1]) == "--loudness") return renderLoudness(argv[2]);
    if (argc > 1) return benchmark(argc > 2 ? argv[2] : nullptr);
    {
        std::unique_ptr<juce::AudioProcessor> plugin(createPluginFilter());
        juce::RangedAudioParameter* drive = nullptr;
        for (auto* parameter : plugin->getParameters()) {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
            if (ranged && ranged->paramID == "Threshold") drive = ranged;
        }
        require(drive != nullptr, "Drive parameter exists");
        require(drive->getNormalisableRange().start == -24.0f && drive->getNormalisableRange().end == 0.0f,
                "Drive range must be 0 to +24 dB");
        set(*plugin, "Threshold", -24.0f);
        juce::MemoryBlock saved;
        plugin->getStateInformation(saved);
        set(*plugin, "Threshold", -6.0f);
        plugin->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        require(std::abs(drive->convertFrom0to1(drive->getValue()) + 24.0f) < 0.001f, "Drive state recall");
    }
    for (int quality = 0; quality < 6; ++quality) {
        auto const dry = render(0.0f, quality);
        auto const bypass = render(0.0f, quality, true);
        auto const wet = render(1.0f, quality);
        auto const mix = render(0.5f, quality);
        auto const latency = quality >= 2 ? 240 : 0;
        float mixError = 0.0f;
        for (size_t i = 0; i < dry.size(); ++i) {
            auto const expected = static_cast<int>(i) < latency ? 0.0f
                : static_cast<float>(0.01 * std::sin(0.071 * (static_cast<int>(i) - latency)));
            require(std::abs(dry[i] - expected) < 1e-7f, "dry integrity");
            require(std::abs(bypass[i] - expected) < 1e-7f, "host bypass delay");
            mixError = std::max(mixError, std::abs(mix[i] - 0.5f * (dry[i] + wet[i])));
        }
        std::cout << "quality=" << quality << " latency=" << latency << " mix_error=" << mixError << '\n';
        require(mixError < 1e-5f, "parallel mix integrity");
    }
    auto const a = render(1.0f, 2, false, 64);
    auto const b = render(1.0f, 2, false, 128);
    float difference = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) difference = std::max(difference, std::abs(a[i] - b[i]));
    std::cout << "block_size_difference=" << difference << '\n';
    std::cout << "PASS processor latency, dry, host bypass, parallel mix\n";
}

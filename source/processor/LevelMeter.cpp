#include "LevelMeter.h"

#include <cmath>

namespace pe::processor {

namespace {
float constexpr gMinusInfinity = -36.0f;
}  // namespace

template <typename T>
LevelMeter<T>::LevelMeter() : dbfs(gMinusInfinity), peakDbfs(gMinusInfinity), rmsDbfs(gMinusInfinity), crestFactorDb(0) {}

template <typename T>
void LevelMeter<T>::updateLevels(juce::dsp::AudioBlock<T> const& audioBlock) {
    auto const range = audioBlock.findMinAndMax();
    auto const magnitude = juce::jmax(range.getStart(), -range.getStart(), range.getEnd(), -range.getEnd());
    auto sumSquares = static_cast<T>(0);
    auto sampleCount = static_cast<T>(0);

    for (size_t channel = 0; channel < audioBlock.getNumChannels(); ++channel) {
        auto const* samples = audioBlock.getChannelPointer(channel);
        for (size_t sample = 0; sample < audioBlock.getNumSamples(); ++sample) {
            auto const value = samples[sample];
            sumSquares += value * value;
            sampleCount += static_cast<T>(1);
        }
    }

    auto const rms = sampleCount > static_cast<T>(0) ? std::sqrt(sumSquares / sampleCount) : static_cast<T>(0);
    auto const peakDb = juce::Decibels::gainToDecibels<T>(magnitude, gMinusInfinity);
    auto const rmsDb = juce::Decibels::gainToDecibels<T>(rms, gMinusInfinity);
    dbfs = peakDb;
    peakDbfs = peakDb;
    rmsDbfs = rmsDb;
    crestFactorDb = juce::jmax(static_cast<T>(0), peakDb - rmsDb);
}

template <typename T>
std::atomic<T>& LevelMeter<T>::getDBFS() {
    return dbfs;
}

template <typename T>
std::atomic<T>& LevelMeter<T>::getPeakDBFS() {
    return peakDbfs;
}

template <typename T>
std::atomic<T>& LevelMeter<T>::getRmsDBFS() {
    return rmsDbfs;
}

template <typename T>
std::atomic<T>& LevelMeter<T>::getCrestFactorDB() {
    return crestFactorDb;
}

template class LevelMeter<float>;
template class LevelMeter<double>;
}  // namespace pe::processor

#include "OscilloscopeBuffer.h"

#include <cmath>

namespace pe::processor {

OscilloscopeBuffer::OscilloscopeBuffer() {
    reset();
}

void OscilloscopeBuffer::reset() {
    for (auto& sample : mSamples) {
        sample.store(0.0f, std::memory_order_relaxed);
    }
    for (auto& sample : mSubSamples) {
        sample.store(0.0f, std::memory_order_relaxed);
    }
    for (auto& sample : mLowSamples) {
        sample.store(0.0f, std::memory_order_relaxed);
    }
    for (auto& sample : mMidSamples) {
        sample.store(0.0f, std::memory_order_relaxed);
    }
    for (auto& sample : mHighSamples) {
        sample.store(0.0f, std::memory_order_relaxed);
    }
    for (auto& sample : mAirSamples) {
        sample.store(0.0f, std::memory_order_relaxed);
    }
    mSubState = 0.0f;
    mLowState = 0.0f;
    mMidState = 0.0f;
    mHighState = 0.0f;
    mWriteIndex.store(0, std::memory_order_release);
    mSequence.store(0, std::memory_order_release);
}

void OscilloscopeBuffer::push(juce::AudioBuffer<float> const& buffer) {
    auto const numSamples = buffer.getNumSamples();
    auto const numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0) {
        return;
    }

    auto writeIndex = mWriteIndex.load(std::memory_order_relaxed);
    auto const channelScale = 1.0f / static_cast<float>(numChannels);
    constexpr auto analysisStride = 2;
    for (int sample = 0; sample < numSamples; sample += analysisStride) {
        auto mixed = 0.0f;
        auto const endSample = std::min(numSamples, sample + analysisStride);
        for (int sourceSample = sample; sourceSample < endSample; ++sourceSample) {
            auto candidate = 0.0f;
            for (int channel = 0; channel < numChannels; ++channel) {
                candidate += buffer.getSample(channel, sourceSample);
            }
            candidate *= channelScale;
            if (std::abs(candidate) >= std::abs(mixed)) {
                mixed = candidate;
            }
        }

        mSubState += 0.010f * (mixed - mSubState);
        mLowState += 0.032f * (mixed - mLowState);
        mMidState += 0.095f * (mixed - mMidState);
        mHighState += 0.245f * (mixed - mHighState);
        auto const sub = mSubState;
        auto const low = mLowState - mSubState;
        auto const mid = mMidState - mLowState;
        auto const high = mHighState - mMidState;
        auto const air = mixed - mHighState;

        mSamples[writeIndex].store(juce::jlimit(-1.4f, 1.4f, mixed), std::memory_order_relaxed);
        mSubSamples[writeIndex].store(juce::jlimit(-1.4f, 1.4f, sub * 1.55f), std::memory_order_relaxed);
        mLowSamples[writeIndex].store(juce::jlimit(-1.4f, 1.4f, low * 1.45f), std::memory_order_relaxed);
        mMidSamples[writeIndex].store(juce::jlimit(-1.4f, 1.4f, mid * 1.20f), std::memory_order_relaxed);
        mHighSamples[writeIndex].store(juce::jlimit(-1.4f, 1.4f, high * 1.25f), std::memory_order_relaxed);
        mAirSamples[writeIndex].store(juce::jlimit(-1.4f, 1.4f, air * 1.35f), std::memory_order_relaxed);
        writeIndex = (writeIndex + 1) % bufferSize;
    }

    mWriteIndex.store(writeIndex, std::memory_order_release);
    mSequence.fetch_add(1, std::memory_order_release);
}

void OscilloscopeBuffer::copySnapshot(std::vector<float>& snapshot) const {
    if (snapshot.size() != bufferSize) {
        snapshot.assign(bufferSize, 0.0f);
    }

    auto const writeIndex = mWriteIndex.load(std::memory_order_acquire);

    for (size_t i = 0; i < bufferSize; ++i) {
        auto const sourceIndex = (writeIndex + i) % bufferSize;
        snapshot[i] = mSamples[sourceIndex].load(std::memory_order_relaxed);
    }
}

void OscilloscopeBuffer::copyBandSnapshots(std::vector<float>& subSnapshot, std::vector<float>& lowSnapshot,
                                           std::vector<float>& midSnapshot, std::vector<float>& highSnapshot,
                                           std::vector<float>& airSnapshot) const {
    if (subSnapshot.size() != bufferSize) {
        subSnapshot.assign(bufferSize, 0.0f);
    }
    if (lowSnapshot.size() != bufferSize) {
        lowSnapshot.assign(bufferSize, 0.0f);
    }
    if (midSnapshot.size() != bufferSize) {
        midSnapshot.assign(bufferSize, 0.0f);
    }
    if (highSnapshot.size() != bufferSize) {
        highSnapshot.assign(bufferSize, 0.0f);
    }
    if (airSnapshot.size() != bufferSize) {
        airSnapshot.assign(bufferSize, 0.0f);
    }

    auto const writeIndex = mWriteIndex.load(std::memory_order_acquire);

    for (size_t i = 0; i < bufferSize; ++i) {
        auto const sourceIndex = (writeIndex + i) % bufferSize;
        subSnapshot[i] = mSubSamples[sourceIndex].load(std::memory_order_relaxed);
        lowSnapshot[i] = mLowSamples[sourceIndex].load(std::memory_order_relaxed);
        midSnapshot[i] = mMidSamples[sourceIndex].load(std::memory_order_relaxed);
        highSnapshot[i] = mHighSamples[sourceIndex].load(std::memory_order_relaxed);
        airSnapshot[i] = mAirSamples[sourceIndex].load(std::memory_order_relaxed);
    }
}

}  // namespace pe::processor

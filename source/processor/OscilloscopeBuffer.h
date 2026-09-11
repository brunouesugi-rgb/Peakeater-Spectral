#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <vector>

namespace pe::processor {

class OscilloscopeBuffer {
   public:
    static constexpr size_t bufferSize = 768;

    OscilloscopeBuffer();

    void reset();
    void push(juce::AudioBuffer<float> const& buffer);
    void copySnapshot(std::vector<float>& snapshot) const;
    void copyBandSnapshots(std::vector<float>& subSnapshot, std::vector<float>& lowSnapshot, std::vector<float>& midSnapshot,
                           std::vector<float>& highSnapshot, std::vector<float>& airSnapshot) const;
    [[nodiscard]] std::uint64_t getSequence() const noexcept { return mSequence.load(std::memory_order_acquire); }

   private:
    std::array<std::atomic<float>, bufferSize> mSamples;
    std::array<std::atomic<float>, bufferSize> mSubSamples;
    std::array<std::atomic<float>, bufferSize> mLowSamples;
    std::array<std::atomic<float>, bufferSize> mMidSamples;
    std::array<std::atomic<float>, bufferSize> mHighSamples;
    std::array<std::atomic<float>, bufferSize> mAirSamples;
    std::atomic<size_t> mWriteIndex{0};
    std::atomic<std::uint64_t> mSequence{0};
    float mSubState = 0.0f;
    float mLowState = 0.0f;
    float mMidState = 0.0f;
    float mHighState = 0.0f;
};

}  // namespace pe::processor

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <deque>

namespace pe::processor {

class DynamicsMeter {
   public:
    void update(float clipAmountDb, float gainReductionDb, juce::AudioBuffer<float> const& outputBuffer, double sampleRate,
                bool updateTruePeakAndLoudness, bool updateLoudnessOnly);
    void reset();
    void resetMaxima();

    [[nodiscard]] float getClipAmountDb() const;
    [[nodiscard]] float getGainReductionDb() const;
    [[nodiscard]] float getTruePeakDb() const;
    [[nodiscard]] float getTruePeakMaxDb() const;
    [[nodiscard]] float getOutputPeakDb() const;
    [[nodiscard]] float getOutputPeakMaxDb() const;
    [[nodiscard]] float getClipAmountMaxDb() const;
    [[nodiscard]] float getGainReductionMaxDb() const;
    [[nodiscard]] float getMomentaryLufs() const;
    [[nodiscard]] float getShortTermLufs() const;
    [[nodiscard]] float getIntegratedLufs() const;
    [[nodiscard]] float getCrestDb() const;
    [[nodiscard]] float getCrestMaxDb() const;

   private:
    struct WeightingState {
        double stage1X1 = 0.0;
        double stage1X2 = 0.0;
        double stage1Y1 = 0.0;
        double stage1Y2 = 0.0;
        double stage2X1 = 0.0;
        double stage2X2 = 0.0;
        double stage2Y1 = 0.0;
        double stage2Y2 = 0.0;
    };

    struct WeightingCoefficients {
        double sampleRate = 0.0;
        double hpB0 = 0.0;
        double hpB1 = 0.0;
        double hpB2 = 0.0;
        double hpA1 = 0.0;
        double hpA2 = 0.0;
        double shelfB0 = 0.0;
        double shelfB1 = 0.0;
        double shelfB2 = 0.0;
        double shelfA1 = 0.0;
        double shelfA2 = 0.0;
    };

    struct BlockEnergy {
        double energy = 0.0;
        uint64_t samples = 0;
    };

    [[nodiscard]] static float estimateTruePeakDb4x(juce::AudioBuffer<float> const& buffer);
    [[nodiscard]] static float bufferPeakDb(juce::AudioBuffer<float> const& buffer);
    [[nodiscard]] double weightedMeanSquare(juce::AudioBuffer<float> const& buffer, double sampleRate);
    void pushWindowEnergy(double energy, uint64_t samples, double sampleRate);
    [[nodiscard]] double windowEnergy(uint64_t maxSamples) const;
    [[nodiscard]] double gatedIntegratedEnergy() const;
    [[nodiscard]] static float lufsFromMeanSquare(double meanSquare);

    std::atomic<float> mClipAmountDb{0.0f};
    std::atomic<float> mGainReductionDb{0.0f};
    std::atomic<float> mTruePeakDb{-120.0f};
    std::atomic<float> mTruePeakMaxDb{-120.0f};
    std::atomic<float> mOutputPeakDb{-120.0f};
    std::atomic<float> mOutputPeakMaxDb{-120.0f};
    std::atomic<float> mClipAmountMaxDb{0.0f};
    std::atomic<float> mGainReductionMaxDb{0.0f};
    std::atomic<float> mMomentaryLufs{-120.0f};
    std::atomic<float> mShortTermLufs{-120.0f};
    std::atomic<float> mIntegratedLufs{-120.0f};
    std::atomic<float> mCrestDb{0.0f};
    std::atomic<float> mCrestMaxDb{0.0f};
    std::array<WeightingState, 2> mWeightingStates{};
    WeightingCoefficients mWeightingCoefficients{};
    std::deque<BlockEnergy> mEnergyHistory;
    std::deque<BlockEnergy> mIntegratedBlocks;
    uint64_t mHistorySamples = 0;
    uint64_t mIntegratedSamples = 0;
    double mIntegratedAccumulatorEnergy = 0.0;
    uint64_t mIntegratedAccumulatorSamples = 0;
    double mCurrentSampleRate = 0.0;
};

}  // namespace pe::processor

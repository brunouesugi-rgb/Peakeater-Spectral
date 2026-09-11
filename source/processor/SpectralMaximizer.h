#pragma once

// allow: SIZE_OK - Central DSP state contract mirrors the real-time processor.

#include <array>
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace pe::processor {

class SpectralMaximizer {
   public:
    enum class Mode {
        EDM,
        HipHop,
        Drums,
        OneShot,
        Acoustic,
        Vocal,
        Bass,
        Bright,
        Glue,
        Clean,
        Percs,
        Dubstep,
        DrumNBass,
        House,
        Trap,
        Kick808,
        OneShotClean
    };

    enum class ToneStyle {
        Warm,
        Clean,
        Bright
    };

    enum class LimiterStyle {
        Transparent,
        Punch,
        Glue,
        Safe,
        Loud
    };

    enum class SaturationStyle {
        Clean,
        Warm,
        Tube,
        Tape,
        Transformer
    };

    enum class DeltaSource {
        All,
        Limiter,
        Spectral,
        SatClip,
        Ceiling
    };

    void prepare(juce::dsp::ProcessSpec const& spec);
    void reset();

    void setMode(Mode mode);
    void setThreshold(float thresholdDb);
    void setCeiling(float ceilingDb);
    void setTone(float tone);
    void setToneStyle(ToneStyle toneStyle);
    void setAttack(float attackMs);
    void setHold(float holdMs);
    void setRelease(float releaseMs);
    void setTransientRecovery(float recovery);
    void setLookahead(float lookaheadMs);
    void setBandMultiplier(size_t multiplier);
    void setDetectorHp(float detectorHpHz);
    void setSaturation(float saturation);
    void setTruePeakLimit(bool shouldLimitTruePeak);
    void setAdaptiveRelease(float adaptiveRelease);
    void setStereoLink(float stereoLink);
    void setLowProtect(float lowProtect);
    void setTruePeakMargin(float truePeakMarginDb);
    void setGrLimit(float grLimitDb);
    void setPunchProtect(float punchProtect);
    void setReleaseShape(float releaseShape);
    void setLimiterStyle(LimiterStyle limiterStyle);
    void setHfGuard(float hfGuard);
    void setDetectorTilt(float detectorTilt);
    void setDeltaListen(bool shouldListenDelta);
    void setDeltaSource(DeltaSource deltaSource);
    void setFinalClip(float finalClip);
    void setSaturationStyle(SaturationStyle saturationStyle);
    void setSaturationTone(float saturationTone);
    void setSaturationDensity(float saturationDensity);
    void setBassSafe(float bassSafe);
    void setBassRecover(float bassRecover);

    void process(juce::dsp::ProcessContextReplacing<float> const& context);

   private:
    static constexpr size_t limiterBaseBandCount = 12;
    static constexpr size_t spectralBaseBinCount = 32;
    static constexpr size_t maxRequestedBandMultiplier = 32;
    static constexpr size_t maxRealtimeBandMultiplier = 2;
    static constexpr size_t limiterBandCount = limiterBaseBandCount * maxRealtimeBandMultiplier;
    static constexpr size_t limiterCrossoverCount = limiterBandCount - 1;
    static constexpr size_t spectralBinCount = spectralBaseBinCount * maxRealtimeBandMultiplier;
    static constexpr size_t spectralCrossoverCount = spectralBinCount - 1;
    static constexpr size_t ceilingTargetWindowLength = 8;

    struct BandSettings {
        float ratio;
        float attackMs;
        float releaseMs;
        float weight;
        float thresholdOffsetDb;
        float transientScale;
    };

    struct ModeSettings {
        std::array<BandSettings, 3> bands;
        float driveDb;
        float kneeDb;
        float lookaheadStrength;
        float ceilingReleaseMs;
        float maxBandReductionDb;
        float transientScale;
        float thresholdDriveScale;
        float softClipAmount;
        float hardClipBlend;
        float spectralControlScale;
        float detectorHpHz;
        float lowDetectorScale;
        float stereoLink;
        float grLimitDb;
        float finalLimiterStrength;
        float loudnessFocus;
        float densityLift;
        float safetyMarginDb;
        float spectralSustainScale = 1.0f;
        float spectralUpwardStartOffsetDb = 0.0f;
        float spectralMidFocusScale = 1.0f;
        float spectralGenreLift = 0.0f;
    };

    struct PeakBudgetFeedback {
        float clipPressure = 0.0f;
        float ceilingPressure = 0.0f;
        float sustainClipDebt = 0.0f;
        float transientRisk = 0.0f;
        float lowMonoRisk = 0.0f;
        float densitySuccess = 0.0f;
    };

    struct MidSideBudget {
        float midPeakPressure = 0.0f;
        float sidePeakPressure = 0.0f;
        float lowSideRisk = 0.0f;
        float monoCompatibilityRisk = 0.0f;
        float widthPreserve = 1.0f;
    };

    struct ChannelState {
        float detectorLow = 0.0f;
        float detectorHighLowpass = 0.0f;
        float detectorHpLowpass = 0.0f;
        float signalLow = 0.0f;
        float signalHighLowpass = 0.0f;
        std::array<float, limiterCrossoverCount> limiterDetectorLowpass{};
        std::array<float, limiterCrossoverCount> limiterSignalLowpass{};
    std::array<float, limiterBandCount> limiterEnvelope{};
    std::array<float, limiterBandCount> limiterTransientFastEnvelope{};
    std::array<float, limiterBandCount> limiterTransientSlowEnvelope{};
    std::array<float, limiterBandCount> limiterPreviousMagnitude{};
    std::array<int, limiterBandCount> limiterHoldSamples{};
    std::array<float, limiterBandCount> limiterDetectorBands{};
    std::array<float, limiterBandCount> limiterSignalBands{};
        float saturationFastEnvelope = 0.0f;
        float saturationSlowEnvelope = 0.0f;
        float saturationSplitLowpass = 0.0f;
        float bassRecoverPreClipLowpass = 0.0f;
        float bassRecoverPostClipLowpass = 0.0f;
        float previousSaturationLowInput = 0.0f;
        float previousSaturationHighInput = 0.0f;
        float previousFinalClipInput = 0.0f;
        float previousCeilingInput2 = 0.0f;
        float previousCeilingInput = 0.0f;
        float rmsContourPeakEnvelope = 0.0f;
        float rmsContourEnergyEnvelope = 0.0f;
        float rmsContourAdaptiveAmount = 0.0f;
        std::array<float, spectralCrossoverCount> spectralDetectorLowpass{};
        std::array<float, spectralCrossoverCount> spectralSignalLowpass{};
        std::array<float, spectralBinCount> spectralEnvelope{};
    std::array<float, spectralBinCount> spectralFastEnvelope{};
    std::array<float, spectralBinCount> spectralSlowEnvelope{};
    std::array<float, spectralBinCount> spectralPreviousMagnitude{};
    std::array<int, spectralBinCount> spectralHoldSamples{};
    std::array<float, spectralBinCount> spectralGainDb{};
    std::array<float, spectralBinCount> spectralLoudnessAllocation{};
    std::array<float, spectralBinCount> spectralDetectorBins{};
    std::array<float, spectralBinCount> spectralSignalBins{};
        std::array<float, 3> envelope{0.0f, 0.0f, 0.0f};
        std::array<float, 3> transientFastEnvelope{0.0f, 0.0f, 0.0f};
        std::array<float, 3> transientSlowEnvelope{0.0f, 0.0f, 0.0f};
        std::array<float, 3> previousMagnitude{0.0f, 0.0f, 0.0f};
        std::array<int, 3> holdSamples{0, 0, 0};
        float limiterOvershootGain = 1.0f;
        float peakReliefAllpassInput = 0.0f;
        float peakReliefAllpassOutput = 0.0f;
        float maskingResidual = 0.0f;
        float maskingResidualEnvelope = 0.0f;
        float ceilingEnvelope = 0.0f;
        float ceilingGain = 1.0f;
        std::array<float, ceilingTargetWindowLength> ceilingTargetGainWindow{};
        size_t ceilingTargetGainIndex = 0;
        PeakBudgetFeedback peakBudgetFeedback{};
        float spectralUpwardAppliedDb = 0.0f;
        float spectralTransientRisk = 0.0f;
        float spectralLowRisk = 0.0f;
        float spectralHighBandRatio = 0.0f;
        float spectralBinSpread = 0.25f;
        float spectralFlatness = 0.30f;
        float spectralCentroidNorm = 0.0f;
        float spectralTransientDensity = 0.0f;
        size_t spectralDescriptorCountdown = 0;
        size_t spectralControlPhase = 0;
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
        size_t spectralDescriptorInterval = 1;
        size_t spectralDescriptorStableUpdates = 0;
        size_t spectralDescriptorEventCooldown = 0;
        float spectralDescriptorInputEnvelope = 0.0f;
#endif
    };

    struct ClipResult {
        float sample = 0.0f;
        float reductionDb = 0.0f;
    };

    struct PeakBudgetControls {
        float densityBudget = 0.0f;
        float transientReserve = 0.0f;
        float lowReserve = 0.0f;
        float clipRelief = 0.0f;
        float peakPressure = 0.0f;
        float upwardQuality = 0.0f;
        float cleanLift = 0.0f;
        float microGr = 0.0f;
        float loudnessDistribution = 0.0f;
        float peakPacking = 0.0f;
        float sustainLift = 0.0f;
        float crestMicroGr = 0.0f;
        float roundedClipQuality = 0.0f;
        float sideClipRelief = 0.0f;
        float lowSideRisk = 0.0f;
        float widthPreserve = 1.0f;
    };

    struct SpectralSharedControls {
        float driveAmount = 0.0f;
        float driveSafety = 0.0f;
        float heavyDriveSafety = 0.0f;
        float extremeDriveSafety = 0.0f;
        float hfGuardAmount = 0.0f;
        float smoothBias = 0.0f;
        float adaptive = 0.0f;
        float transientProtect = 0.0f;
        float bassLock = 0.0f;
        float upwardDriveGate = 0.0f;
        float cleanLoudnessDrive = 0.0f;
        float genreSustainScale = 1.0f;
        float genreUpwardStartOffsetDb = 0.0f;
        float genreUpwardMaxBoostDb = 0.0f;
        float genreUpwardSlopeBoost = 0.0f;
        float loudnessEfficiency = 0.0f;
        float spectral2DensityAssist = 0.0f;
        float fastAttack = 0.0f;
        float slowAttack = 0.0f;
        float slowRelease = 0.0f;
        float driveTargetRelax = 0.0f;
        float spectralReductionLimit = 0.0f;
        float upwardSmoothingBase = 0.0f;
        float reductionSmoothingBase = 0.0f;
        float stereoSustainGain = 1.0f;
        bool lowCpuQuality = true;
        bool percussiveMode = false;
    };

    struct StyleSettings {
        float punchOffset = 0.0f;
        float releaseOffset = 0.0f;
        float grLimitScale = 1.0f;
        float hfGuardOffset = 0.0f;
        float finalClipOffset = 0.0f;
        float safetyMarginOffsetDb = 0.0f;
        float limiterStrengthScale = 1.0f;
    };

    struct SaturationSettings {
        float driveScale = 1.0f;
        float densityScale = 1.0f;
        float evenAmount = 1.0f;
        float oddAmount = 1.0f;
        float tapeSoftness = 0.0f;
        float transientProtection = 1.0f;
    };

    [[nodiscard]] static ModeSettings getModeSettings(Mode mode);
    [[nodiscard]] static ModeSettings qualityAdjustedMode(Mode mode, ModeSettings settings, float quality, size_t bandMultiplier);
    [[nodiscard]] static StyleSettings getStyleSettings(LimiterStyle limiterStyle);
    [[nodiscard]] static SaturationSettings getSaturationSettings(SaturationStyle saturationStyle);
    [[nodiscard]] static float dbToGain(float db);
    [[nodiscard]] static float gainToDb(float gain);
    [[nodiscard]] static float thresholdToDriveDb(float thresholdDb);
    [[nodiscard]] static float compressionGainDb(float envelopeDb, float thresholdDb, float inverseRatio,
                                                 float kneeDb, float inverseTwoKneeDb);
    [[nodiscard]] static float roundedHardClip(float sample, float kneeWidth);
    [[nodiscard]] static float applySyncedAllpassPeakRelief(float sample, float amount, float coefficient, ChannelState& state);
    [[nodiscard]] static ClipResult applyMaskingShapedPeakResidual(float sample, float ceilingGain, float authority,
                                                                   float transientRisk, float lowRisk, float spectralFlatness,
                                                                   float highBandRatio, float previousInput, ChannelState& state);
    [[nodiscard]] static ClipResult applyCeiling(float sample, float ceilingGain, float releaseMs, ModeSettings const& mode,
                                                 ChannelState& state, double sampleRate, bool truePeakLimit,
                                                 float truePeakMarginDb, float adaptiveRelease, float punchProtect,
                                                 float releaseShape, float safetyMarginOffsetDb, float limiterStrengthScale,
                                                 float grLimitDb, float rmsContourAmount,
                                                 float contourAttackCoefficient, float contourReleaseCoefficient);
    [[nodiscard]] static float applySaturation(float sample, float amount, float densityLift, float driveDb, float transientProtect,
                                               SaturationSettings const& settings, float saturationDensity, float previousInput);
    [[nodiscard]] static ClipResult applyFinalClip(float sample, float ceilingGain, float amount, float previousInput);
    [[nodiscard]] static float smoothToward(float current, float target, float timeMs, double sampleRate);
    [[nodiscard]] static float smoothingCoefficient(float timeMs, double sampleRate);
    [[nodiscard]] static float fastSmoothingCoefficient(float timeMs, double sampleRate);
    [[nodiscard]] static float hermiteShape(float amount);
    [[nodiscard]] static float smoothSuppressionShape(float amount, float smoothBias);
    [[nodiscard]] static float onePoleCoefficient(float frequencyHz, double sampleRate);
    [[nodiscard]] static float mixDb(float dryDb, float wetDb, float amount);
    [[nodiscard]] static float fastDbToGain(float db);
    [[nodiscard]] static float fastGainToDb(float gain);
    [[nodiscard]] static size_t activeBandCount(size_t baseCount, size_t multiplier, size_t maximum);
    [[nodiscard]] static BandSettings bandSettingsForFrequency(float frequencyHz, ModeSettings const& mode);
    [[nodiscard]] static float spectralTargetOffsetDb(float frequencyHz, ModeSettings const& mode, float tone,
                                                      ToneStyle toneStyle, float quality);
    [[nodiscard]] static float spectralToneGainDb(float frequencyHz, float tone, ToneStyle toneStyle, float quality);
    [[nodiscard]] static float spectralRatioForFrequency(float frequencyHz, float baseRatio, float rolloff);
    void updateBankLayout(ModeSettings const& mode, float quality, size_t activeLimiterBands, size_t activeSpectralBins);
    [[nodiscard]] float processLimiterBank(ChannelState& state, float detectorInput, float signalInput, ModeSettings const& mode,
                                           float thresholdDriveDb, float ceilingDb, size_t activeBands, float adaptiveRelease,
                                           float lowProtect, float grLimitDb, float punchProtect, float releaseShape,
                                           float lookaheadAmount, int holdSamples, float overshootReleaseCoefficient,
                                           PeakBudgetControls const& peakBudget,
                                           float& gainReductionDb) const;
    [[nodiscard]] float processSpectralBank(ChannelState& state, float detectorInput, float signalInput, ModeSettings const& mode,
                                            float thresholdDriveDb, float ceilingDb, float depth, size_t activeBins,
                                            PeakBudgetControls const& peakBudget, SpectralSharedControls const& shared,
                                            float& gainReductionDb) const;
    [[nodiscard]] float toneWeight(size_t bandIndex) const;

   public:
    [[nodiscard]] float getClipAmountDb() const;
    [[nodiscard]] float getGainReductionDb() const;
#if defined(PEAKEATER_CPU_BENCHMARK)
    [[nodiscard]] uint64_t getSpectralDescriptorUpdateCount() const { return mSpectralDescriptorUpdateCount; }
    [[nodiscard]] uint64_t getSpectralDescriptorEventUpdateCount() const { return mSpectralDescriptorEventUpdateCount; }
    [[nodiscard]] uint64_t getPerceptualAllocatorUpdateCount() const { return mPerceptualAllocatorUpdateCount; }
    void setMaskingResidualEnabled(bool enabled) { mMaskingResidualEnabled = enabled; }
    void setPerceptualAllocatorEnabled(bool enabled) { mPerceptualAllocatorEnabled = enabled; }
#endif

   private:
    double mSampleRate = 44100.0;
    size_t mNumChannels = 2;
    float mLowCoefficient = 0.0f;
    float mHighCoefficient = 0.0f;
    float mLimiterOvershootAttackCoefficient = 0.0f;
    float mLimiterOvershootReleaseBaseCoefficient = 0.0f;
    float mThresholdSmoothCoefficient = 0.0f;
    float mCeilingSmoothCoefficient = 0.0f;
    float mToneSmoothCoefficient = 0.0f;
    float mSaturationSmoothCoefficient = 0.0f;
    float mFinalClipSmoothCoefficient = 0.0f;
    float mHfGuardSmoothCoefficient = 0.0f;
    float mSaturationFastCoefficient = 0.0f;
    float mSaturationSlowCoefficient = 0.0f;
    float mFeedbackAttackCoefficient = 0.0f;
    float mFeedbackReleaseCoefficient = 0.0f;

    Mode mMode = Mode::EDM;
    float mThresholdDb = -6.0f;
    float mCeilingDb = -0.1f;
    float mTone = 0.0f;
    ToneStyle mToneStyle = ToneStyle::Clean;
    float mAttackMs = 1.0f;
    float mHoldMs = 0.0f;
    int mSpectralHoldSamples = 0;
    float mReleaseMs = 80.0f;
    float mTransientRecovery = 0.0f;
    float mLookaheadMs = 0.0f;
    size_t mBandMultiplier = 1;
    size_t mSpectralDescriptorInterval = 24;
    bool mPercussiveMode = false;
#if defined(PEAKEATER_CPU_BENCHMARK)
    mutable uint64_t mSpectralDescriptorUpdateCount = 0;
    mutable uint64_t mSpectralDescriptorEventUpdateCount = 0;
    mutable uint64_t mPerceptualAllocatorUpdateCount = 0;
    bool mMaskingResidualEnabled = true;
    bool mPerceptualAllocatorEnabled = true;
#endif
    float mDetectorHpHz = 0.0f;
    float mSaturation = 0.0f;
    bool mTruePeakLimit = false;
    float mAdaptiveRelease = 0.65f;
    float mStereoLink = 1.0f;
    float mLowProtect = 0.5f;
    float mTruePeakMarginDb = 0.2f;
    float mGrLimitDb = 12.0f;
    float mPunchProtect = 0.25f;
    float mReleaseShape = 0.5f;
    LimiterStyle mLimiterStyle = LimiterStyle::Transparent;
    float mHfGuard = 0.0f;
    float mDetectorTilt = 0.0f;
    bool mDeltaListen = false;
    DeltaSource mDeltaSource = DeltaSource::All;
    float mFinalClip = 0.0f;
    SaturationStyle mSaturationStyle = SaturationStyle::Clean;
    float mSaturationTone = 0.0f;
    float mSaturationDensity = 0.35f;
    float mBassSafe = 0.35f;
    float mBassRecover = 0.0f;
    float mSmoothedThresholdDb = -6.0f;
    float mSmoothedCeilingDb = -0.1f;
    float mSmoothedTone = 0.0f;
    float mSmoothedSaturation = 0.0f;
    float mSmoothedFinalClip = 0.0f;
    float mSmoothedHfGuard = 0.0f;
    float mSharedStereoLoudnessDb = 0.0f;
    bool mBankLayoutDirty = true;
    bool mPreviousBlockWasSilent = false;
    Mode mCachedLayoutMode = Mode::EDM;
    ToneStyle mCachedLayoutToneStyle = ToneStyle::Clean;
    float mCachedLayoutTone = 999.0f;
    float mCachedLayoutQuality = -1.0f;
    float mCachedLayoutAttackMs = -1.0f;
    float mCachedLayoutReleaseMs = -1.0f;
    float mCachedLayoutLookaheadMs = -1.0f;
    size_t mCachedLayoutLimiterBands = 0;
    size_t mCachedLayoutSpectralBins = 0;

    std::array<float, limiterCrossoverCount> mLimiterCrossoverCoefficients{};
    std::array<float, limiterBandCount> mLimiterCenterFrequencies{};
    std::array<BandSettings, limiterBandCount> mLimiterBandSettings{};
    std::array<float, limiterBandCount> mLimiterTargetOffsets{};
    std::array<float, limiterBandCount> mLimiterFocusWeights{};
    std::array<float, limiterBandCount> mLimiterLowBandWeights{};
    std::array<float, limiterBandCount> mLimiterHighTransientWeights{};
    std::array<float, limiterBandCount> mLimiterInverseRatios{};
    std::array<float, limiterBandCount> mLimiterAttackSlowCoefficients{};
    std::array<float, limiterBandCount> mLimiterAttackFastCoefficients{};
    std::array<float, limiterBandCount> mLimiterReleaseFastCoefficients{};
    std::array<float, limiterBandCount> mLimiterReleaseSlowCoefficients{};
    std::array<float, limiterBandCount> mLimiterTransientFastCoefficients{};
    std::array<float, limiterBandCount> mLimiterTransientSlowCoefficients{};
    std::array<float, spectralCrossoverCount> mSpectralCrossoverCoefficients{};
    std::array<float, spectralBinCount> mSpectralCenterFrequencies{};
    std::array<float, spectralBinCount> mSpectralTargetOffsets{};
    std::array<float, spectralBinCount> mSpectralToneGains{};
    std::array<float, spectralBinCount> mSpectralRatios{};
    std::array<float, spectralBinCount> mSpectralFastInverseRatios{};
    std::array<float, spectralBinCount> mSpectralSlowInverseRatios{};
    std::array<float, spectralBinCount> mSpectralHighGuardWeights{};
    std::array<float, spectralBinCount> mSpectralLowLockWeights{};
    std::array<float, spectralBinCount> mSpectralMidDensityWeights{};
    std::array<float, spectralBinCount> mSpectralCenterFrequencySquares{};
    float mSpectralAttackCoefficient = 0.0f;
    float mSpectralReleaseCoefficient = 0.0f;
    float mSpectralFastAttackCoefficient = 0.0f;
    float mSpectralFastReleaseCoefficient = 0.0f;
    float mSpectralSlowAttackCoefficient = 0.0f;
    float mSpectralSlowReleaseCoefficient = 0.0f;
    float mLimiterKneeDb = 0.0f;
    float mLimiterInverseTwoKneeDb = 0.0f;
    float mSpectralFastKneeDb = 0.0f;
    float mSpectralFastInverseTwoKneeDb = 0.0f;
    float mSpectralSlowKneeDb = 0.0f;
    float mSpectralSlowInverseTwoKneeDb = 0.0f;

    std::array<ChannelState, 2> mChannelStates;
    std::atomic<float> mClipAmountDb{0.0f};
    std::atomic<float> mGainReductionDb{0.0f};
};

}  // namespace pe::processor

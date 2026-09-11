#include "SpectralMaximizer.h"

// allow: SIZE_OK - The real-time DSP state machine stays colocated for callback locality.

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace pe::processor {
namespace {
float constexpr gMinDb = -120.0f;
float constexpr gLimiterMinHz = 28.0f;
float constexpr gLimiterMaxHz = 18000.0f;
float constexpr gSpectralMinHz = 24.0f;
float constexpr gSpectralMaxHz = 20000.0f;
float constexpr gLn2 = 0.6931471805599453f;
size_t constexpr gConversionTableBits = 11;
size_t constexpr gConversionTableSize = size_t{1} << gConversionTableBits;
size_t constexpr gNonlinearTableSize = 2048;
float constexpr gNonlinearTableMax = 12.0f;
float constexpr gAtanTableMax = 32.0f;

struct ConversionTables {
    std::array<float, gConversionTableSize + 1> exp2Fraction{};
    std::array<float, gConversionTableSize + 1> log2Mantissa{};
    std::array<float, gNonlinearTableSize + 1> tanhMagnitude{};
    std::array<float, gNonlinearTableSize + 1> atanMagnitude{};
    std::array<float, gNonlinearTableSize + 1> logCoshMagnitude{};

    ConversionTables() {
        for (size_t index = 0; index <= gConversionTableSize; ++index) {
            auto const position = static_cast<float>(index) / static_cast<float>(gConversionTableSize);
            exp2Fraction[index] = std::exp2(position);
            log2Mantissa[index] = std::log2(1.0f + position);
        }
        for (size_t index = 0; index <= gNonlinearTableSize; ++index) {
            auto const position = static_cast<float>(index) / static_cast<float>(gNonlinearTableSize);
            auto const nonlinearInput = position * gNonlinearTableMax;
            tanhMagnitude[index] = std::tanh(nonlinearInput);
            logCoshMagnitude[index] = nonlinearInput + std::log1p(std::exp(-2.0f * nonlinearInput)) - gLn2;
            atanMagnitude[index] = std::atan(position * gAtanTableMax);
        }
    }
};

ConversionTables const gConversionTables;

template <size_t TableEntries>
[[nodiscard]] float interpolateMagnitude(std::array<float, TableEntries> const& table, float magnitude, float maximum) {
    constexpr auto intervalCount = TableEntries - 1;
    auto const position = magnitude * (static_cast<float>(intervalCount) / maximum);
    auto const index = static_cast<size_t>(position);
    auto const fraction = position - static_cast<float>(index);
    auto const lower = table[index];
    return lower + ((table[index + 1] - lower) * fraction);
}

[[nodiscard]] float fastTanh(float value) {
#if defined(PEAKEATER_REFERENCE_MATH)
    return std::tanh(value);
#else
    auto const magnitude = std::abs(value);
    auto const shaped = magnitude >= gNonlinearTableMax
                            ? 1.0f
                            : interpolateMagnitude(gConversionTables.tanhMagnitude, magnitude, gNonlinearTableMax);
    return std::copysign(shaped, value);
#endif
}

[[nodiscard]] float fastAtan(float value) {
#if defined(PEAKEATER_REFERENCE_MATH)
    return std::atan(value);
#else
    auto const magnitude = std::abs(value);
    auto const shaped = magnitude >= gAtanTableMax
                            ? juce::MathConstants<float>::halfPi - (1.0f / magnitude)
                            : interpolateMagnitude(gConversionTables.atanMagnitude, magnitude, gAtanTableMax);
    return std::copysign(shaped, value);
#endif
}

[[nodiscard]] float fastLogCosh(float value) {
#if defined(PEAKEATER_REFERENCE_MATH)
    auto const magnitude = std::abs(value);
    return magnitude + std::log1p(std::exp(-2.0f * magnitude)) - gLn2;
#else
    auto const magnitude = std::abs(value);
    return magnitude >= gNonlinearTableMax
               ? magnitude - gLn2
               : interpolateMagnitude(gConversionTables.logCoshMagnitude, magnitude, gNonlinearTableMax);
#endif
}

[[nodiscard]] float fastHalfPiSin(float position) {
#if defined(PEAKEATER_REFERENCE_MATH)
    return std::sin(position * juce::MathConstants<float>::halfPi);
#else
    auto const x = position * juce::MathConstants<float>::halfPi;
    auto const x2 = x * x;
    return x * (1.0f + x2 * (-0.1666666667f + x2 * (0.0083333333f
                                                      + x2 * (-0.0001984127f + x2 * 0.0000027557f))));
#endif
}

[[nodiscard]] float logSpacedFrequency(float minHz, float maxHz, float position) {
    auto const safePosition = juce::jlimit(0.0f, 1.0f, position);
    return std::exp(std::log(minHz) + ((std::log(maxHz) - std::log(minHz)) * safePosition));
}

[[nodiscard]] float crossoverFrequency(size_t split, size_t activeCount, float minHz, float maxHz) {
    auto const position = static_cast<float>(split + 1) / static_cast<float>(std::max<size_t>(2, activeCount));
    return logSpacedFrequency(minHz, maxHz, position);
}

[[nodiscard]] float centerFrequency(size_t band, size_t activeCount, float minHz, float maxHz) {
    auto const position = (static_cast<float>(band) + 0.5f) / static_cast<float>(std::max<size_t>(1, activeCount));
    return logSpacedFrequency(minHz, maxHz, position);
}

[[nodiscard]] float multiplierToQuality(size_t multiplier, size_t maxMultiplier) {
    if (maxMultiplier <= 1 || multiplier <= 1) {
        return 0.0f;
    }

    auto const safeMultiplier = static_cast<float>(std::min(multiplier, maxMultiplier));
    return juce::jlimit(0.0f, 1.0f, std::log2(safeMultiplier) / std::log2(static_cast<float>(maxMultiplier)));
}

[[nodiscard]] float hzToBark(float frequencyHz) {
    auto const f = std::max(1.0f, frequencyHz);
    return (13.0f * std::atan(0.00076f * f)) + (3.5f * std::atan((f / 7500.0f) * (f / 7500.0f)));
}

[[nodiscard]] float logCoshStable(float x) {
    return fastLogCosh(x);
}

[[nodiscard]] float antialiasedTanh(float current, float previous, float drive) {
    auto const safeDrive = std::max(0.001f, drive);
    auto const denominator = std::max(0.000001f, fastTanh(safeDrive));
    auto const shape = [safeDrive, denominator](float x) {
        auto const limited = juce::jlimit(-32.0f, 32.0f, x);
        return fastTanh(limited * safeDrive) / denominator;
    };
    auto const antiderivative = [safeDrive, denominator](float x) {
        auto const limited = juce::jlimit(-32.0f, 32.0f, x);
        return logCoshStable(limited * safeDrive) / (safeDrive * denominator);
    };

    auto const safePrevious = std::isfinite(previous) ? previous : current;
    auto const delta = current - safePrevious;
    if (std::abs(delta) < 0.00001f) {
        return shape(current);
    }

    // DAFx ADAA principle for memoryless nonlinearities: average the antiderivative
    // across the sample interval to reduce high-frequency foldback without full oversampling.
    return juce::jlimit(-1.0f, 1.0f, (antiderivative(current) - antiderivative(safePrevious)) / delta);
}

[[nodiscard]] float normalizedAtanShape(float x, float drive) {
    auto const safeDrive = std::max(0.001f, drive);
    auto const limited = juce::jlimit(-32.0f, 32.0f, x);
    return fastAtan(limited * safeDrive) / std::max(0.000001f, fastAtan(safeDrive));
}

[[nodiscard]] float cubicDensityShape(float x, float amount) {
    auto const limited = juce::jlimit(-4.0f, 4.0f, x);
    auto const shapedAmount = juce::jlimit(0.0f, 1.0f, amount);
    return limited - ((limited * limited * limited) * 0.035f * shapedAmount);
}

[[nodiscard]] float faustPrecisionSaturator(float sample, float previous, float amount, float drive, float density, float transientProtect) {
    auto const safeAmount = juce::jlimit(0.0f, 1.0f, amount);
    auto const safeDensity = juce::jlimit(0.0f, 1.0f, density);
    auto const protect = juce::jlimit(0.0f, 1.0f, transientProtect);
    auto const precisionDrive = 1.0f + safeAmount * (2.2f + safeDensity * 1.65f) + drive * 0.12f;
    auto const aaSoft = antialiasedTanh(sample, previous, precisionDrive);
    auto const atanRounded = normalizedAtanShape(sample, 1.0f + safeAmount * (2.8f + safeDensity * 1.2f));
    auto const cubic = cubicDensityShape(sample, safeAmount * (0.45f + safeDensity * 0.55f));
    auto const blended = (aaSoft * 0.54f) + (atanRounded * 0.28f) + (cubic * 0.18f);
    auto const safeMix = safeAmount * juce::jlimit(0.10f, 1.0f, 1.0f - protect * 0.84f);
    return sample + ((blended - sample) * safeMix);
}

[[nodiscard]] float faustPrecisionClip(float normalized, float previousNormalized, float amount) {
    auto const safeAmount = juce::jlimit(0.0f, 1.0f, amount);
    auto const softDrive = 1.0f + safeAmount * 3.9f;
    auto const roundDrive = 1.0f + safeAmount * 5.2f;
    auto const aaSoft = antialiasedTanh(normalized, previousNormalized, softDrive);
    auto const atanRounded = normalizedAtanShape(normalized, roundDrive);
    auto const knee = juce::jmap(safeAmount, 0.34f, 0.13f);
    auto const magnitude = std::abs(normalized);
    auto const sign = normalized < 0.0f ? -1.0f : 1.0f;
    auto const kneeStart = std::max(0.0f, 1.0f - knee);
    auto roundedCorner = normalized;
    if (magnitude > kneeStart) {
        auto const position = juce::jlimit(0.0f, 1.0f, (magnitude - kneeStart) / std::max(knee, 0.000001f));
        auto const shaped = kneeStart + knee * fastHalfPiSin(position);
        roundedCorner = sign * std::min(shaped, 1.0f);
    }
    return (aaSoft * 0.58f) + (atanRounded * 0.27f) + (roundedCorner * 0.15f);
}

}

void SpectralMaximizer::prepare(juce::dsp::ProcessSpec const& spec) {
    mSampleRate = spec.sampleRate;
    mNumChannels = std::min<size_t>(spec.numChannels, mChannelStates.size());
    mSpectralHoldSamples = static_cast<int>(std::round(mHoldMs * 0.001f * mSampleRate));

    auto const lowCutoff = 180.0f;
    auto const highCutoff = 5200.0f;
    mLowCoefficient = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * lowCutoff / static_cast<float>(mSampleRate));
    mHighCoefficient = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * highCutoff / static_cast<float>(mSampleRate));
    mLimiterOvershootAttackCoefficient = fastSmoothingCoefficient(0.65f, mSampleRate);
    mLimiterOvershootReleaseBaseCoefficient = fastSmoothingCoefficient(38.0f, mSampleRate);
    mThresholdSmoothCoefficient = smoothingCoefficient(18.0f, mSampleRate);
    mCeilingSmoothCoefficient = smoothingCoefficient(12.0f, mSampleRate);
    mToneSmoothCoefficient = smoothingCoefficient(22.0f, mSampleRate);
    mSaturationSmoothCoefficient = smoothingCoefficient(20.0f, mSampleRate);
    mFinalClipSmoothCoefficient = smoothingCoefficient(18.0f, mSampleRate);
    mHfGuardSmoothCoefficient = smoothingCoefficient(22.0f, mSampleRate);
    mSaturationFastCoefficient = smoothingCoefficient(0.18f, mSampleRate);
    mSaturationSlowCoefficient = smoothingCoefficient(18.0f, mSampleRate);
    mFeedbackAttackCoefficient = smoothingCoefficient(28.0f, mSampleRate);
    mFeedbackReleaseCoefficient = smoothingCoefficient(76.0f, mSampleRate);
    reset();
}

void SpectralMaximizer::reset() {
    for (auto& state : mChannelStates) {
        state.detectorLow = 0.0f;
        state.detectorHighLowpass = 0.0f;
        state.detectorHpLowpass = 0.0f;
        state.signalLow = 0.0f;
        state.signalHighLowpass = 0.0f;
        state.limiterDetectorLowpass.fill(0.0f);
        state.limiterSignalLowpass.fill(0.0f);
        state.limiterEnvelope.fill(0.0f);
        state.limiterTransientFastEnvelope.fill(0.0f);
        state.limiterTransientSlowEnvelope.fill(0.0f);
        state.limiterPreviousMagnitude.fill(0.0f);
        state.limiterHoldSamples.fill(0);
        state.saturationFastEnvelope = 0.0f;
        state.saturationSlowEnvelope = 0.0f;
        state.saturationSplitLowpass = 0.0f;
        state.bassRecoverPreClipLowpass = 0.0f;
        state.bassRecoverPostClipLowpass = 0.0f;
        state.previousSaturationLowInput = 0.0f;
        state.previousSaturationHighInput = 0.0f;
        state.previousFinalClipInput = 0.0f;
        state.previousCeilingInput2 = 0.0f;
        state.previousCeilingInput = 0.0f;
        state.rmsContourPeakEnvelope = 0.0f;
        state.rmsContourEnergyEnvelope = 0.0f;
        state.rmsContourAdaptiveAmount = 0.0f;
        state.spectralDetectorLowpass.fill(0.0f);
        state.spectralSignalLowpass.fill(0.0f);
        state.spectralEnvelope.fill(0.0f);
        state.spectralFastEnvelope.fill(0.0f);
        state.spectralSlowEnvelope.fill(0.0f);
        state.spectralPreviousMagnitude.fill(0.0f);
        state.spectralGainDb.fill(0.0f);
        state.spectralLoudnessAllocation.fill(1.0f);
        state.envelope = {0.0f, 0.0f, 0.0f};
        state.transientFastEnvelope = {0.0f, 0.0f, 0.0f};
        state.transientSlowEnvelope = {0.0f, 0.0f, 0.0f};
        state.previousMagnitude = {0.0f, 0.0f, 0.0f};
        state.holdSamples = {0, 0, 0};
        state.limiterOvershootGain = 1.0f;
        state.peakReliefAllpassInput = 0.0f;
        state.peakReliefAllpassOutput = 0.0f;
        state.maskingResidual = 0.0f;
        state.maskingResidualEnvelope = 0.0f;
        state.ceilingEnvelope = 0.0f;
        state.ceilingGain = 1.0f;
        state.ceilingTargetGainWindow.fill(1.0f);
        state.ceilingTargetGainIndex = 0;
        state.peakBudgetFeedback = {};
        state.spectralUpwardAppliedDb = 0.0f;
        state.spectralTransientRisk = 0.0f;
        state.spectralLowRisk = 0.0f;
        state.spectralHighBandRatio = 0.0f;
        state.spectralBinSpread = 0.25f;
        state.spectralFlatness = 0.30f;
        state.spectralCentroidNorm = 0.0f;
        state.spectralTransientDensity = 0.0f;
        state.spectralDescriptorCountdown = 0;
        state.spectralControlPhase = 0;
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
        state.spectralDescriptorInterval = mSpectralDescriptorInterval;
        state.spectralDescriptorStableUpdates = 0;
        state.spectralDescriptorEventCooldown = 0;
        state.spectralDescriptorInputEnvelope = 0.0f;
#endif
    }
#if defined(PEAKEATER_CPU_BENCHMARK)
    mSpectralDescriptorUpdateCount = 0;
    mSpectralDescriptorEventUpdateCount = 0;
    mPerceptualAllocatorUpdateCount = 0;
#endif
    mSmoothedThresholdDb = mThresholdDb;
    mSmoothedCeilingDb = mCeilingDb;
    mSmoothedTone = mTone;
    mSmoothedSaturation = mSaturation;
    mSmoothedFinalClip = mFinalClip;
    mSmoothedHfGuard = mHfGuard;
    mSharedStereoLoudnessDb = 0.0f;
    mBankLayoutDirty = true;
    mClipAmountDb = 0.0f;
    mGainReductionDb = 0.0f;
}

void SpectralMaximizer::setMode(Mode mode) {
    mBankLayoutDirty = mBankLayoutDirty || mMode != mode;
    mMode = mode;
    mPercussiveMode = mMode == Mode::Drums || mMode == Mode::OneShot || mMode == Mode::OneShotClean
                      || mMode == Mode::Percs || mMode == Mode::DrumNBass;
}

void SpectralMaximizer::setThreshold(float thresholdDb) {
    mThresholdDb = thresholdDb;
}

void SpectralMaximizer::setCeiling(float ceilingDb) {
    mCeilingDb = ceilingDb;
}

void SpectralMaximizer::setTone(float tone) {
    mTone = juce::jlimit(-1.0f, 1.0f, tone);
}

void SpectralMaximizer::setToneStyle(ToneStyle toneStyle) {
    mBankLayoutDirty = mBankLayoutDirty || mToneStyle != toneStyle;
    mToneStyle = toneStyle;
}

void SpectralMaximizer::setAttack(float attackMs) {
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    auto const next = juce::jlimit(0.01f, 1000.0f, attackMs);
#else
    auto const next = juce::jlimit(0.01f, 200.0f, attackMs);
#endif
    mBankLayoutDirty = mBankLayoutDirty || std::abs(mAttackMs - next) > 0.001f;
    mAttackMs = next;
}

void SpectralMaximizer::setHold(float holdMs) {
    auto const value = juce::jlimit(0.0f, 500.0f, holdMs);
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    auto const normalized = value / 500.0f;
    mHoldMs = value * (1.22f - (normalized * 0.22f));
#else
    mHoldMs = value;
#endif
    mSpectralHoldSamples = static_cast<int>(std::round(mHoldMs * 0.001f * mSampleRate));
}

void SpectralMaximizer::setRelease(float releaseMs) {
    auto const next = juce::jlimit(1.0f, 2000.0f, releaseMs);
    mBankLayoutDirty = mBankLayoutDirty || std::abs(mReleaseMs - next) > 0.001f;
    mReleaseMs = next;
}

void SpectralMaximizer::setTransientRecovery(float recovery) {
    auto const value = juce::jlimit(0.0f, 1.0f, recovery);
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    mTransientRecovery = value * (1.32f - (value * 0.32f));
#else
    mTransientRecovery = value;
#endif
}

void SpectralMaximizer::setLookahead(float lookaheadMs) {
    auto const next = juce::jlimit(0.0f, 50.0f, lookaheadMs);
    mBankLayoutDirty = mBankLayoutDirty || std::abs(mLookaheadMs - next) > 0.001f;
    mLookaheadMs = next;
}

void SpectralMaximizer::setBandMultiplier(size_t multiplier) {
    auto const next = juce::jlimit<size_t>(1, maxRequestedBandMultiplier, multiplier);
    mBankLayoutDirty = mBankLayoutDirty || mBandMultiplier != next;
    mBandMultiplier = next;
    mSpectralDescriptorInterval = mBandMultiplier == 1 ? size_t{24}
                                : mBandMultiplier == 2 ? size_t{12}
                                : mBandMultiplier <= 4 ? size_t{4}
                                                       : size_t{2};
}

void SpectralMaximizer::setDetectorHp(float detectorHpHz) {
    mDetectorHpHz = juce::jlimit(0.0f, 250.0f, detectorHpHz);
}

void SpectralMaximizer::setSaturation(float saturation) {
    auto const value = juce::jlimit(0.0f, 1.0f, saturation);
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    mSaturation = value * (1.20f - (value * 0.20f));
#else
    mSaturation = value;
#endif
}

void SpectralMaximizer::setTruePeakLimit(bool shouldLimitTruePeak) {
    mTruePeakLimit = shouldLimitTruePeak;
}

void SpectralMaximizer::setAdaptiveRelease(float adaptiveRelease) {
    mAdaptiveRelease = juce::jlimit(0.0f, 1.0f, adaptiveRelease);
}

void SpectralMaximizer::setStereoLink(float stereoLink) {
    mStereoLink = juce::jlimit(0.0f, 1.0f, stereoLink);
}

void SpectralMaximizer::setLowProtect(float lowProtect) {
    mLowProtect = juce::jlimit(0.0f, 1.0f, lowProtect);
}

void SpectralMaximizer::setTruePeakMargin(float truePeakMarginDb) {
    mTruePeakMarginDb = juce::jlimit(0.0f, 1.0f, truePeakMarginDb);
}

void SpectralMaximizer::setGrLimit(float grLimitDb) {
    mGrLimitDb = juce::jlimit(0.0f, 12.0f, grLimitDb);
}

void SpectralMaximizer::setPunchProtect(float punchProtect) {
    mPunchProtect = juce::jlimit(0.0f, 1.0f, punchProtect);
}

void SpectralMaximizer::setReleaseShape(float releaseShape) {
    mReleaseShape = juce::jlimit(0.0f, 1.0f, releaseShape);
}

void SpectralMaximizer::setLimiterStyle(LimiterStyle limiterStyle) {
    mLimiterStyle = limiterStyle;
}

void SpectralMaximizer::setHfGuard(float hfGuard) {
    mHfGuard = juce::jlimit(0.0f, 1.0f, hfGuard);
}

void SpectralMaximizer::setDetectorTilt(float detectorTilt) {
    mDetectorTilt = juce::jlimit(-1.0f, 1.0f, detectorTilt);
}

void SpectralMaximizer::setDeltaListen(bool shouldListenDelta) {
    mDeltaListen = shouldListenDelta;
}

void SpectralMaximizer::setDeltaSource(DeltaSource deltaSource) {
    mDeltaSource = deltaSource;
}

void SpectralMaximizer::setFinalClip(float finalClip) {
    mFinalClip = juce::jlimit(0.0f, 1.0f, finalClip);
}

void SpectralMaximizer::setSaturationStyle(SaturationStyle saturationStyle) {
    mSaturationStyle = saturationStyle;
}

void SpectralMaximizer::setSaturationTone(float saturationTone) {
    auto const value = juce::jlimit(-1.0f, 1.0f, saturationTone);
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    auto const magnitude = std::abs(value);
    mSaturationTone = std::copysign(magnitude * (1.34f - (magnitude * 0.34f)), value);
#else
    mSaturationTone = value;
#endif
}

void SpectralMaximizer::setSaturationDensity(float saturationDensity) {
    auto const value = juce::jlimit(0.0f, 1.0f, saturationDensity);
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    if (value <= 0.35f) {
        auto const normalized = value / 0.35f;
        mSaturationDensity = value * (0.82f + (normalized * 0.18f));
    } else {
        auto const normalized = (value - 0.35f) / 0.65f;
        mSaturationDensity = 0.35f + ((value - 0.35f) * (1.22f - (normalized * 0.22f)));
    }
#else
    mSaturationDensity = value;
#endif
}

void SpectralMaximizer::setBassSafe(float bassSafe) {
    auto const value = juce::jlimit(0.0f, 1.0f, bassSafe);
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    if (value <= 0.35f) {
        auto const normalized = value / 0.35f;
        mBassSafe = value * (0.84f + (normalized * 0.16f));
    } else {
        auto const normalized = (value - 0.35f) / 0.65f;
        mBassSafe = 0.35f + ((value - 0.35f) * (1.18f - (normalized * 0.18f)));
    }
#else
    mBassSafe = value;
#endif
}

void SpectralMaximizer::setBassRecover(float bassRecover) {
    mBassRecover = juce::jlimit(0.0f, 1.0f, bassRecover);
}

void SpectralMaximizer::process(juce::dsp::ProcessContextReplacing<float> const& context) {
    juce::ScopedNoDenormals noDenormals;
    auto block = context.getOutputBlock();
    auto const numSamples = block.getNumSamples();
    auto const numChannels = std::min<size_t>(block.getNumChannels(), mNumChannels);
    if (numSamples == 0 || numChannels == 0 || mSampleRate <= 0.0) {
        mClipAmountDb = 0.0f;
        mGainReductionDb = 0.0f;
        return;
    }

    if (mPreviousBlockWasSilent) {
        auto silentBlock = true;
        for (size_t channel = 0; channel < numChannels && silentBlock; ++channel) {
            auto const* samples = block.getChannelPointer(channel);
            for (size_t sample = 0; sample < numSamples; ++sample) {
                auto const value = samples[sample];
                if (!std::isfinite(value) || std::abs(value) > 1.0e-8f) {
                    silentBlock = false;
                    break;
                }
            }
        }

        if (silentBlock) {
            mClipAmountDb = 0.0f;
            mGainReductionDb = 0.0f;
            return;
        }
    }

    mPreviousBlockWasSilent = false;

    auto blockPeak = 0.0f;

    auto const style = getStyleSettings(mLimiterStyle);
    auto const bandMultiplier = juce::jlimit<size_t>(1, maxRequestedBandMultiplier, mBandMultiplier);
    auto const realtimeBandMultiplier = std::min(bandMultiplier, maxRealtimeBandMultiplier);
    auto const quality = multiplierToQuality(bandMultiplier, maxRequestedBandMultiplier);
    auto const qualitySafety = bandMultiplier >= 32 ? 0.72f
                             : bandMultiplier >= 16 ? 0.56f
                             : bandMultiplier >= 8 ? 0.40f
                             : bandMultiplier >= 4 ? 0.22f
                                                   : 0.0f;
    auto const masteringQuality = bandMultiplier >= 8 ? juce::jlimit(0.0f, 1.0f, (static_cast<float>(bandMultiplier) - 4.0f) / 28.0f)
                                                       : 0.0f;
    auto const ultraRenderQuality = bandMultiplier >= 16 ? juce::jlimit(0.0f, 1.0f, (static_cast<float>(bandMultiplier) - 8.0f) / 24.0f)
                                                         : 0.0f;
    auto const maxRenderQuality = bandMultiplier >= 32 ? 1.0f : 0.0f;
    auto const mode = qualityAdjustedMode(mMode, getModeSettings(mMode), quality, bandMultiplier);
    auto const detectorHpHz = mDetectorHpHz > 1.0f ? mDetectorHpHz : mode.detectorHpHz;
    auto const detectorHpCoefficient = onePoleCoefficient(detectorHpHz, mSampleRate);
    auto const saturationSplitCoefficient = onePoleCoefficient(430.0f, mSampleRate);
    auto const activeLimiterBands = bandMultiplier == 1 ? size_t{6}
                                 : bandMultiplier == 2 ? size_t{8}
                                 : bandMultiplier <= 4 ? limiterBaseBandCount
                                                       : activeBandCount(limiterBaseBandCount, realtimeBandMultiplier, limiterBandCount);
    auto const activeSpectralBins = bandMultiplier == 1 ? size_t{14}
                                  : bandMultiplier == 2 ? size_t{17}
                                  : bandMultiplier <= 4 ? size_t{28}
                                                        : activeBandCount(spectralBaseBinCount, realtimeBandMultiplier, spectralBinCount);
    auto const spectralDepth = juce::jlimit(0.54f, 0.80f, 0.62f + (quality * 0.030f) + (qualitySafety * 0.012f));
    auto const toneLayoutChanged = std::abs(mCachedLayoutTone - mSmoothedTone) > 0.018f;
    auto const layoutDirty = mBankLayoutDirty || mCachedLayoutMode != mMode || mCachedLayoutToneStyle != mToneStyle
                             || toneLayoutChanged || std::abs(mCachedLayoutQuality - quality) > 0.0001f
                             || std::abs(mCachedLayoutAttackMs - mAttackMs) > 0.001f
                             || std::abs(mCachedLayoutReleaseMs - mReleaseMs) > 0.001f
                             || std::abs(mCachedLayoutLookaheadMs - mLookaheadMs) > 0.001f
                             || mCachedLayoutLimiterBands != activeLimiterBands
                             || mCachedLayoutSpectralBins != activeSpectralBins;
    if (layoutDirty) {
        updateBankLayout(mode, quality, activeLimiterBands, activeSpectralBins);
        mCachedLayoutMode = mMode;
        mCachedLayoutToneStyle = mToneStyle;
        mCachedLayoutTone = mSmoothedTone;
        mCachedLayoutQuality = quality;
        mCachedLayoutAttackMs = mAttackMs;
        mCachedLayoutReleaseMs = mReleaseMs;
        mCachedLayoutLookaheadMs = mLookaheadMs;
        mCachedLayoutLimiterBands = activeLimiterBands;
        mCachedLayoutSpectralBins = activeSpectralBins;
        mBankLayoutDirty = false;
    }
    auto driveFinalClipRelief = 0.14f;
    auto driveSpectralBoost = 0.055f;
    auto driveHfGuardBoost = 0.025f;
    auto driveLowProtectBoost = 0.035f;
    auto driveMarginBoost = 0.05f;
    auto driveAdaptiveBoost = 0.06f;
    auto driveReleaseBoost = 0.05f;
    auto driveGrLimitBoost = 0.28f;
    auto driveSaturationRelief = 0.06f;
    auto drivePunchBoost = 0.035f;
    auto typeDensityBias = 0.58f;
    auto typeTransientReserve = 0.42f;
    auto typeLowReserve = 0.34f;
    auto typeClipKeep = 0.66f;
    switch (mMode) {
        case Mode::Clean:
            driveFinalClipRelief = 0.38f;
            driveSpectralBoost = 0.055f;
            driveHfGuardBoost = 0.025f;
            driveLowProtectBoost = 0.035f;
            driveMarginBoost = 0.075f;
            driveAdaptiveBoost = 0.09f;
            driveReleaseBoost = 0.09f;
            driveGrLimitBoost = 0.46f;
            driveSaturationRelief = 0.18f;
            drivePunchBoost = 0.015f;
            typeDensityBias = 0.18f;
            typeTransientReserve = 0.58f;
            typeLowReserve = 0.36f;
            typeClipKeep = 0.16f;
            break;
        case Mode::Acoustic:
            driveFinalClipRelief = 0.34f;
            driveSpectralBoost = 0.045f;
            driveHfGuardBoost = 0.025f;
            driveLowProtectBoost = 0.035f;
            driveMarginBoost = 0.085f;
            driveAdaptiveBoost = 0.11f;
            driveReleaseBoost = 0.12f;
            driveGrLimitBoost = 0.42f;
            driveSaturationRelief = 0.16f;
            drivePunchBoost = 0.02f;
            typeDensityBias = 0.24f;
            typeTransientReserve = 0.66f;
            typeLowReserve = 0.38f;
            typeClipKeep = 0.20f;
            break;
        case Mode::Vocal:
            driveFinalClipRelief = 0.30f;
            driveSpectralBoost = 0.060f;
            driveHfGuardBoost = 0.045f;
            driveLowProtectBoost = 0.035f;
            driveMarginBoost = 0.075f;
            driveAdaptiveBoost = 0.10f;
            driveReleaseBoost = 0.10f;
            driveGrLimitBoost = 0.44f;
            driveSaturationRelief = 0.14f;
            drivePunchBoost = 0.025f;
            typeDensityBias = 0.30f;
            typeTransientReserve = 0.62f;
            typeLowReserve = 0.34f;
            typeClipKeep = 0.22f;
            break;
        case Mode::HipHop:
            driveFinalClipRelief = 0.17f;
            driveSpectralBoost = 0.055f;
            driveHfGuardBoost = 0.025f;
            driveLowProtectBoost = 0.07f;
            driveMarginBoost = 0.055f;
            driveAdaptiveBoost = 0.07f;
            driveReleaseBoost = 0.055f;
            driveGrLimitBoost = 0.38f;
            driveSaturationRelief = 0.07f;
            drivePunchBoost = 0.045f;
            typeDensityBias = 0.58f;
            typeTransientReserve = 0.48f;
            typeLowReserve = 0.68f;
            typeClipKeep = 0.62f;
            break;
        case Mode::Bass:
            driveFinalClipRelief = 0.20f;
            driveSpectralBoost = 0.045f;
            driveHfGuardBoost = 0.018f;
            driveLowProtectBoost = 0.105f;
            driveMarginBoost = 0.06f;
            driveAdaptiveBoost = 0.08f;
            driveReleaseBoost = 0.07f;
            driveGrLimitBoost = 0.40f;
            driveSaturationRelief = 0.08f;
            drivePunchBoost = 0.03f;
            typeDensityBias = 0.38f;
            typeTransientReserve = 0.50f;
            typeLowReserve = 0.86f;
            typeClipKeep = 0.40f;
            break;
        case Mode::Trap:
            driveFinalClipRelief = 0.18f;
            driveSpectralBoost = 0.058f;
            driveHfGuardBoost = 0.03f;
            driveLowProtectBoost = 0.095f;
            driveMarginBoost = 0.070f;
            driveAdaptiveBoost = 0.095f;
            driveReleaseBoost = 0.072f;
            driveGrLimitBoost = 0.44f;
            driveSaturationRelief = 0.09f;
            drivePunchBoost = 0.07f;
            typeDensityBias = 0.70f;
            typeTransientReserve = 0.64f;
            typeLowReserve = 0.82f;
            typeClipKeep = 0.62f;
            break;
        case Mode::Kick808:
            driveFinalClipRelief = 0.22f;
            driveSpectralBoost = 0.035f;
            driveHfGuardBoost = 0.014f;
            driveLowProtectBoost = 0.13f;
            driveMarginBoost = 0.07f;
            driveAdaptiveBoost = 0.09f;
            driveReleaseBoost = 0.08f;
            driveGrLimitBoost = 0.42f;
            driveSaturationRelief = 0.085f;
            drivePunchBoost = 0.025f;
            typeDensityBias = 0.26f;
            typeTransientReserve = 0.56f;
            typeLowReserve = 0.98f;
            typeClipKeep = 0.32f;
            break;
        case Mode::Drums:
            driveFinalClipRelief = 0.10f;
            driveSpectralBoost = 0.04f;
            driveHfGuardBoost = 0.025f;
            driveLowProtectBoost = 0.03f;
            driveMarginBoost = 0.045f;
            driveAdaptiveBoost = 0.045f;
            driveReleaseBoost = 0.03f;
            driveGrLimitBoost = 0.28f;
            driveSaturationRelief = 0.04f;
            drivePunchBoost = 0.16f;
            typeDensityBias = 0.42f;
            typeTransientReserve = 0.92f;
            typeLowReserve = 0.34f;
            typeClipKeep = 0.74f;
            break;
        case Mode::OneShot:
            driveFinalClipRelief = 0.085f;
            driveSpectralBoost = 0.035f;
            driveHfGuardBoost = 0.02f;
            driveLowProtectBoost = 0.025f;
            driveMarginBoost = 0.04f;
            driveAdaptiveBoost = 0.035f;
            driveReleaseBoost = 0.025f;
            driveGrLimitBoost = 0.24f;
            driveSaturationRelief = 0.035f;
            drivePunchBoost = 0.18f;
            typeDensityBias = 0.28f;
            typeTransientReserve = 0.98f;
            typeLowReserve = 0.26f;
            typeClipKeep = 0.76f;
            break;
        case Mode::OneShotClean:
            driveFinalClipRelief = 0.30f;
            driveSpectralBoost = 0.032f;
            driveHfGuardBoost = 0.018f;
            driveLowProtectBoost = 0.028f;
            driveMarginBoost = 0.065f;
            driveAdaptiveBoost = 0.072f;
            driveReleaseBoost = 0.052f;
            driveGrLimitBoost = 0.32f;
            driveSaturationRelief = 0.15f;
            drivePunchBoost = 0.18f;
            typeDensityBias = 0.20f;
            typeTransientReserve = 0.99f;
            typeLowReserve = 0.30f;
            typeClipKeep = 0.26f;
            break;
        case Mode::Percs:
            driveFinalClipRelief = 0.09f;
            driveSpectralBoost = 0.045f;
            driveHfGuardBoost = 0.035f;
            driveLowProtectBoost = 0.025f;
            driveMarginBoost = 0.04f;
            driveAdaptiveBoost = 0.04f;
            driveReleaseBoost = 0.025f;
            driveGrLimitBoost = 0.26f;
            driveSaturationRelief = 0.035f;
            drivePunchBoost = 0.16f;
            typeDensityBias = 0.36f;
            typeTransientReserve = 0.96f;
            typeLowReserve = 0.24f;
            typeClipKeep = 0.72f;
            break;
        case Mode::DrumNBass:
            driveFinalClipRelief = 0.17f;
            driveSpectralBoost = 0.052f;
            driveHfGuardBoost = 0.025f;
            driveLowProtectBoost = 0.05f;
            driveMarginBoost = 0.060f;
            driveAdaptiveBoost = 0.060f;
            driveReleaseBoost = 0.048f;
            driveGrLimitBoost = 0.36f;
            driveSaturationRelief = 0.060f;
            drivePunchBoost = 0.14f;
            typeDensityBias = 0.74f;
            typeTransientReserve = 0.88f;
            typeLowReserve = 0.60f;
            typeClipKeep = 0.66f;
            break;
        case Mode::Dubstep:
            driveFinalClipRelief = 0.19f;
            driveSpectralBoost = 0.058f;
            driveHfGuardBoost = 0.035f;
            driveLowProtectBoost = 0.09f;
            driveMarginBoost = 0.072f;
            driveAdaptiveBoost = 0.083f;
            driveReleaseBoost = 0.058f;
            driveGrLimitBoost = 0.43f;
            driveSaturationRelief = 0.082f;
            drivePunchBoost = 0.08f;
            typeDensityBias = 0.78f;
            typeTransientReserve = 0.70f;
            typeLowReserve = 0.84f;
            typeClipKeep = 0.64f;
            break;
        case Mode::House:
            driveFinalClipRelief = 0.14f;
            driveSpectralBoost = 0.045f;
            driveHfGuardBoost = 0.025f;
            driveLowProtectBoost = 0.06f;
            driveMarginBoost = 0.05f;
            driveAdaptiveBoost = 0.06f;
            driveReleaseBoost = 0.055f;
            driveGrLimitBoost = 0.32f;
            driveSaturationRelief = 0.06f;
            drivePunchBoost = 0.07f;
            typeDensityBias = 0.66f;
            typeTransientReserve = 0.60f;
            typeLowReserve = 0.62f;
            typeClipKeep = 0.68f;
            break;
        case Mode::Bright:
            driveFinalClipRelief = 0.28f;
            driveSpectralBoost = 0.055f;
            driveHfGuardBoost = 0.055f;
            driveLowProtectBoost = 0.025f;
            driveMarginBoost = 0.075f;
            driveAdaptiveBoost = 0.08f;
            driveReleaseBoost = 0.08f;
            driveGrLimitBoost = 0.40f;
            driveSaturationRelief = 0.12f;
            drivePunchBoost = 0.025f;
            typeDensityBias = 0.44f;
            typeTransientReserve = 0.58f;
            typeLowReserve = 0.24f;
            typeClipKeep = 0.28f;
            break;
        case Mode::Glue:
            driveFinalClipRelief = 0.22f;
            driveSpectralBoost = 0.045f;
            driveHfGuardBoost = 0.025f;
            driveLowProtectBoost = 0.045f;
            driveMarginBoost = 0.055f;
            driveAdaptiveBoost = 0.09f;
            driveReleaseBoost = 0.11f;
            driveGrLimitBoost = 0.34f;
            driveSaturationRelief = 0.09f;
            drivePunchBoost = 0.025f;
            typeDensityBias = 0.54f;
            typeTransientReserve = 0.54f;
            typeLowReserve = 0.44f;
            typeClipKeep = 0.36f;
            break;
        case Mode::EDM:
            driveFinalClipRelief = 0.18f;
            driveSpectralBoost = 0.064f;
            driveHfGuardBoost = 0.030f;
            driveLowProtectBoost = 0.046f;
            driveMarginBoost = 0.066f;
            driveAdaptiveBoost = 0.076f;
            driveReleaseBoost = 0.062f;
            driveGrLimitBoost = 0.36f;
            driveSaturationRelief = 0.080f;
            drivePunchBoost = 0.052f;
            typeDensityBias = 0.64f;
            typeTransientReserve = 0.56f;
            typeLowReserve = 0.46f;
            typeClipKeep = 0.58f;
            break;
        default:
            break;
    }
#if defined(PEAKEATER_SPECTRAL_2_VARIANT)
    driveFinalClipRelief *= 1.70f;
    driveSaturationRelief *= 2.25f;
    typeClipKeep *= 0.82f;
#endif
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    switch (mMode) {
        case Mode::Drums:
        case Mode::OneShot:
        case Mode::OneShotClean:
        case Mode::Percs:
            typeTransientReserve = juce::jlimit(0.0f, 1.0f, typeTransientReserve + 0.045f);
            typeDensityBias *= 0.92f;
            drivePunchBoost *= 1.16f;
            driveSpectralBoost *= 0.92f;
            break;
        case Mode::DrumNBass:
            typeTransientReserve = juce::jlimit(0.0f, 1.0f, typeTransientReserve + 0.035f);
            typeDensityBias *= 1.03f;
            drivePunchBoost *= 1.10f;
            driveHfGuardBoost *= 1.08f;
            break;
        case Mode::Dubstep:
        case Mode::Trap:
        case Mode::HipHop:
        case Mode::Bass:
        case Mode::Kick808:
            typeLowReserve = juce::jlimit(0.0f, 1.0f, typeLowReserve + 0.045f);
            driveLowProtectBoost *= 1.10f;
            driveSpectralBoost *= 0.96f;
            break;
        case Mode::Bright:
        case Mode::Vocal:
            driveHfGuardBoost *= 1.08f;
            driveSpectralBoost *= 0.90f;
            typeClipKeep *= 0.94f;
            break;
        default:
            break;
    }
#endif
    auto const baseEffectivePunchProtect = juce::jlimit(0.0f, 1.0f, mPunchProtect + style.punchOffset + (qualitySafety * 0.035f));
    auto const baseEffectiveReleaseShape =
        juce::jlimit(0.0f, 1.0f, mReleaseShape + style.releaseOffset + (qualitySafety * 0.04f) + (masteringQuality * 0.035f));
    auto const baseEffectiveAdaptiveRelease =
        juce::jlimit(0.0f, 1.0f, mAdaptiveRelease + (qualitySafety * 0.035f) + (masteringQuality * 0.05f));
    auto const baseEffectiveGrLimitDb =
        juce::jlimit(0.0f, 18.0f, (mGrLimitDb * style.grLimitScale) + (qualitySafety * 0.45f) + (masteringQuality * 0.55f));
    auto const baseEffectiveTruePeakMarginDb =
        juce::jlimit(0.0f, 1.0f, mTruePeakMarginDb + (qualitySafety * 0.055f) + (masteringQuality * 0.10f));
    auto const nonlinearQuality = juce::jlimit(0.0f, 1.0f, quality * 1.35f);
    auto const thresholdSmooth = mThresholdSmoothCoefficient;
    auto const ceilingSmooth = mCeilingSmoothCoefficient;
    auto const toneSmooth = mToneSmoothCoefficient;
    auto const saturationSmooth = mSaturationSmoothCoefficient;
    auto const finalClipSmooth = mFinalClipSmoothCoefficient;
    auto const hfGuardSmooth = mHfGuardSmoothCoefficient;
    auto const saturationFast = mSaturationFastCoefficient;
    auto const saturationSlow = mSaturationSlowCoefficient;
    auto const useNonlinearInterpolation = nonlinearQuality > 0.0001f && bandMultiplier >= 8;
    auto const hqNonlinearInterpolation = useNonlinearInterpolation && bandMultiplier >= 8;
    auto const deltaListening = mDeltaListen;
    auto const typeSaturation = mode.softClipAmount * mode.densityLift * 0.055f;
    auto const saturationSettings = getSaturationSettings(mSaturationStyle);
    auto const satTone = juce::jlimit(-1.0f, 1.0f, mSaturationTone);
    auto const bassSafe = juce::jlimit(0.0f, 1.0f, mBassSafe);
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    auto const bassSafeEmphasis = juce::jlimit(0.0f, 1.0f, (bassSafe - 0.35f) / 0.65f);
#else
    constexpr auto bassSafeEmphasis = 0.0f;
#endif
    auto const bassRecover = juce::jlimit(0.0f, 1.0f, mBassRecover);
    auto const baseLowProtectAmount = juce::jlimit(0.0f, 1.0f, mLowProtect + (qualitySafety * 0.025f) + (masteringQuality * 0.035f));
    auto const detectorTilt = juce::jlimit(-1.0f, 1.0f, mDetectorTilt);
    auto const highTilt = std::max(0.0f, -detectorTilt);
    auto const lowTilt = std::max(0.0f, detectorTilt);
    auto const cleanSafetyBias = mMode == Mode::Clean || mMode == Mode::Acoustic || mMode == Mode::Vocal ? 1.18f : 1.0f;
    auto const loudTypeClipKeep = mMode == Mode::EDM || mMode == Mode::Dubstep || mMode == Mode::DrumNBass
                                  || mMode == Mode::House || mMode == Mode::Trap
                                      ? 0.60f
                                      : 1.0f;
    auto const brightSafety = mMode == Mode::Bright || mMode == Mode::Vocal ? 0.025f
                            : mMode == Mode::DrumNBass || mMode == Mode::Percs ? 0.012f
                                                                                : 0.0f;
    auto const typeTransientPriority =
        (mMode == Mode::Drums || mMode == Mode::OneShot || mMode == Mode::OneShotClean || mMode == Mode::Percs
         || mMode == Mode::DrumNBass)
            ? 1.0f
      : (mMode == Mode::EDM || mMode == Mode::House || mMode == Mode::Dubstep || mMode == Mode::Trap) ? 0.55f
                                                                                                      : 0.25f;
    auto const drumsDriveMicroGr = mMode == Mode::Drums ? 0.08f : 0.0f;
    auto const typeSaturationRelief = mMode == Mode::OneShot || mMode == Mode::OneShotClean ? 0.70f : 1.0f;
    auto const effectiveDriveSaturationRelief = driveSaturationRelief + (typeSaturationRelief * 0.50f);
    auto const lookaheadAmount = juce::jlimit(0.0f, 1.0f, mLookaheadMs / 50.0f);
    auto const lookaheadPrecision = hermiteShape(lookaheadAmount);
    auto const limiterHoldSamples = mSpectralHoldSamples;
    auto const driveQualityScale = juce::jlimit(0.20f, 0.88f, 0.30f + (qualitySafety * 0.44f) + (masteringQuality * 0.16f));
    auto const cleanLiftTransparentMode =
        mMode == Mode::Clean || mMode == Mode::Acoustic || mMode == Mode::Vocal || mMode == Mode::OneShotClean;
    auto const loudLiftMode = mMode == Mode::EDM || mMode == Mode::Dubstep || mMode == Mode::DrumNBass
                              || mMode == Mode::House || mMode == Mode::Trap;
    auto const bassLiftMode = mMode == Mode::HipHop || mMode == Mode::Bass || mMode == Mode::Kick808;
    auto const percussiveLiftMode =
        mMode == Mode::Drums || mMode == Mode::OneShot || mMode == Mode::OneShotClean || mMode == Mode::Percs
        || mMode == Mode::DrumNBass;
    auto const liftTypeScale = cleanLiftTransparentMode ? 0.72f
                             : loudLiftMode ? 1.08f
                             : bassLiftMode ? 0.78f
                             : percussiveLiftMode ? 0.82f
                                                   : 0.90f;
    auto const loudnessDistributionScale = cleanLiftTransparentMode ? 0.68f
                                       : loudLiftMode ? 1.12f
                                       : bassLiftMode ? 0.82f
                                       : percussiveLiftMode ? 0.78f
                                                             : 0.92f;
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    auto const spectral3PercussiveMode =
        mMode == Mode::Drums || mMode == Mode::OneShot || mMode == Mode::OneShotClean || mMode == Mode::Percs
        || mMode == Mode::DrumNBass;
    auto const spectral3BassMode =
        mMode == Mode::HipHop || mMode == Mode::Trap || mMode == Mode::Bass || mMode == Mode::Kick808
        || mMode == Mode::Dubstep;
    auto const spectral3BrightMode = mMode == Mode::Bright || mMode == Mode::Vocal || mMode == Mode::DrumNBass;
#endif
    float blockClipAmountDb = 0.0f;
    float blockGainReductionDb = 0.0f;
    auto const feedbackAttack = mFeedbackAttackCoefficient;
    auto const feedbackRelease = mFeedbackReleaseCoefficient;
    auto const sanitizeFeedback = [](float value) {
        return std::isfinite(value) ? juce::jlimit(0.0f, 1.0f, value) : 0.0f;
    };
    auto const smoothFeedback = [&](float current, float target) {
        auto const safeCurrent = sanitizeFeedback(current);
        auto const safeTarget = sanitizeFeedback(target);
        auto const coefficient = safeTarget > safeCurrent ? feedbackAttack : feedbackRelease;
        return sanitizeFeedback((coefficient * safeCurrent) + ((1.0f - coefficient) * safeTarget));
    };

    std::array<float*, 2> samplePointers{nullptr, nullptr};
    for (size_t channel = 0; channel < numChannels; ++channel) {
        samplePointers[channel] = block.getChannelPointer(channel);
    }
    std::array<float, 2> preSaturationInputs{0.0f, 0.0f};
    std::array<float, 2> detectorInputs{0.0f, 0.0f};
    std::array<float, 2> signalInputs{0.0f, 0.0f};
    std::array<float, 2> processedOutputs{0.0f, 0.0f};
    auto const snapSmoothed = [](float& value, float target) {
        if (std::abs(value - target) <= 1.0e-6f) {
            value = target;
        }
    };
    snapSmoothed(mSmoothedThresholdDb, mThresholdDb);
    snapSmoothed(mSmoothedCeilingDb, mCeilingDb);
    snapSmoothed(mSmoothedTone, mTone);
    snapSmoothed(mSmoothedSaturation, mSaturation);
    snapSmoothed(mSmoothedFinalClip, mFinalClip);
    snapSmoothed(mSmoothedHfGuard, mHfGuard);
#if defined(PEAKEATER_DISABLE_SETTLED_FAST_PATH)
    constexpr auto smoothingActive = true;
#else
    auto const smoothingActive = mSmoothedThresholdDb != mThresholdDb || mSmoothedCeilingDb != mCeilingDb
                                 || mSmoothedTone != mTone || mSmoothedSaturation != mSaturation
                                 || mSmoothedFinalClip != mFinalClip || mSmoothedHfGuard != mHfGuard;
#endif
    auto const settledThresholdDriveDb = thresholdToDriveDb(mThresholdDb) * mode.thresholdDriveScale;
    auto const settledDriveSafetyBase = juce::jlimit(0.0f, 1.0f, (settledThresholdDriveDb - 3.0f) / 31.0f);
    auto const settledHeavyDriveSafetyBase = juce::jlimit(0.0f, 1.0f, (settledThresholdDriveDb - 12.0f) / 34.0f);
    auto const settledDriveSafety = juce::jlimit(0.0f, 1.0f, settledDriveSafetyBase * driveQualityScale);
    auto const settledHeavyDriveSafety = juce::jlimit(0.0f, 1.0f, settledHeavyDriveSafetyBase * driveQualityScale);
    auto const settledLowProtectAmount =
        juce::jlimit(0.0f, 1.0f, baseLowProtectAmount + (settledDriveSafety * driveLowProtectBoost)
                                       + (settledHeavyDriveSafety * driveLowProtectBoost * 0.55f));
    auto const settledOvershootReleaseCoefficient =
        settledLowProtectAmount <= 0.0001f
            ? mLimiterOvershootReleaseBaseCoefficient
            : fastSmoothingCoefficient(38.0f + (settledLowProtectAmount * 42.0f), mSampleRate);
    auto const settledSignalDrive = fastDbToGain(settledThresholdDriveDb);
#if defined(PEAKEATER_SPECTRAL_2_VARIANT)
    auto const settledDetectorDrive = fastDbToGain(mode.driveDb + std::min(3.0f, settledThresholdDriveDb * 0.070f));
#else
    auto const settledDetectorDrive = fastDbToGain(mode.driveDb + std::min(8.0f, settledThresholdDriveDb * 0.18f));
#endif
    auto const settledCeilingGain = fastDbToGain(mCeilingDb);
    auto const blockSpectralTransientProtect = juce::jlimit(0.0f, 1.0f, mTransientRecovery + (mPunchProtect * 0.55f));
    auto const blockGenreSustainScale = mode.spectralSustainScale * (1.0f + mode.spectralGenreLift * 0.04f);
    auto const blockGenreUpwardStartOffsetDb = mode.spectralUpwardStartOffsetDb + (mode.spectralGenreLift * 0.12f);
    auto const blockGenreUpwardMaxBoostDb = mode.spectralGenreLift * 0.24f;
    auto const blockGenreUpwardSlopeBoost = mode.spectralGenreLift * 0.006f;
    auto const blockLowCpuSpectralQuality = mBandMultiplier <= 4;
    auto const blockPercussiveMode = mPercussiveMode;
    for (size_t sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex) {
        if (smoothingActive) {
            mSmoothedThresholdDb = (thresholdSmooth * mSmoothedThresholdDb) + ((1.0f - thresholdSmooth) * mThresholdDb);
            mSmoothedCeilingDb = (ceilingSmooth * mSmoothedCeilingDb) + ((1.0f - ceilingSmooth) * mCeilingDb);
            mSmoothedTone = (toneSmooth * mSmoothedTone) + ((1.0f - toneSmooth) * mTone);
            mSmoothedSaturation = (saturationSmooth * mSmoothedSaturation) + ((1.0f - saturationSmooth) * mSaturation);
            mSmoothedFinalClip = (finalClipSmooth * mSmoothedFinalClip) + ((1.0f - finalClipSmooth) * mFinalClip);
            mSmoothedHfGuard = (hfGuardSmooth * mSmoothedHfGuard) + ((1.0f - hfGuardSmooth) * mHfGuard);
        }
        auto const thresholdDriveDb = smoothingActive
                                          ? thresholdToDriveDb(mSmoothedThresholdDb) * mode.thresholdDriveScale
                                          : settledThresholdDriveDb;
        auto const driveAmount = juce::jlimit(0.0f, 1.0f, thresholdDriveDb / 48.0f);
        auto const driveSafetyBase = juce::jlimit(0.0f, 1.0f, (thresholdDriveDb - 3.0f) / 31.0f);
        auto const heavyDriveSafetyBase = juce::jlimit(0.0f, 1.0f, (thresholdDriveDb - 12.0f) / 34.0f);
        auto const extremeDriveSafetyBase = juce::jlimit(0.0f, 1.0f, (thresholdDriveDb - 24.0f) / 24.0f);
        auto const driveSafety = juce::jlimit(0.0f, 1.0f, driveSafetyBase * driveQualityScale);
        auto const heavyDriveSafety = juce::jlimit(0.0f, 1.0f, heavyDriveSafetyBase * driveQualityScale);
        auto const extremeDriveSafety = juce::jlimit(0.0f, 1.0f, extremeDriveSafetyBase * driveQualityScale);
        auto const driveClipAvoidance = juce::jlimit(0.0f, 1.0f, (driveSafety * 0.26f) + (heavyDriveSafety * 0.22f)
                                                                        + (extremeDriveSafety * 0.18f) + (qualitySafety * 0.05f)
                                                                        + (masteringQuality * 0.08f));
        auto const lookaheadPeakControl =
            lookaheadPrecision * juce::jlimit(0.0f, 1.0f, 0.28f + (driveAmount * 0.34f) + (driveSafety * 0.22f)
                                                                + (heavyDriveSafety * 0.12f) + (qualitySafety * 0.16f));
        auto const effectivePunchProtect =
            juce::jlimit(0.0f, 1.0f, baseEffectivePunchProtect + (driveSafety * drivePunchBoost)
                                           + (heavyDriveSafety * drivePunchBoost * 0.45f) + (lookaheadPeakControl * 0.035f));
        auto const effectiveReleaseShape = juce::jlimit(0.0f, 1.0f,
                                                        baseEffectiveReleaseShape + (driveSafety * driveReleaseBoost)
                                                            + (heavyDriveSafety * driveReleaseBoost * 0.45f)
                                                            + (lookaheadPeakControl * 0.045f));
        auto const effectiveAdaptiveRelease = juce::jlimit(0.0f, 1.0f,
                                                           baseEffectiveAdaptiveRelease + (driveSafety * driveAdaptiveBoost)
                                                               + (heavyDriveSafety * driveAdaptiveBoost * 0.55f)
                                                               + (lookaheadPeakControl * 0.055f));
        auto const effectiveGrLimitDb =
            juce::jlimit(0.0f, 18.0f, baseEffectiveGrLimitDb + (driveSafety * driveGrLimitBoost)
                                           + (heavyDriveSafety * driveGrLimitBoost * 0.48f)
                                           + (extremeDriveSafety * driveGrLimitBoost * 0.45f)
                                           + (lookaheadPeakControl * 1.15f));
        auto const effectiveTruePeakMarginDb =
            juce::jlimit(0.0f, 1.0f, baseEffectiveTruePeakMarginDb + (driveSafety * driveMarginBoost)
                                           + (heavyDriveSafety * driveMarginBoost * 0.42f)
                                           + (extremeDriveSafety * driveMarginBoost * 0.38f)
                                           + (lookaheadPeakControl * 0.085f));
        auto const effectiveLowProtectAmount = smoothingActive
                                                   ? juce::jlimit(0.0f, 1.0f, baseLowProtectAmount
                                                                                  + (driveSafety * driveLowProtectBoost)
                                                                                  + (heavyDriveSafety * driveLowProtectBoost * 0.55f))
                                                   : settledLowProtectAmount;
        auto const overshootReleaseCoefficient =
            smoothingActive
                ? (effectiveLowProtectAmount <= 0.0001f
                       ? mLimiterOvershootReleaseBaseCoefficient
                       : fastSmoothingCoefficient(38.0f + (effectiveLowProtectAmount * 42.0f), mSampleRate))
                : settledOvershootReleaseCoefficient;
        auto const lowSensitivity =
            mode.lowDetectorScale * (1.0f - (effectiveLowProtectAmount * (0.72f - bassRecover * 0.18f)));
        auto const highScale = juce::jlimit(0.36f, 2.12f, 1.0f + (highTilt * 0.92f) - (lowTilt * 0.34f)
                                                              + (mHfGuard * highTilt * 0.24f)
                                                              + (driveSafety * driveHfGuardBoost * highTilt * 0.34f));
        auto const lowScale = juce::jlimit(0.04f, 2.15f, lowSensitivity * (1.0f + (lowTilt * 1.18f)
                                                                               - (highTilt * 0.62f)
                                                                               - (effectiveLowProtectAmount * (0.18f - bassRecover * 0.06f))));
        auto const signalDrive = smoothingActive ? fastDbToGain(thresholdDriveDb) : settledSignalDrive;
#if defined(PEAKEATER_SPECTRAL_2_VARIANT)
        auto const detectorDrive = smoothingActive
                                       ? fastDbToGain(mode.driveDb + std::min(3.0f, thresholdDriveDb * 0.070f))
                                       : settledDetectorDrive;
#else
        auto const detectorDrive = smoothingActive
                                       ? fastDbToGain(mode.driveDb + std::min(8.0f, thresholdDriveDb * 0.18f))
                                       : settledDetectorDrive;
#endif
        auto const ceilingGain = smoothingActive ? fastDbToGain(mSmoothedCeilingDb) : settledCeilingGain;
        auto const effectiveHfGuard =
            juce::jlimit(0.0f, 1.0f, mSmoothedHfGuard + style.hfGuardOffset + brightSafety
                                       + (driveSafety * driveHfGuardBoost * 0.28f)
                                       + (heavyDriveSafety * driveHfGuardBoost * 0.14f));
        auto const finalClipSafetyScale =
            juce::jlimit(0.48f, 1.0f, 1.0f - (qualitySafety * 0.055f) - (masteringQuality * 0.045f)
                                      - (driveSafety * driveFinalClipRelief * 0.30f)
                                      - (heavyDriveSafety * driveFinalClipRelief * 0.16f)
                                      - (extremeDriveSafety * driveFinalClipRelief * 0.12f)
                                      - (driveClipAvoidance * 0.045f) - (lookaheadPeakControl * 0.065f));
        auto const typeFinalClipBase = mode.softClipAmount * juce::jlimit(0.0f, 0.18f, 0.045f + (mode.hardClipBlend * 0.18f))
                                       * juce::jlimit(0.35f, 1.25f, 0.70f + (mode.loudnessFocus * 0.58f));
#if defined(PEAKEATER_SPECTRAL_2_VARIANT)
        auto const typeFinalClip =
            typeFinalClipBase * juce::jlimit(0.14f, 0.46f, 0.42f - (driveAmount * 0.18f) - (driveSafety * 0.10f));
#else
        auto const typeFinalClip = typeFinalClipBase;
#endif
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
        auto const drive24Relief = hermiteShape(juce::jlimit(0.0f, 1.0f, (-mSmoothedThresholdDb - 12.0f) / 12.0f));
        auto const typeClipRelief = mMode == Mode::Clean || mMode == Mode::Acoustic || mMode == Mode::Vocal
                                       || mMode == Mode::OneShotClean ? 0.35f
                                   : spectral3BassMode ? 0.30f
                                   : spectral3PercussiveMode ? 0.25f : 0.20f;
        auto const drive24ClipScale = 1.0f - drive24Relief * typeClipRelief;
#else
        constexpr auto drive24ClipScale = 1.0f;
#endif
        auto const effectiveFinalClip =
            juce::jlimit(0.0f, 1.0f, (mSmoothedFinalClip + style.finalClipOffset + typeFinalClip) * finalClipSafetyScale * drive24ClipScale);
        auto const effectiveSpectralDepth =
            juce::jlimit(0.54f, 0.86f, spectralDepth + (driveSafety * driveSpectralBoost * 0.72f)
                                       + (heavyDriveSafety * driveSpectralBoost * 0.16f) + (driveClipAvoidance * 0.018f)
                                       + (lookaheadPeakControl * 0.018f));
        PeakBudgetControls peakBudget;
        peakBudget.upwardQuality = juce::jlimit(0.0f, 1.0f, (qualitySafety * 0.34f) + (masteringQuality * 0.52f));
        peakBudget.upwardQuality = juce::jlimit(0.0f, 1.0f, peakBudget.upwardQuality + (ultraRenderQuality * 0.18f)
                                                                                       + (maxRenderQuality * 0.06f));
        peakBudget.peakPressure =
            juce::jlimit(0.0f, 1.0f, (driveSafety * 0.46f) + (heavyDriveSafety * 0.34f) + (extremeDriveSafety * 0.22f)
                                            + (qualitySafety * 0.08f));
        peakBudget.densityBudget =
            juce::jlimit(0.0f, 1.0f, ((driveAmount * 0.46f) + (driveSafety * 0.15f) + (peakBudget.upwardQuality * 0.10f))
                                            * juce::jlimit(0.42f, 1.18f, typeDensityBias)
                                        * (1.0f - effectivePunchProtect * 0.08f));
        peakBudget.densityBudget =
            juce::jlimit(0.0f, 1.0f, peakBudget.densityBudget + (ultraRenderQuality * mode.loudnessFocus * 0.040f)
                                                                  + (maxRenderQuality * mode.densityLift * 0.018f));
        peakBudget.transientReserve =
            juce::jlimit(0.0f, 1.0f, typeTransientReserve + (effectivePunchProtect * 0.40f) + (mTransientRecovery * 0.18f)
                                           - (peakBudget.densityBudget * 0.08f));
        peakBudget.lowReserve =
            juce::jlimit(0.0f, 1.0f, typeLowReserve + (effectiveLowProtectAmount * 0.42f) + (bassSafe * 0.32f)
                                           + (bassRecover * 0.24f));
        peakBudget.clipRelief =
            juce::jlimit(0.0f, 1.0f, (peakBudget.peakPressure * 0.28f) + (peakBudget.upwardQuality * 0.16f)
                                           - (typeClipKeep * 0.06f));
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
        if (spectral3PercussiveMode) {
            peakBudget.transientReserve = juce::jlimit(0.0f, 1.0f, peakBudget.transientReserve + 0.035f + (driveSafety * 0.020f));
            peakBudget.densityBudget = juce::jlimit(0.0f, 1.0f, peakBudget.densityBudget * (0.96f - driveSafety * 0.025f));
        }
        if (spectral3BassMode) {
            peakBudget.lowReserve = juce::jlimit(0.0f, 1.0f, peakBudget.lowReserve + 0.030f + (bassRecover * 0.040f));
            peakBudget.clipRelief = juce::jlimit(0.0f, 1.0f, peakBudget.clipRelief + (driveSafety * 0.025f));
        }
        if (spectral3BrightMode) {
            peakBudget.upwardQuality = juce::jlimit(0.0f, 1.0f, peakBudget.upwardQuality + (qualitySafety * 0.030f));
            peakBudget.clipRelief = juce::jlimit(0.0f, 1.0f, peakBudget.clipRelief + (qualitySafety * 0.018f));
        }
#endif
        PeakBudgetFeedback feedback{};
        if (numChannels > 0) {
            auto const feedbackScale = 1.0f / static_cast<float>(numChannels);
            for (size_t channel = 0; channel < numChannels; ++channel) {
                auto const& channelFeedback = mChannelStates[channel].peakBudgetFeedback;
                feedback.clipPressure += sanitizeFeedback(channelFeedback.clipPressure) * feedbackScale;
                feedback.ceilingPressure += sanitizeFeedback(channelFeedback.ceilingPressure) * feedbackScale;
                feedback.sustainClipDebt += sanitizeFeedback(channelFeedback.sustainClipDebt) * feedbackScale;
                feedback.transientRisk += sanitizeFeedback(channelFeedback.transientRisk) * feedbackScale;
                feedback.lowMonoRisk += sanitizeFeedback(channelFeedback.lowMonoRisk) * feedbackScale;
                feedback.densitySuccess += sanitizeFeedback(channelFeedback.densitySuccess) * feedbackScale;
            }
        }
        auto const feedbackPressure = juce::jlimit(0.0f, 1.0f, (feedback.clipPressure * 0.58f) + (feedback.ceilingPressure * 0.42f));
        peakBudget.clipRelief =
            juce::jlimit(0.0f, 1.0f, peakBudget.clipRelief + (feedbackPressure * 0.12f * cleanSafetyBias * loudTypeClipKeep));
        peakBudget.transientReserve =
            juce::jlimit(0.0f, 1.0f, peakBudget.transientReserve + (feedback.transientRisk * 0.18f));
        peakBudget.lowReserve =
            juce::jlimit(0.0f, 1.0f, peakBudget.lowReserve + (feedback.lowMonoRisk * (0.16f + bassSafe * 0.10f)));
        peakBudget.upwardQuality =
            juce::jlimit(0.0f, 1.0f, peakBudget.upwardQuality * (1.0f - feedbackPressure * 0.18f - feedback.transientRisk * 0.12f
                                                                  - feedback.lowMonoRisk * 0.06f));
        peakBudget.densityBudget =
            juce::jlimit(0.0f, 1.0f, (peakBudget.densityBudget * (1.0f - feedback.sustainClipDebt * 0.16f
                                                                  - feedback.transientRisk * 0.10f
                                                                  - feedback.lowMonoRisk * 0.05f
                                                                  - feedbackPressure * 0.08f))
                                           + (feedback.densitySuccess * 0.038f * (1.0f - feedbackPressure * 0.58f)));
        auto const headroomConfidence =
            juce::jlimit(0.0f, 1.0f, 1.0f - feedbackPressure * 0.52f
                                           - feedback.sustainClipDebt * 0.42f
                                           - feedback.transientRisk * (percussiveLiftMode ? 0.30f : 0.18f)
                                           - feedback.lowMonoRisk * (bassLiftMode ? 0.26f : 0.12f));
        auto const sustainSafety =
            juce::jlimit(0.0f, 1.0f, headroomConfidence
                                           * (1.0f - peakBudget.transientReserve * (percussiveLiftMode ? 0.20f : 0.10f))
                                           * (1.0f - peakBudget.lowReserve * (bassLiftMode ? 0.18f : 0.06f)));
        peakBudget.loudnessDistribution =
            juce::jlimit(0.0f, 1.0f, driveAmount * (0.22f + mode.loudnessFocus * 0.30f + peakBudget.upwardQuality * 0.06f)
                                           * loudnessDistributionScale * headroomConfidence);
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
        auto const spectral3LoudnessModeScale = loudLiftMode ? 1.0f
                                               : cleanLiftTransparentMode ? 0.40f
                                               : bassLiftMode ? 0.62f
                                               : percussiveLiftMode ? 0.54f
                                                                     : 0.72f;
        auto const spectral3LoudnessOpportunity =
            driveAmount * headroomConfidence * spectral3LoudnessModeScale
            * juce::jlimit(0.0f, 1.0f, 1.0f - feedback.transientRisk * 0.58f
                                            - feedback.lowMonoRisk * 0.34f
                                            - feedback.sustainClipDebt * 0.42f);
        peakBudget.loudnessDistribution =
            juce::jlimit(0.0f, 1.0f, peakBudget.loudnessDistribution
                                           + spectral3LoudnessOpportunity
                                                 * (0.050f + qualitySafety * 0.018f + masteringQuality * 0.024f));
#endif
        peakBudget.peakPacking =
            juce::jlimit(0.0f, 1.0f, ((driveSafety * 0.18f) + (heavyDriveSafety * 0.17f)
                                      + (feedback.densitySuccess * 0.20f) + (peakBudget.peakPressure * 0.10f))
                                           * juce::jlimit(0.0f, 1.0f, 1.0f - feedbackPressure * 0.50f
                                                                            - feedback.transientRisk * 0.18f
                                                                            - feedback.lowMonoRisk * 0.10f));
        peakBudget.sustainLift =
            juce::jlimit(0.0f, 1.0f, ((peakBudget.densityBudget * 0.55f) + (peakBudget.loudnessDistribution * 0.28f)
                                      + (feedback.densitySuccess * 0.18f)) * sustainSafety);
        peakBudget.crestMicroGr =
            juce::jlimit(0.0f, 1.0f, ((peakBudget.peakPressure * 0.20f) + (feedbackPressure * 0.18f)
                                      + (driveSafety * 0.16f) + (heavyDriveSafety * 0.12f)
                                      + (lookaheadPeakControl * 0.12f))
                                           * juce::jlimit(0.35f, 1.0f, 1.0f - peakBudget.transientReserve * 0.12f
                                                                            - feedback.transientRisk * (percussiveLiftMode ? 0.32f : 0.18f)
                                                                            - feedback.lowMonoRisk * 0.08f));
        peakBudget.densityBudget =
            juce::jlimit(0.0f, 1.0f, peakBudget.densityBudget
                                           + peakBudget.sustainLift * (0.050f + mode.loudnessFocus * 0.030f)
                                               * juce::jlimit(0.35f, 1.0f, 1.0f - feedbackPressure * 0.30f));
        peakBudget.clipRelief =
            juce::jlimit(0.0f, 1.0f, peakBudget.clipRelief + peakBudget.crestMicroGr * 0.050f
                                           + peakBudget.peakPacking * 0.035f);
        peakBudget.cleanLift =
            juce::jlimit(0.0f, 1.0f,
                         driveAmount * (0.10f + (mode.loudnessFocus * 0.22f) + (peakBudget.densityBudget * 0.12f))
                             * liftTypeScale
                             * juce::jlimit(0.34f, 1.0f, 1.0f - feedbackPressure * 0.32f
                                                              - feedback.transientRisk * (percussiveLiftMode ? 0.34f : 0.20f)
                                                              - feedback.lowMonoRisk * 0.12f)
                             + peakBudget.loudnessDistribution * (0.040f + mode.loudnessFocus * 0.045f) * sustainSafety);
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
        peakBudget.cleanLift =
            juce::jlimit(0.0f, 1.0f, peakBudget.cleanLift
                                           + spectral3LoudnessOpportunity
                                                 * (0.018f + mode.loudnessFocus * 0.024f + masteringQuality * 0.014f));
#endif
        peakBudget.microGr =
            juce::jlimit(0.0f, 1.0f, (driveSafety * 0.30f) + (heavyDriveSafety * 0.28f)
                                           + (extremeDriveSafety * 0.18f) + (peakBudget.peakPressure * 0.18f)
                                           + (feedbackPressure * 0.10f) + (lookaheadPeakControl * 0.16f)
                                           + (peakBudget.crestMicroGr * 0.18f) + (peakBudget.peakPacking * 0.08f)
                                           + (driveClipAvoidance * (0.095f + drumsDriveMicroGr)));
        peakBudget.microGr *= juce::jlimit(0.56f, 1.0f, 1.0f - peakBudget.transientReserve * 0.10f
                                                        - feedback.transientRisk * 0.12f
                                                        - feedback.lowMonoRisk * 0.06f);
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
        peakBudget.microGr =
            juce::jlimit(0.0f, 1.0f, peakBudget.microGr
                                           + feedbackPressure * (0.024f + qualitySafety * 0.020f)
                                           + peakBudget.peakPacking * 0.022f)
            * juce::jlimit(0.74f, 1.0f, 1.0f - feedback.transientRisk * 0.18f
                                            - feedback.lowMonoRisk * 0.08f);
#endif
        peakBudget.roundedClipQuality =
            juce::jlimit(0.0f, 1.0f, (nonlinearQuality * 0.55f) + (qualitySafety * 0.24f)
                                           + (masteringQuality * 0.30f) + (peakBudget.microGr * 0.10f)
                                           + (peakBudget.crestMicroGr * 0.06f) + (peakBudget.peakPacking * 0.08f));
        auto const effectiveFinalClipClosedLoop =
            effectiveFinalClip * juce::jlimit(0.62f, 1.0f, 1.0f - feedbackPressure * 0.18f - feedback.transientRisk * 0.06f
                                                            - feedback.lowMonoRisk * 0.05f - peakBudget.microGr * 0.045f
                                                            - peakBudget.crestMicroGr * 0.025f - peakBudget.peakPacking * 0.018f
                                                            - driveClipAvoidance * 0.050f);

        for (size_t channel = 0; channel < numChannels; ++channel) {
            auto& state = mChannelStates[channel];
            auto const inputSample = samplePointers[channel][sampleIndex];
            auto const rawInput = std::isfinite(inputSample) ? inputSample : 0.0f;
            blockPeak = std::max(blockPeak, std::abs(rawInput));
            if (!std::isfinite(inputSample)) {
                blockClipAmountDb = std::max(blockClipAmountDb, 24.0f);
            }
            auto const drivenSignal = rawInput * signalDrive;
            preSaturationInputs[channel] = drivenSignal;
            auto const drivenSignalDb = fastGainToDb(std::abs(drivenSignal));
            auto const drivenOverDb = std::max(0.0f, drivenSignalDb - mSmoothedCeilingDb);
            auto const inputOverloadSafety = juce::jlimit(0.0f, 1.0f, (drivenOverDb - 12.0f) / 36.0f);
            auto const magnitude = std::abs(drivenSignal);
            state.saturationFastEnvelope = saturationFast * state.saturationFastEnvelope + (1.0f - saturationFast) * magnitude;
            state.saturationSlowEnvelope = saturationSlow * state.saturationSlowEnvelope + (1.0f - saturationSlow) * magnitude;
            auto const transientProtect =
                juce::jlimit(0.0f, 1.0f, (state.saturationFastEnvelope - state.saturationSlowEnvelope)
                                            / std::max(0.000001f, state.saturationSlowEnvelope) * 0.65f);
            auto const smoothedSaturation = juce::jlimit(0.0f, 1.0f, mSmoothedSaturation);
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
            auto const userSaturation = smoothedSaturation * (0.60f + (smoothedSaturation * 0.14f));
#else
            auto const userSaturation = smoothedSaturation * std::sqrt(smoothedSaturation) * 0.58f;
#endif
            auto const saturationSafetyScale =
                juce::jlimit(0.48f, 1.0f, 1.0f - (driveSafety * effectiveDriveSaturationRelief * 0.70f)
                                          - (heavyDriveSafety * effectiveDriveSaturationRelief * 0.34f)
                                          - (extremeDriveSafety * effectiveDriveSaturationRelief * 0.24f)
                                          - (inputOverloadSafety * (0.035f + qualitySafety * 0.06f)));
#if defined(PEAKEATER_SPECTRAL_2_VARIANT)
            auto const typeSaturationCleanDrive =
                typeSaturation * juce::jlimit(0.10f, 0.42f, 0.38f - (driveAmount * 0.16f) - (driveSafety * 0.09f));
            auto const effectiveSaturation =
                juce::jlimit(0.0f, 0.62f, (userSaturation + typeSaturationCleanDrive) * saturationSafetyScale);
#else
   #if defined(PEAKEATER_SPECTRAL_3_VARIANT)
            auto const effectiveSaturation = juce::jlimit(0.0f, 0.82f, std::max(userSaturation, typeSaturation) * saturationSafetyScale);
   #else
            auto const effectiveSaturation = juce::jlimit(0.0f, 0.72f, std::max(userSaturation, typeSaturation) * saturationSafetyScale);
   #endif
#endif
            state.saturationSplitLowpass += saturationSplitCoefficient * (drivenSignal - state.saturationSplitLowpass);
            auto const lowSignal = state.saturationSplitLowpass;
            auto const highSignal = drivenSignal - lowSignal;
            auto const transientSatRelief =
                1.0f - (transientProtect * juce::jlimit(0.0f, 0.58f, 0.18f + (qualitySafety * 0.16f)
                                                                          + (effectivePunchProtect * 0.13f)
                                                                          + (typeTransientPriority * 0.11f)));
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
            constexpr auto lowToneBoost = 0.44f;
            constexpr auto lowToneCut = 0.50f;
            constexpr auto highToneBoost = 0.66f;
            constexpr auto highToneCut = 0.23f;
#else
            constexpr auto lowToneBoost = 0.35f;
            constexpr auto lowToneCut = 0.42f;
            constexpr auto highToneBoost = 0.52f;
            constexpr auto highToneCut = 0.18f;
#endif
            auto const bassSafeCut = 0.68f + (bassSafeEmphasis * 0.10f);
            auto const bassSafeRestore = 0.58f + (bassSafeEmphasis * 0.10f);
            auto const lowAmount = effectiveSaturation * transientSatRelief
                                   * juce::jlimit(0.08f, 1.55f, 1.0f + (std::max(0.0f, -satTone) * lowToneBoost)
                                                                                      - (std::max(0.0f, satTone) * lowToneCut)
                                                                                      - (bassSafe * bassSafeCut)
                                                                                      - (bassRecover * 0.52f));
            auto const highAmount = effectiveSaturation * transientSatRelief
                                    * juce::jlimit(0.18f, 1.65f, 1.0f + (std::max(0.0f, satTone) * highToneBoost)
                                                                                       - (std::max(0.0f, -satTone) * highToneCut));
            auto const saturationActive = effectiveSaturation > 0.0008f;
            auto const saturatedLowCurrent =
                saturationActive ? applySaturation(lowSignal, lowAmount, mode.densityLift, thresholdDriveDb, transientProtect,
                                                   saturationSettings, mSaturationDensity, state.previousSaturationLowInput)
                                 : lowSignal;
            auto const saturatedHighCurrent =
                saturationActive ? applySaturation(highSignal, highAmount, mode.densityLift, thresholdDriveDb, transientProtect,
                                                   saturationSettings, mSaturationDensity, state.previousSaturationHighInput)
                                 : highSignal;
            auto saturatedLowMid = saturatedLowCurrent;
            auto saturatedHighMid = saturatedHighCurrent;
            auto const saturationNeedsInterpolation =
                hqNonlinearInterpolation && saturationActive && (std::max(lowAmount, highAmount) > 0.0035f)
                && (effectiveSaturation > 0.006f || mode.densityLift > 0.38f)
                && std::max(std::abs(lowSignal), std::abs(highSignal)) > 0.18f;
            if (saturationNeedsInterpolation) {
                auto const lowMidpoint = (state.previousSaturationLowInput + lowSignal) * 0.5f;
                auto const highMidpoint = (state.previousSaturationHighInput + highSignal) * 0.5f;
                saturatedLowMid = applySaturation(lowMidpoint, lowAmount, mode.densityLift, thresholdDriveDb, transientProtect,
                                                  saturationSettings, mSaturationDensity, state.previousSaturationLowInput);
                saturatedHighMid = applySaturation(highMidpoint, highAmount, mode.densityLift, thresholdDriveDb, transientProtect,
                                                   saturationSettings, mSaturationDensity, state.previousSaturationHighInput);
            }
            state.previousSaturationLowInput = lowSignal;
            state.previousSaturationHighInput = highSignal;
            auto const saturatedLow = saturatedLowCurrent + ((saturatedLowMid - saturatedLowCurrent) * nonlinearQuality * 0.34f);
            auto const saturatedHigh = saturatedHighCurrent + ((saturatedHighMid - saturatedHighCurrent) * nonlinearQuality * 0.42f);
            auto const lowLoss = lowSignal - saturatedLow;
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
            constexpr auto bassRestoreLimit = 0.80f;
#else
            constexpr auto bassRestoreLimit = 0.76f;
#endif
            auto const bassRecoverBlend =
                juce::jlimit(0.0f, bassRestoreLimit, (bassSafe * bassSafeRestore)
                                           + (bassRecover * (0.22f + driveAmount * 0.34f + inputOverloadSafety * 0.16f))
                                                 * juce::jlimit(0.68f, 1.0f, 1.0f - transientProtect * 0.18f));
            auto const bassRestored = saturatedLow + (lowLoss * bassRecoverBlend);
            auto const saturatedSignal = bassRestored + saturatedHigh;
            auto const saturationReductionDb =
                saturationActive ? std::max(0.0f, drivenSignalDb - fastGainToDb(std::abs(saturatedSignal))) : 0.0f;
            blockClipAmountDb = std::max(blockClipAmountDb, saturationReductionDb);
            auto const drivenDetector = saturatedSignal * detectorDrive;
            state.detectorHpLowpass += detectorHpCoefficient * (drivenDetector - state.detectorHpLowpass);
            auto const detectorHp = drivenDetector - state.detectorHpLowpass;
            auto const detectorLow = drivenDetector - detectorHp;
            detectorInputs[channel] = (detectorHp * highScale) + (detectorLow * lowScale);
            signalInputs[channel] = saturatedSignal;
        }

        MidSideBudget midSideBudget{};
        auto stereoSustainOpportunity = 0.0f;
        if (numChannels > 1) {
            auto linkedDetectorInputs = detectorInputs;
            auto const stereoLink = juce::jlimit(0.0f, 1.0f, mode.stereoLink * mStereoLink);
            auto const bassLock = effectiveLowProtectAmount;
            auto const detectorMid = (detectorInputs[0] + detectorInputs[1]) * 0.5f;
            auto const detectorSide = (detectorInputs[0] - detectorInputs[1]) * 0.5f;
            auto const signalMid = (signalInputs[0] + signalInputs[1]) * 0.5f;
            auto const signalSide = (signalInputs[0] - signalInputs[1]) * 0.5f;
            auto const monoEnergy = std::abs(detectorMid);
            auto const sideEnergy = std::abs(detectorSide);
            auto const detectorEnergyInverse = 1.0f / std::max(0.000001f, monoEnergy + sideEnergy);
            auto const lowDominance = juce::jlimit(0.0f, 1.0f, monoEnergy * detectorEnergyInverse);
            auto const sideFreedom = juce::jlimit(0.0f, 0.42f, sideEnergy * detectorEnergyInverse);
            auto const lowSideRatio = juce::jlimit(0.0f, 1.0f, sideEnergy * detectorEnergyInverse);
            auto const signalMidMagnitude = std::abs(signalMid);
            auto const signalSideMagnitude = std::abs(signalSide);
            auto const signalSideRatio = juce::jlimit(0.0f, 1.0f, signalSideMagnitude
                                                                  / std::max(0.000001f, signalMidMagnitude + signalSideMagnitude));
            auto const lowRiskGate = hermiteShape(juce::jlimit(0.0f, 1.0f, (std::max(lowSideRatio, signalSideRatio) - 0.20f) * 3.15f));
            auto const bassTypeBias =
                (mMode == Mode::Kick808 || mMode == Mode::Bass || mMode == Mode::Trap || mMode == Mode::HipHop) ? 1.20f
              : (mMode == Mode::Dubstep || mMode == Mode::EDM || mMode == Mode::DrumNBass) ? 1.05f
                                                                                             : 0.86f;
            auto const lowMonoRisk = lowRiskGate * juce::jlimit(0.18f, 1.0f, (bassLock + lowTilt + bassSafe * 0.65f) * bassTypeBias);
            midSideBudget.midPeakPressure = hermiteShape(juce::jlimit(0.0f, 1.0f, (fastGainToDb(std::abs(signalMid)) - mSmoothedCeilingDb) / 12.0f));
            midSideBudget.sidePeakPressure = hermiteShape(juce::jlimit(0.0f, 1.0f, (fastGainToDb(std::abs(signalSide)) - mSmoothedCeilingDb + 1.2f) / 12.0f));
            midSideBudget.lowSideRisk = lowMonoRisk;
            midSideBudget.monoCompatibilityRisk =
                juce::jlimit(0.0f, 1.0f, (lowMonoRisk * 0.72f) + (signalSideRatio * bassLock * 0.18f)
                                             + (midSideBudget.sidePeakPressure * 0.10f));
            midSideBudget.widthPreserve =
                juce::jlimit(0.72f, 1.0f, 1.0f - (midSideBudget.lowSideRisk * (0.08f + bassLock * 0.10f))
                                             - (midSideBudget.sidePeakPressure * 0.025f)
                                             + (highTilt * sideFreedom * 0.08f));
            auto const centerCoherence = 1.0f - signalSideRatio;
            auto const centerLevel = hermiteShape(juce::jlimit(0.0f, 1.0f, (signalMidMagnitude - 0.03162278f) * 2.1290035f));
            stereoSustainOpportunity = centerCoherence * centerLevel * (1.0f - midSideBudget.lowSideRisk)
                                       * (1.0f - midSideBudget.sidePeakPressure);
            for (size_t channel = 0; channel < numChannels; ++channel) {
                auto& channelFeedback = mChannelStates[channel].peakBudgetFeedback;
                channelFeedback.lowMonoRisk = smoothFeedback(channelFeedback.lowMonoRisk, midSideBudget.monoCompatibilityRisk);
            }
            auto const lowTiltLink = lowTilt * lowDominance * 0.12f;
            auto const highTiltFreedom = highTilt * sideFreedom * 0.18f;
            auto const riskDrivenBassLock = bassLock * juce::jlimit(0.22f, 1.0f, 0.22f + lowMonoRisk * 0.78f);
            auto const effectiveStereoLink =
                juce::jlimit(0.0f, 1.0f, stereoLink + (riskDrivenBassLock * lowDominance * 0.60f)
                                                  + (midSideBudget.lowSideRisk * bassLock * 0.16f) + lowTiltLink
                                                  - ((1.0f - bassLock) * sideFreedom * 0.22f) - highTiltFreedom);
            auto const sideScale = juce::jlimit(0.16f, 1.0f,
                                                1.0f - (effectiveStereoLink * (0.30f + riskDrivenBassLock * 0.36f
                                                                               + lowDominance * 0.10f
                                                                               + midSideBudget.lowSideRisk * 0.08f))
                                                    + (highTilt * sideFreedom * 0.18f));
            for (size_t channel = 0; channel < numChannels; ++channel) {
                auto const channelSide = channel == 0 ? detectorSide : -detectorSide;
                auto const msLinked = detectorMid + (channelSide * sideScale);
                linkedDetectorInputs[channel] = detectorInputs[channel] + ((msLinked - detectorInputs[channel]) * effectiveStereoLink);
            }
            detectorInputs = linkedDetectorInputs;
        }

        peakBudget.lowSideRisk = midSideBudget.lowSideRisk;
        peakBudget.widthPreserve = midSideBudget.widthPreserve;
        peakBudget.sideClipRelief =
            juce::jlimit(0.0f, 1.0f, midSideBudget.sidePeakPressure * (0.20f + peakBudget.upwardQuality * 0.10f)
                                       + midSideBudget.monoCompatibilityRisk * 0.10f);
        peakBudget.lowReserve =
            juce::jlimit(0.0f, 1.0f, peakBudget.lowReserve + midSideBudget.monoCompatibilityRisk * (0.11f + bassSafe * 0.06f));
        peakBudget.clipRelief =
            juce::jlimit(0.0f, 1.0f, peakBudget.clipRelief + peakBudget.sideClipRelief * (0.08f + driveSafety * 0.05f));
        peakBudget.microGr =
            juce::jlimit(0.0f, 1.0f, peakBudget.microGr + peakBudget.sideClipRelief * (0.12f + qualitySafety * 0.08f));
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
        if (mBandMultiplier > 4) {
            auto const sharedLoudnessAuthority = std::max(peakBudget.upwardQuality, peakBudget.densityBudget * 0.80f);
            auto const sharedLoudnessLift = stereoSustainOpportunity * sharedLoudnessAuthority;
            peakBudget.sustainLift = juce::jlimit(0.0f, 1.0f, peakBudget.sustainLift + sharedLoudnessLift * 0.055f);
            peakBudget.densityBudget = juce::jlimit(0.0f, 1.0f, peakBudget.densityBudget + sharedLoudnessLift * 0.030f);
            auto const sharedLoudnessTargetBaseDb =
                sharedLoudnessLift * juce::jlimit(0.0f, 1.0f, 1.0f - feedbackPressure * 0.64f)
                * juce::jlimit(0.42f, 1.0f, 1.0f - peakBudget.transientReserve * 0.34f) * 0.45f;
#if defined(PEAKEATER_CPU_BENCHMARK)
            auto const allocatorRecoveryEnabled = mPerceptualAllocatorEnabled;
#else
            constexpr auto allocatorRecoveryEnabled = true;
#endif
            auto const allocatorRecoveryDb =
                allocatorRecoveryEnabled
                    ? juce::jlimit(0.0f, 0.15f,
                                   peakBudget.densityBudget * juce::jlimit(0.0f, 1.0f, 1.0f - feedbackPressure)
                                       * juce::jlimit(0.0f, 1.0f, 1.0f - peakBudget.transientReserve * 0.72f)
                                       * juce::jlimit(0.0f, 1.0f, 1.0f - feedback.lowMonoRisk * 0.65f) * 2.75f)
                    : 0.0f;
            auto const sharedLoudnessTargetDb = sharedLoudnessTargetBaseDb + allocatorRecoveryDb;
            auto const sharedLoudnessCoefficient =
                sharedLoudnessTargetDb > mSharedStereoLoudnessDb ? mFeedbackAttackCoefficient : mFeedbackReleaseCoefficient;
            mSharedStereoLoudnessDb = (sharedLoudnessCoefficient * mSharedStereoLoudnessDb)
                                      + ((1.0f - sharedLoudnessCoefficient) * sharedLoudnessTargetDb);
        }
#endif

        auto const spectralReleaseShapeAmount = juce::jlimit(0.0f, 1.0f, effectiveReleaseShape);
        auto const spectralFastBias = juce::jlimit(0.0f, 1.0f, (0.5f - spectralReleaseShapeAmount) * 2.0f);
        auto const spectralSmoothBias = juce::jlimit(0.0f, 1.0f, (spectralReleaseShapeAmount - 0.5f) * 2.0f);
        SpectralSharedControls spectralShared{};
        spectralShared.driveAmount = juce::jlimit(0.0f, 1.0f, thresholdDriveDb / 48.0f);
        spectralShared.driveSafety = juce::jlimit(0.0f, 1.0f, (thresholdDriveDb - 4.0f) / 32.0f);
        spectralShared.heavyDriveSafety = juce::jlimit(0.0f, 1.0f, (thresholdDriveDb - 16.0f) / 32.0f);
        spectralShared.extremeDriveSafety = juce::jlimit(0.0f, 1.0f, (thresholdDriveDb - 30.0f) / 18.0f);
        auto const sharedGrLimitDb = juce::jlimit(0.0f, 18.0f, effectiveGrLimitDb);
        spectralShared.hfGuardAmount = juce::jlimit(0.0f, 1.0f, effectiveHfGuard);
        spectralShared.smoothBias = spectralSmoothBias;
        spectralShared.adaptive = juce::jlimit(0.0f, 1.0f, effectiveAdaptiveRelease);
        spectralShared.transientProtect = blockSpectralTransientProtect;
        spectralShared.bassLock = juce::jlimit(0.0f, 1.0f, effectiveLowProtectAmount);
        spectralShared.lowCpuQuality = blockLowCpuSpectralQuality;
        spectralShared.upwardDriveGate =
            smoothSuppressionShape(juce::jlimit(0.0f, 1.0f, (thresholdDriveDb - 1.5f) / 14.0f), spectralSmoothBias);
        spectralShared.cleanLoudnessDrive =
            smoothSuppressionShape(juce::jlimit(0.0f, 1.0f, (thresholdDriveDb - 3.0f) / 18.0f), spectralSmoothBias);
        spectralShared.genreSustainScale = blockGenreSustainScale;
        spectralShared.genreUpwardStartOffsetDb = blockGenreUpwardStartOffsetDb;
        spectralShared.genreUpwardMaxBoostDb = blockGenreUpwardMaxBoostDb;
        spectralShared.genreUpwardSlopeBoost = blockGenreUpwardSlopeBoost;
        spectralShared.loudnessEfficiency =
            juce::jlimit(0.0f, 1.0f, spectralShared.driveAmount * spectralShared.upwardDriveGate
                                          * (0.34f + (mode.loudnessFocus * 0.60f) + (mode.densityLift * 0.46f))
                                          * spectralShared.genreSustainScale);
#if defined(PEAKEATER_SPECTRAL_2_VARIANT)
        spectralShared.spectral2DensityAssist =
            smoothSuppressionShape(juce::jlimit(0.0f, 1.0f, (thresholdDriveDb - 5.0f) / 20.0f), spectralSmoothBias)
            * juce::jlimit(0.0f, 1.0f, 0.34f + (mode.loudnessFocus * 0.48f) + (mode.densityLift * 0.32f));
#endif
        spectralShared.percussiveMode = blockPercussiveMode;
        spectralShared.fastAttack = juce::jmap(spectralShared.transientProtect, mSpectralFastAttackCoefficient, mSpectralAttackCoefficient);
        spectralShared.slowAttack = juce::jmap(spectralFastBias, mSpectralSlowAttackCoefficient, mSpectralAttackCoefficient);
        spectralShared.slowRelease = juce::jmap(spectralSmoothBias, mSpectralSlowReleaseCoefficient, mSpectralReleaseCoefficient);
        spectralShared.driveTargetRelax = 0.018f - (spectralShared.driveSafety * 0.0035f)
                                          - (spectralShared.heavyDriveSafety * 0.0020f)
                                          - (spectralShared.extremeDriveSafety * 0.0012f);
        spectralShared.spectralReductionLimit =
            std::min(mode.grLimitDb * (0.52f + spectralShared.driveSafety * 0.045f
                                       + spectralShared.heavyDriveSafety * 0.028f
                                       + spectralShared.extremeDriveSafety * 0.018f),
                     sharedGrLimitDb);
        spectralShared.upwardSmoothingBase = juce::jmap(spectralSmoothBias, 0.64f, 0.88f);
        spectralShared.reductionSmoothingBase = juce::jmap(spectralFastBias, 0.18f, 0.38f);
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
        if (mBandMultiplier > 4) {
            auto const sharedGainExponent = mSharedStereoLoudnessDb * 0.115129255f;
            spectralShared.stereoSustainGain = 1.0f + sharedGainExponent + (0.5f * sharedGainExponent * sharedGainExponent);
        }
#endif

        for (size_t channel = 0; channel < numChannels; ++channel) {
            auto& state = mChannelStates[channel];
            float limiterReductionDb = 0.0f;
            auto const limitedOutput = processLimiterBank(state, detectorInputs[channel], signalInputs[channel], mode, thresholdDriveDb,
                                                          mSmoothedCeilingDb, activeLimiterBands, effectiveAdaptiveRelease,
                                                          effectiveLowProtectAmount, effectiveGrLimitDb, effectivePunchProtect, effectiveReleaseShape,
                                                          lookaheadAmount, limiterHoldSamples, overshootReleaseCoefficient,
                                                          peakBudget, limiterReductionDb);
            blockGainReductionDb = std::max(blockGainReductionDb, limiterReductionDb);

            float spectralReductionDb = 0.0f;
            auto spectralOutput = processSpectralBank(state, detectorInputs[channel], limitedOutput, mode, thresholdDriveDb,
                                                      mSmoothedCeilingDb, effectiveSpectralDepth, activeSpectralBins,
                                                      peakBudget, spectralShared, spectralReductionDb);
            blockGainReductionDb = std::max(blockGainReductionDb, spectralReductionDb);

            auto spectralOutputDb = fastGainToDb(std::abs(spectralOutput));
            auto const peakReliefOverDb = std::max(0.0f, spectralOutputDb - mSmoothedCeilingDb);
            auto const peakReliefEvent = hermiteShape(juce::jlimit(0.0f, 1.0f, (peakReliefOverDb - 0.35f) / 7.5f));
            auto const peakReliefProtect = juce::jlimit(0.18f, 1.0f, 1.0f - (effectivePunchProtect * 0.34f) - (bassSafe * 0.30f));
            auto const oneShotPeakReliefDodge =
                (mMode == Mode::Drums || mMode == Mode::OneShot || mMode == Mode::OneShotClean || mMode == Mode::Percs
                 || mMode == Mode::DrumNBass)
                    ? juce::jlimit(0.18f, 1.0f, 1.0f - state.spectralTransientRisk * 0.72f)
                    : juce::jlimit(0.42f, 1.0f, 1.0f - state.spectralTransientRisk * 0.34f);
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
            auto const peakReliefStrength =
                juce::jlimit(0.0f, 0.12f, (qualitySafety * 0.030f) + (masteringQuality * 0.042f)
                                                  + (ultraRenderQuality * 0.020f) + (maxRenderQuality * 0.012f)
                                                  + (heavyDriveSafety * 0.020f) + (lookaheadPeakControl * 0.034f)
                                                  + (feedbackPressure * 0.018f) + (peakBudget.peakPacking * 0.014f));
#else
            auto const peakReliefStrength =
                juce::jlimit(0.0f, 0.085f, (qualitySafety * 0.028f) + (masteringQuality * 0.038f)
                                                   + (ultraRenderQuality * 0.018f) + (maxRenderQuality * 0.010f)
                                                   + (heavyDriveSafety * 0.018f) + (lookaheadPeakControl * 0.030f));
#endif
            auto const peakReliefAmount = peakReliefEvent * peakReliefProtect * oneShotPeakReliefDodge * peakReliefStrength;
            if (peakReliefAmount > 0.0001f) {
                auto const allpassCoefficient =
                    juce::jlimit(0.22f, 0.68f, 0.34f + (masteringQuality * 0.16f) + (peakReliefEvent * 0.10f));
                auto const relievedOutput = applySyncedAllpassPeakRelief(spectralOutput, peakReliefAmount, allpassCoefficient, state);
                if (std::abs(relievedOutput) < std::abs(spectralOutput)) {
                    blockClipAmountDb = std::max(blockClipAmountDb,
                                                 std::max(0.0f, spectralOutputDb - fastGainToDb(std::abs(relievedOutput))));
                    spectralOutput = relievedOutput;
                    spectralOutputDb = fastGainToDb(std::abs(spectralOutput));
                }
            }

#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
            // Jeannerot et al. (2022): spend a bounded correction budget only where
            // the local signal can mask it. This preconditions sparse sustained peaks
            // before clipping; transient and low-mono risk explicitly close the gate.
            auto const residualQuality = juce::jlimit(0.0f, 1.0f, (bandMultiplier - 1.0f) / 7.0f);
            auto const residualDriveGate = hermiteShape(juce::jlimit(0.0f, 1.0f, (thresholdDriveDb - 4.0f) / 18.0f));
            auto const residualModeScale = mMode == Mode::EDM ? 1.0f
                                           : mMode == Mode::Dubstep ? 0.90f
                                                                 : 0.0f;
            auto const residualAuthority =
                residualDriveGate * residualQuality * residualModeScale
                * juce::jlimit(0.0f, 0.72f, 0.12f + residualQuality * 0.30f
                                                + peakBudget.peakPacking * 0.18f
                                                + peakBudget.clipRelief * 0.14f)
                * juce::jlimit(0.30f, 1.0f, 1.0f - feedbackPressure * 0.42f);
#if defined(PEAKEATER_CPU_BENCHMARK)
            auto const enabledResidualAuthority = mMaskingResidualEnabled ? residualAuthority : 0.0f;
#else
            auto const enabledResidualAuthority = residualAuthority;
#endif
            auto const residualResult =
                applyMaskingShapedPeakResidual(spectralOutput, ceilingGain, enabledResidualAuthority,
                                               state.spectralTransientRisk, std::max(state.spectralLowRisk, peakBudget.lowSideRisk),
                                               state.spectralFlatness, state.spectralHighBandRatio,
                                               state.previousFinalClipInput, state);
            if (residualResult.reductionDb > 0.0f) {
                spectralOutput = residualResult.sample;
                spectralOutputDb = fastGainToDb(std::abs(spectralOutput));
                blockGainReductionDb = std::max(blockGainReductionDb, residualResult.reductionDb);
            }
#endif

            auto const preClipOverDb = std::max(0.0f, spectralOutputDb - mSmoothedCeilingDb);
            auto const overloadSafety = juce::jlimit(0.0f, 1.0f, (preClipOverDb - 1.5f) / 18.0f);
            auto const overloadClipRelief = juce::jlimit(0.0f, 1.0f, overloadSafety * (0.08f + qualitySafety * 0.12f
                                                                                       + masteringQuality * 0.08f));
            auto const crestClipRelief =
                peakBudget.crestMicroGr * juce::jlimit(0.025f, 0.12f, 0.045f + qualitySafety * 0.035f
                                                                        + masteringQuality * 0.025f);
            auto const peakPackingRelief =
                peakBudget.peakPacking * juce::jlimit(0.015f, 0.085f, 0.034f + driveSafety * 0.025f
                                                                       + qualitySafety * 0.018f);
            auto const clipBalanceRelief =
                peakBudget.clipRelief * juce::jlimit(0.04f, 0.16f, 0.17f - (typeClipKeep * 0.08f))
                + crestClipRelief + peakPackingRelief;
#if defined(PEAKEATER_SPECTRAL_2_VARIANT)
            auto const densityClipSupport =
                juce::jlimit(0.0f, 0.045f, (peakBudget.densityBudget * (0.018f + typeClipKeep * 0.035f)
                                            + peakBudget.sustainLift * (0.004f + typeClipKeep * 0.010f))
                                               * (1.0f - driveSafety * 0.28f)
                                               * (1.0f - peakBudget.peakPacking * 0.20f));
#else
            auto const densityClipSupport =
                juce::jlimit(0.0f, 0.08f, peakBudget.densityBudget * (0.03f + typeClipKeep * 0.06f)
                                             + peakBudget.sustainLift * (0.006f + typeClipKeep * 0.014f));
#endif
            auto const transientClipDodge =
                juce::jlimit(0.66f, 1.0f, 1.0f - (state.spectralTransientRisk * (0.085f + peakBudget.transientReserve * 0.115f
                                                                                  + effectivePunchProtect * 0.085f)));
            auto const transparentMode = mMode == Mode::Clean || mMode == Mode::Acoustic || mMode == Mode::Vocal;
            auto const cleanOverloadRound =
                transparentMode ? juce::jlimit(0.0f, 0.16f,
                                               overloadSafety
                                                   * (0.030f + (driveSafety * 0.050f) + (heavyDriveSafety * 0.040f)
                                                      + (qualitySafety * 0.025f))
                                                   * juce::jlimit(0.74f, 1.0f, 1.0f - state.spectralTransientRisk * 0.16f))
                                : 0.0f;
            auto const sampleFinalClip =
                juce::jlimit(0.0f, 1.0f,
                             (effectiveFinalClipClosedLoop * transientClipDodge
                              * juce::jlimit(0.62f, 1.08f,
                                             1.0f - overloadClipRelief - clipBalanceRelief + densityClipSupport))
                                  + cleanOverloadRound);
            auto const finalClipNeedsWork = sampleFinalClip > 0.0008f || preClipOverDb > 0.18f;
            auto finalClipResult =
                finalClipNeedsWork ? applyFinalClip(spectralOutput, ceilingGain, sampleFinalClip, state.previousFinalClipInput)
                                   : ClipResult{spectralOutput, 0.0f};
            auto const clipInterpolationQuality = juce::jlimit(0.0f, 1.0f, std::max(nonlinearQuality, peakBudget.roundedClipQuality * 0.58f));
            auto const clipNeedsInterpolation =
                finalClipNeedsWork && sampleFinalClip > 0.0015f
                && (hqNonlinearInterpolation || (bandMultiplier >= 8 && clipInterpolationQuality > 0.28f))
                && std::max(std::abs(state.previousFinalClipInput), std::abs(spectralOutput)) > 0.18f;
            if (clipNeedsInterpolation) {
                auto const clipMidpoint = (state.previousFinalClipInput + spectralOutput) * 0.5f;
                auto const midpointResult = applyFinalClip(clipMidpoint, ceilingGain, sampleFinalClip, state.previousFinalClipInput);
                finalClipResult.sample += (midpointResult.sample - finalClipResult.sample) * clipInterpolationQuality * 0.24f;
                finalClipResult.reductionDb = std::max(finalClipResult.reductionDb, midpointResult.reductionDb);
                if (ultraRenderQuality > 0.0001f && sampleFinalClip > 0.018f && preClipOverDb > 0.35f) {
                    auto const quarterInput = state.previousFinalClipInput + ((spectralOutput - state.previousFinalClipInput) * 0.25f);
                    auto const threeQuarterInput = state.previousFinalClipInput + ((spectralOutput - state.previousFinalClipInput) * 0.75f);
                    auto const quarterResult = applyFinalClip(quarterInput, ceilingGain, sampleFinalClip, state.previousFinalClipInput);
                    auto const threeQuarterResult = applyFinalClip(threeQuarterInput, ceilingGain, sampleFinalClip, clipMidpoint);
                    auto const predictiveSample = (quarterResult.sample + midpointResult.sample + threeQuarterResult.sample + finalClipResult.sample) * 0.25f;
                    finalClipResult.sample += (predictiveSample - finalClipResult.sample) * juce::jlimit(0.0f, 0.22f, ultraRenderQuality * 0.16f
                                                                                                                        + maxRenderQuality * 0.06f);
                    finalClipResult.reductionDb = std::max(finalClipResult.reductionDb,
                                                           std::max(quarterResult.reductionDb, threeQuarterResult.reductionDb));
                }
            }
            if (bassRecover > 0.0001f) {
                state.bassRecoverPreClipLowpass += saturationSplitCoefficient * (spectralOutput - state.bassRecoverPreClipLowpass);
                state.bassRecoverPostClipLowpass += saturationSplitCoefficient * (finalClipResult.sample - state.bassRecoverPostClipLowpass);
                auto const clippedLowLoss = state.bassRecoverPreClipLowpass - state.bassRecoverPostClipLowpass;
                auto const lowClipPressure = juce::jlimit(0.0f, 1.0f, (finalClipResult.reductionDb / 5.5f) + (preClipOverDb / 18.0f));
                auto const lowRecoverAmount =
                    bassRecover * juce::jlimit(0.0f, 0.34f, 0.08f + (lowClipPressure * 0.22f) + (driveAmount * 0.08f))
                    * juce::jlimit(0.45f, 1.0f, 1.0f - state.spectralTransientRisk * 0.28f);
                finalClipResult.sample += clippedLowLoss * lowRecoverAmount;
            } else {
                state.bassRecoverPreClipLowpass = spectralOutput;
                state.bassRecoverPostClipLowpass = finalClipResult.sample;
            }
            state.previousFinalClipInput = spectralOutput;
            blockClipAmountDb = std::max(blockClipAmountDb, finalClipResult.reductionDb);

            auto const ceilingOverDb = std::max(0.0f, fastGainToDb(std::abs(finalClipResult.sample)) - mSmoothedCeilingDb);
            auto const emergencyCeilingGrLimitDb =
                juce::jlimit(0.0f, 24.0f, effectiveGrLimitDb + (qualitySafety * 1.8f) + (masteringQuality * 2.5f)
                                             + (heavyDriveSafety * 2.4f) + (extremeDriveSafety * 3.2f)
                                             + (juce::jlimit(0.0f, 1.0f, ceilingOverDb / 24.0f) * 8.0f));
            auto const emergencySafetyMarginOffsetDb = style.safetyMarginOffsetDb + (overloadSafety * (0.035f + qualitySafety * 0.08f));
            auto const emergencyLimiterStrengthScale =
                style.limiterStrengthScale * (1.0f + (qualitySafety * 0.05f) + (masteringQuality * 0.08f)
                                              + (ultraRenderQuality * 0.055f) + (maxRenderQuality * 0.035f)
                                              + (overloadSafety * 0.14f) + (lookaheadPeakControl * 0.16f));
            auto const truePeakGuardEnabled = mTruePeakLimit || bandMultiplier >= 4 || heavyDriveSafety > 0.14f || lookaheadPeakControl > 0.020f
                                              || preClipOverDb > 1.25f || finalClipResult.reductionDb > 0.35f
                                              || ceilingOverDb > 0.10f;
            auto const automaticTruePeakMargin = truePeakGuardEnabled && !mTruePeakLimit ? 0.035f + (lookaheadPeakControl * 0.040f) : 0.0f;
            auto const closedLoopTruePeakMargin = feedback.ceilingPressure * juce::jlimit(0.0f, 0.055f, 0.018f + qualitySafety * 0.030f);
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
            constexpr auto rmsContourVariantScale = 1.75f;
            auto const baseRmsContourAmount = mMode == Mode::EDM ? 0.670f
                                              : mMode == Mode::Percs ? 0.600f
                                                                     : 0.630f;
            auto const maxRmsContourAmount = baseRmsContourAmount + (mode.spectralGenreLift * 0.08f);
            auto const spectralDensityDelivery =
                juce::jlimit(0.0f, 0.55f,
                             ((mode.spectralSustainScale - 1.0f) * 1.20f)
                                 + (mode.spectralUpwardStartOffsetDb * 0.25f)
                                 + ((mode.spectralMidFocusScale - 1.0f) * 0.40f)
                                 + (mode.spectralGenreLift * 0.10f));
#elif defined(PEAKEATER_SPECTRAL_2_VARIANT)
            constexpr auto rmsContourVariantScale = 0.58f;
            constexpr auto maxRmsContourAmount = 0.30f;
            constexpr auto spectralDensityDelivery = 0.0f;
#else
            constexpr auto rmsContourVariantScale = 0.46f;
            constexpr auto maxRmsContourAmount = 0.30f;
            constexpr auto spectralDensityDelivery = 0.0f;
#endif
            auto const rmsContourDriveGate =
                hermiteShape(juce::jlimit(0.0f, 1.0f, (thresholdDriveDb - 1.0f) / 8.0f));
            auto const rmsContourModeScale = loudLiftMode ? 1.0f
                                             : cleanLiftTransparentMode ? 0.64f
                                             : bassLiftMode ? 0.78f
                                             : percussiveLiftMode ? 0.56f
                                                                  : 0.68f;
            auto const rmsContourSafety =
                juce::jlimit(0.30f, 1.0f,
                             (1.0f - feedbackPressure * 0.42f)
                                 * (1.0f - feedback.transientRisk * 0.30f)
                                 * (1.0f - feedback.lowMonoRisk * 0.15f));
            auto const rmsContourAmount =
                juce::jlimit(0.0f, maxRmsContourAmount,
                             ((driveAmount * 0.58f) + (peakBudget.loudnessDistribution * 0.46f)
                              + (peakBudget.sustainLift * 0.38f) + (feedback.densitySuccess * 0.10f)
                              + spectralDensityDelivery)
                                 * rmsContourVariantScale * rmsContourModeScale * rmsContourSafety * rmsContourDriveGate
                                 * juce::jlimit(0.92f, 1.0f, 0.93f + qualitySafety * 0.04f + masteringQuality * 0.03f));
            auto const ceilingResult = applyCeiling(finalClipResult.sample, ceilingGain, mode.ceilingReleaseMs, mode, state, mSampleRate,
                                                    truePeakGuardEnabled,
                                                    juce::jlimit(0.0f, 1.0f, effectiveTruePeakMarginDb + automaticTruePeakMargin
                                                                                         + closedLoopTruePeakMargin
                                                                                         + (ultraRenderQuality * 0.035f)
                                                                                         + (maxRenderQuality * 0.025f)),
                                                    effectiveAdaptiveRelease, effectivePunchProtect,
                                                    effectiveReleaseShape, emergencySafetyMarginOffsetDb, emergencyLimiterStrengthScale,
                                                    emergencyCeilingGrLimitDb, rmsContourAmount,
                                                    feedbackAttack, feedbackRelease);
            blockGainReductionDb = std::max(blockGainReductionDb, ceilingResult.reductionDb);
            auto& feedbackState = state.peakBudgetFeedback;
            auto const clipPressureTarget =
                juce::jlimit(0.0f, 1.0f, (finalClipResult.reductionDb / 5.5f) + (preClipOverDb / 18.0f) + (overloadSafety * 0.20f));
            auto const ceilingPressureTarget =
                juce::jlimit(0.0f, 1.0f, (ceilingResult.reductionDb / 7.0f) + (ceilingOverDb / 16.0f));
            auto const upwardNorm = juce::jlimit(0.0f, 1.0f, state.spectralUpwardAppliedDb / 4.5f);
            auto const pressureTarget = juce::jlimit(0.0f, 1.0f, (clipPressureTarget * 0.62f) + (ceilingPressureTarget * 0.38f));
            auto const transientRiskTarget =
                juce::jlimit(0.0f, 1.0f, state.spectralTransientRisk + (limiterReductionDb > 2.5f ? 0.10f : 0.0f));
            auto const lowRiskTarget = juce::jlimit(0.0f, 1.0f, std::max(feedbackState.lowMonoRisk, state.spectralLowRisk));
            feedbackState.clipPressure = smoothFeedback(feedbackState.clipPressure, clipPressureTarget);
            feedbackState.ceilingPressure = smoothFeedback(feedbackState.ceilingPressure, ceilingPressureTarget);
            feedbackState.sustainClipDebt = smoothFeedback(feedbackState.sustainClipDebt, upwardNorm * pressureTarget);
            feedbackState.transientRisk = smoothFeedback(feedbackState.transientRisk, transientRiskTarget);
            feedbackState.lowMonoRisk = smoothFeedback(feedbackState.lowMonoRisk, lowRiskTarget);
            feedbackState.densitySuccess =
                smoothFeedback(feedbackState.densitySuccess, upwardNorm * (1.0f - pressureTarget) * (1.0f - transientRiskTarget * 0.55f)
                                                                 * (1.0f - lowRiskTarget * 0.35f));
            auto outputSample = ceilingResult.sample;
            if (deltaListening) {
                switch (mDeltaSource) {
                    case DeltaSource::Limiter:
                        outputSample = signalInputs[channel] - limitedOutput;
                        break;
                    case DeltaSource::Spectral:
                        outputSample = limitedOutput - spectralOutput;
                        break;
                    case DeltaSource::SatClip:
                        outputSample = (preSaturationInputs[channel] - signalInputs[channel]) + (spectralOutput - finalClipResult.sample);
                        break;
                    case DeltaSource::Ceiling:
                        outputSample = finalClipResult.sample - ceilingResult.sample;
                        break;
                    case DeltaSource::All:
                    default:
                        outputSample = preSaturationInputs[channel] - ceilingResult.sample;
                        break;
                }
            }
            processedOutputs[channel] = outputSample;
        }

#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
        if (mBandMultiplier > 4 && numChannels > 1 && !deltaListening && spectralShared.stereoSustainGain > 1.0f) {
            auto const outputPeak = std::max(std::abs(processedOutputs[0]), std::abs(processedOutputs[1]));
            constexpr auto sharedGainSafety = 0.99655208f;
            auto sharedOutputGain = spectralShared.stereoSustainGain;
            auto const sharedOutputCeiling = ceilingGain * sharedGainSafety;
            if (outputPeak > 1.0e-7f && outputPeak * sharedOutputGain > sharedOutputCeiling) {
                sharedOutputGain = std::max(1.0f, sharedOutputCeiling / outputPeak);
            }
            processedOutputs[0] *= sharedOutputGain;
            processedOutputs[1] *= sharedOutputGain;
        }
#endif

        if (numChannels > 1 && !deltaListening) {
            auto const outMid = (processedOutputs[0] + processedOutputs[1]) * 0.5f;
            auto const outSide = (processedOutputs[0] - processedOutputs[1]) * 0.5f;
            auto const outputSideRatio =
                juce::jlimit(0.0f, 1.0f, std::abs(outSide) / std::max(0.000001f, std::abs(outMid) + std::abs(outSide)));
            auto const sidePeakRisk =
                hermiteShape(juce::jlimit(0.0f, 1.0f, (fastGainToDb(std::abs(outSide)) - mSmoothedCeilingDb + 1.5f) / 10.0f));
            auto const outputLowSideRisk =
                juce::jlimit(0.0f, 1.0f, std::max(midSideBudget.lowSideRisk, outputSideRatio * effectiveLowProtectAmount));
            auto const sideSafety =
                outputLowSideRisk * (0.045f + peakBudget.lowReserve * 0.075f + bassSafe * 0.035f)
                + sidePeakRisk * (0.020f + peakBudget.sideClipRelief * 0.035f);
            auto const protectedSideScale =
                juce::jlimit(0.88f, 1.0f, 1.0f - sideSafety * juce::jlimit(0.72f, 1.0f, peakBudget.widthPreserve));
            auto const safeSide = outSide * protectedSideScale;
            processedOutputs[0] = outMid + safeSide;
            processedOutputs[1] = outMid - safeSide;
        }

        for (size_t channel = 0; channel < numChannels; ++channel) {
            samplePointers[channel][sampleIndex] = processedOutputs[channel];
        }
    }

    if (blockPeak <= 1.0e-8f) {
        reset();
        mPreviousBlockWasSilent = true;
        mClipAmountDb = 0.0f;
        mGainReductionDb = 0.0f;
        return;
    }

    mClipAmountDb = blockClipAmountDb;
    mGainReductionDb = blockGainReductionDb;
}

SpectralMaximizer::ModeSettings SpectralMaximizer::getModeSettings(Mode mode) {
    switch (mode) {
        case Mode::EDM:
            return {{{{2.85f, 1.05f, 64.0f, 0.84f, -0.3f, 1.05f}, {2.35f, 0.68f, 52.0f, 0.78f, 0.1f, 0.88f},
                      {3.15f, 0.36f, 38.0f, 1.02f, -1.0f, 1.20f}}},
                    1.36f, 4.0f, 3.9f, 46.0f, 11.8f, 0.88f, 1.40f, 0.96f, 0.42f, 0.56f, 56.0f, 0.62f, 0.82f, 8.6f, 1.18f, 0.72f, 0.50f, 0.00f};
        case Mode::HipHop:
            return {{{{2.55f, 4.2f, 142.0f, 0.98f, 0.8f, 0.68f}, {2.05f, 1.8f, 96.0f, 0.66f, 0.1f, 0.82f},
                      {2.45f, 0.72f, 66.0f, 0.74f, -0.6f, 0.98f}}},
                    0.82f, 5.4f, 3.0f, 88.0f, 9.6f, 0.84f, 1.25f, 0.76f, 0.26f, 0.56f, 82.0f, 0.32f, 0.82f, 7.4f, 1.06f, 0.42f, 0.42f, 0.01f};
        case Mode::Drums:
            return {{{{2.15f, 0.36f, 58.0f, 0.70f, 0.4f, 0.92f}, {2.85f, 0.24f, 44.0f, 0.94f, -0.3f, 1.18f},
                      {3.65f, 0.14f, 32.0f, 1.06f, -1.15f, 1.36f}}},
                    0.66f, 2.70f, 4.2f, 34.0f, 10.0f, 1.30f, 1.12f, 0.58f, 0.40f, 0.38f, 118.0f, 0.46f, 0.58f, 7.2f, 1.16f, 0.42f, 0.24f, 0.00f};
        case Mode::OneShot:
            return {{{{2.0f, 0.16f, 40.0f, 0.64f, 0.7f, 0.88f}, {2.5f, 0.12f, 32.0f, 0.84f, -0.1f, 1.08f},
                      {3.25f, 0.08f, 24.0f, 0.98f, -0.9f, 1.28f}}},
                    0.34f, 2.15f, 5.0f, 24.0f, 9.0f, 1.30f, 1.08f, 0.52f, 0.48f, 0.34f, 142.0f, 0.42f, 0.42f, 6.4f, 1.24f, 0.48f, 0.30f, 0.00f};
        case Mode::OneShotClean:
            return {{{{1.62f, 0.34f, 58.0f, 0.54f, 1.1f, 0.82f}, {1.92f, 0.24f, 46.0f, 0.66f, 0.5f, 0.98f},
                      {2.28f, 0.16f, 36.0f, 0.78f, -0.25f, 1.16f}}},
                    0.10f, 4.8f, 4.7f, 54.0f, 6.2f, 1.34f, 0.92f, 0.12f, 0.035f, 0.28f, 96.0f, 0.58f, 0.72f, 4.8f,
                    1.08f, 0.20f, 0.08f, 0.10f};
        case Mode::Percs:
            return {{{{1.85f, 0.24f, 50.0f, 0.52f, 0.9f, 0.86f}, {2.95f, 0.18f, 40.0f, 0.96f, -0.2f, 1.22f},
                      {3.85f, 0.12f, 30.0f, 1.08f, -1.35f, 1.38f}}},
                    0.56f, 2.35f, 4.4f, 26.0f, 9.8f, 1.30f, 1.14f, 0.58f, 0.36f, 0.42f, 155.0f, 0.44f, 0.50f, 7.1f, 1.20f, 0.56f, 0.28f, 0.00f};
        case Mode::Dubstep:
            return {{{{3.15f, 3.4f, 148.0f, 1.02f, -0.2f, 0.60f}, {2.75f, 0.72f, 68.0f, 0.90f, -0.5f, 0.98f},
                      {3.05f, 0.34f, 46.0f, 0.86f, -0.85f, 1.10f}}},
                    1.10f, 4.6f, 3.8f, 64.0f, 11.0f, 0.88f, 1.34f, 0.90f, 0.36f, 0.58f, 60.0f, 0.42f, 0.88f, 8.3f, 1.12f, 0.66f, 0.58f, 0.01f};
        case Mode::DrumNBass:
            return {{{{2.75f, 0.9f, 66.0f, 0.86f, 0.1f, 0.92f}, {2.55f, 0.48f, 52.0f, 0.90f, 0.0f, 1.0f},
                      {3.45f, 0.20f, 34.0f, 1.04f, -1.2f, 1.30f}}},
                    1.12f, 3.35f, 4.3f, 38.0f, 11.0f, 1.12f, 1.34f, 0.86f, 0.36f, 0.50f, 78.0f, 0.56f, 0.74f, 8.2f, 1.20f, 0.74f, 0.44f, 0.00f};
        case Mode::House:
            return {{{{2.35f, 1.7f, 90.0f, 0.82f, 0.2f, 0.84f}, {2.05f, 0.95f, 72.0f, 0.78f, 0.0f, 0.88f},
                      {2.65f, 0.42f, 50.0f, 0.88f, -0.75f, 1.06f}}},
                    0.78f, 4.4f, 3.2f, 64.0f, 9.4f, 0.86f, 1.20f, 0.66f, 0.18f, 0.54f, 68.0f, 0.58f, 0.90f, 7.0f, 1.04f, 0.54f, 0.38f, 0.01f};
        case Mode::Trap:
            return {{{{2.95f, 3.8f, 150.0f, 1.02f, -0.1f, 0.58f}, {2.25f, 1.15f, 82.0f, 0.76f, 0.0f, 0.82f},
                      {3.05f, 0.32f, 48.0f, 0.94f, -1.0f, 1.20f}}},
                    0.98f, 4.9f, 3.6f, 76.0f, 10.5f, 0.92f, 1.30f, 0.82f, 0.32f, 0.56f, 68.0f, 0.38f, 0.90f, 7.9f, 1.12f, 0.62f, 0.50f, 0.01f};
        case Mode::Kick808:
            return {{{{3.45f, 4.6f, 185.0f, 1.10f, -0.5f, 0.50f}, {1.75f, 2.4f, 118.0f, 0.44f, 0.8f, 0.54f},
                      {1.45f, 1.0f, 76.0f, 0.22f, 2.0f, 0.42f}}},
                    0.68f, 6.6f, 2.2f, 156.0f, 8.1f, 0.66f, 1.08f, 0.46f, 0.10f, 0.40f, 34.0f, 0.72f, 1.0f, 6.4f, 0.96f, 0.20f, 0.20f, 0.05f};
        case Mode::Acoustic:
            return {{{{1.32f, 16.0f, 245.0f, 0.38f, 1.2f, 0.42f}, {1.52f, 10.0f, 198.0f, 0.54f, 0.6f, 0.54f},
                      {1.70f, 5.6f, 150.0f, 0.54f, 0.2f, 0.62f}}},
                    0.0f, 9.4f, 1.55f, 180.0f, 4.8f, 0.40f, 0.62f, 0.16f, 0.01f, 0.72f, 32.0f, 0.92f, 1.0f, 3.6f, 0.72f, 0.08f, 0.03f, 0.14f};
        case Mode::Vocal:
            return {{{{1.42f, 7.5f, 175.0f, 0.36f, 1.1f, 0.44f}, {2.28f, 2.7f, 110.0f, 1.0f, -0.8f, 0.86f},
                      {2.35f, 1.1f, 84.0f, 0.88f, -0.5f, 1.04f}}},
                    0.2f, 6.4f, 2.8f, 92.0f, 7.4f, 0.70f, 0.90f, 0.34f, 0.04f, 0.70f, 110.0f, 0.62f, 0.94f, 5.8f, 0.96f, 0.42f, 0.14f, 0.05f};
        case Mode::Bass:
            return {{{{3.15f, 5.0f, 170.0f, 1.02f, -0.4f, 0.54f}, {1.72f, 2.8f, 108.0f, 0.46f, 0.9f, 0.54f},
                      {1.42f, 1.0f, 78.0f, 0.26f, 2.0f, 0.44f}}},
                    0.62f, 6.1f, 2.25f, 132.0f, 8.2f, 0.58f, 1.08f, 0.56f, 0.18f, 0.54f, 42.0f, 0.68f, 0.92f, 6.4f, 0.98f, 0.18f, 0.26f, 0.03f};
        case Mode::Bright:
            return {{{{1.40f, 5.2f, 130.0f, 0.32f, 1.8f, 0.42f}, {1.90f, 2.4f, 88.0f, 0.62f, 0.5f, 0.70f},
                      {3.35f, 0.28f, 44.0f, 1.02f, -1.7f, 1.24f}}},
                    0.44f, 3.6f, 3.9f, 54.0f, 8.8f, 0.82f, 0.98f, 0.44f, 0.06f, 0.58f, 96.0f, 0.64f, 0.88f, 6.4f, 1.00f, 0.78f, 0.20f, 0.035f};
        case Mode::Glue:
            return {{{{1.80f, 16.0f, 270.0f, 0.62f, 0.7f, 0.44f}, {1.92f, 11.5f, 230.0f, 0.74f, 0.1f, 0.54f},
                      {1.82f, 7.4f, 180.0f, 0.64f, 0.4f, 0.58f}}},
                    0.0f, 8.5f, 1.8f, 184.0f, 6.6f, 0.54f, 0.84f, 0.32f, 0.04f, 0.72f, 48.0f, 0.88f, 1.0f, 5.3f, 0.86f, 0.20f, 0.12f, 0.07f};
        case Mode::Clean:
            return {{{{1.25f, 24.0f, 320.0f, 0.30f, 1.8f, 0.32f}, {1.34f, 18.0f, 280.0f, 0.40f, 1.0f, 0.38f},
                      {1.44f, 11.0f, 225.0f, 0.40f, 0.8f, 0.44f}}},
                    0.0f, 11.8f, 1.0f, 250.0f, 4.0f, 0.34f, 0.62f, 0.10f, 0.0f, 0.68f, 22.0f, 0.96f, 1.0f, 3.0f, 0.72f, 0.02f, 0.00f, 0.18f};
    }

    return getModeSettings(Mode::EDM);
}

SpectralMaximizer::ModeSettings SpectralMaximizer::qualityAdjustedMode(Mode mode, ModeSettings settings, float quality, size_t bandMultiplier) {
    auto const q = juce::jlimit(0.0f, 1.0f, quality);
    auto const ecoQuality = bandMultiplier <= 1;
    auto const highQuality = juce::jlimit(0.0f, 1.0f, (q - 0.35f) / 0.65f);
    auto const steppedSafety = bandMultiplier >= 32 ? 0.72f
                             : bandMultiplier >= 16 ? 0.56f
                             : bandMultiplier >= 8 ? 0.40f
                             : bandMultiplier >= 4 ? 0.22f
                                                   : 0.0f;
    auto const highSafety = std::max(highQuality, steppedSafety);
    auto const masterSafety = bandMultiplier >= 8 ? juce::jlimit(0.0f, 1.0f, (static_cast<float>(bandMultiplier) - 4.0f) / 28.0f)
                                                  : 0.0f;
    auto const ultraSafety = bandMultiplier >= 16 ? juce::jlimit(0.0f, 1.0f, (static_cast<float>(bandMultiplier) - 8.0f) / 24.0f)
                                                  : 0.0f;
    auto const ecoBias = 1.0f - q;
    auto bandRatioScale = 1.0f;
    auto bandAttackScale = 1.0f;
    auto bandReleaseScale = 1.0f;
    auto bandWeightScale = 1.0f;

    settings.kneeDb *= 1.0f + (highSafety * 0.06f) + (masterSafety * 0.03f);
    settings.finalLimiterStrength *= 0.98f + (highSafety * 0.18f) + (masterSafety * 0.15f) + (ultraSafety * 0.10f);
    settings.spectralControlScale *= 0.94f + (q * 0.018f) + (highSafety * 0.006f) + (masterSafety * 0.004f);
    settings.safetyMarginDb += (highSafety * 0.035f) + (masterSafety * 0.030f) + (ultraSafety * 0.020f);
    settings.maxBandReductionDb += (highSafety * 0.30f) + (masterSafety * 0.25f) + (ultraSafety * 0.15f);

    switch (mode) {
        case Mode::EDM:
            settings.thresholdDriveScale *= 0.98f + (highQuality * 0.08f);
            settings.softClipAmount *= 0.78f + (q * 0.24f);
            settings.hardClipBlend += highQuality * 0.020f;
            settings.spectralControlScale *= 0.97f + (highQuality * 0.018f);
            settings.loudnessFocus *= 1.04f + (highQuality * 0.20f);
            settings.densityLift *= 0.98f + (highQuality * 0.26f);
            settings.safetyMarginDb += highQuality * 0.050f;
            settings.finalLimiterStrength *= 1.0f + (highQuality * 0.070f);
            bandAttackScale = 0.92f - (highQuality * 0.06f);
            bandReleaseScale = 0.96f - (highQuality * 0.08f);
            bandWeightScale = 0.96f + (highQuality * 0.16f);
            break;
        case Mode::House:
            settings.thresholdDriveScale *= 0.96f + (highQuality * 0.06f);
            settings.softClipAmount *= 0.80f + (q * 0.26f);
            settings.hardClipBlend += highQuality * 0.025f;
            settings.spectralControlScale *= 0.97f + (highQuality * 0.012f);
            settings.loudnessFocus *= 1.0f + (highQuality * 0.14f);
            settings.densityLift *= 0.96f + (highQuality * 0.16f);
            settings.safetyMarginDb += highQuality * 0.030f;
            bandAttackScale = 0.94f - (highQuality * 0.04f);
            bandReleaseScale = 0.98f - (highQuality * 0.05f);
            bandWeightScale = 0.94f + (highQuality * 0.10f);
            break;
        case Mode::HipHop:
            settings.thresholdDriveScale *= 0.96f + (highQuality * 0.05f);
            settings.softClipAmount *= 0.76f + (q * 0.28f);
            settings.hardClipBlend += highQuality * 0.025f;
            settings.lowDetectorScale *= 0.90f - (highQuality * 0.08f);
            settings.stereoLink += highQuality * 0.12f;
            settings.densityLift *= 1.0f + (highQuality * 0.18f);
            settings.safetyMarginDb += highQuality * 0.045f;
            bandAttackScale = 1.04f;
            bandReleaseScale = 1.02f + (highQuality * 0.08f);
            bandWeightScale = 0.92f + (highQuality * 0.10f);
            break;
        case Mode::Drums:
            settings.thresholdDriveScale *= 0.935f + (highQuality * 0.06f);
            settings.softClipAmount *= 0.78f + (q * 0.12f);
            settings.spectralControlScale *= 0.98f + (highQuality * 0.008f);
            settings.transientScale *= 1.10f + (ecoBias * 0.08f);
            settings.loudnessFocus *= 0.91f + (highQuality * 0.06f);
            settings.densityLift *= 0.92f + (highQuality * 0.10f);
            settings.safetyMarginDb += 0.020f + (highQuality * 0.035f);
            bandAttackScale = 0.86f - (highQuality * 0.04f);
            bandReleaseScale = 0.90f - (highQuality * 0.05f);
            bandWeightScale = 0.90f + (highQuality * 0.08f);
            break;
        case Mode::OneShot:
            settings.thresholdDriveScale *= 0.88f + (highQuality * 0.03f);
            settings.softClipAmount *= 0.70f + (q * 0.10f);
            settings.transientScale *= 1.06f + (ecoBias * 0.08f);
            settings.spectralControlScale *= 0.98f + (highQuality * 0.008f);
            settings.loudnessFocus *= 0.88f + (highQuality * 0.05f);
            settings.densityLift *= 0.88f + (highQuality * 0.08f);
            settings.safetyMarginDb += 0.030f + (highQuality * 0.040f);
            bandAttackScale = 0.82f - (highQuality * 0.03f);
            bandReleaseScale = 0.86f - (highQuality * 0.03f);
            bandWeightScale = 0.88f + (highQuality * 0.06f);
            break;
        case Mode::OneShotClean:
            settings.thresholdDriveScale *= 0.90f + (highQuality * 0.025f);
            settings.softClipAmount *= 0.34f + (q * 0.05f);
            settings.hardClipBlend *= 0.42f;
            settings.transientScale *= 1.10f + (ecoBias * 0.08f);
            settings.spectralControlScale *= 0.94f + (highQuality * 0.01f);
            settings.loudnessFocus *= 0.84f + (highQuality * 0.04f);
            settings.densityLift *= 0.78f + (highQuality * 0.06f);
            settings.finalLimiterStrength *= 1.03f + (highQuality * 0.045f);
            settings.safetyMarginDb += 0.045f + (highQuality * 0.055f);
            bandAttackScale = 0.88f - (highQuality * 0.02f);
            bandReleaseScale = 0.94f + (highQuality * 0.05f);
            bandWeightScale = 0.80f + (highQuality * 0.06f);
            break;
        case Mode::Percs:
            settings.thresholdDriveScale *= 0.93f + (highQuality * 0.05f);
            settings.softClipAmount *= 0.78f + (q * 0.18f);
            settings.hardClipBlend += highQuality * 0.025f;
            settings.transientScale *= 1.18f + (ecoBias * 0.10f);
            settings.spectralControlScale *= 0.94f + (highQuality * 0.014f);
            settings.loudnessFocus *= 0.96f + (highQuality * 0.12f);
            bandAttackScale = 0.72f - (highQuality * 0.05f);
            bandReleaseScale = 0.74f - (highQuality * 0.05f);
            bandWeightScale = 0.92f + (highQuality * 0.10f);
            break;
        case Mode::Dubstep:
            settings.thresholdDriveScale *= 0.96f + (highQuality * 0.06f);
            settings.softClipAmount *= 0.80f + (q * 0.22f);
            settings.hardClipBlend += highQuality * 0.018f;
            settings.lowDetectorScale *= 0.86f - (highQuality * 0.08f);
            settings.stereoLink += 0.08f + (highQuality * 0.12f);
            settings.densityLift *= 1.06f + (highQuality * 0.20f);
            settings.loudnessFocus *= 1.0f + (highQuality * 0.08f);
            settings.safetyMarginDb += highQuality * 0.058f;
            settings.finalLimiterStrength *= 1.0f + (highQuality * 0.075f);
            bandAttackScale = 0.98f;
            bandReleaseScale = 1.02f + (highQuality * 0.08f);
            bandWeightScale = 0.94f + (highQuality * 0.12f);
            break;
        case Mode::Trap:
            settings.thresholdDriveScale *= 0.96f + (highQuality * 0.06f);
            settings.softClipAmount *= 0.78f + (q * 0.22f);
            settings.hardClipBlend += highQuality * 0.018f;
            settings.lowDetectorScale *= 0.84f - (highQuality * 0.08f);
            settings.stereoLink += 0.08f + (highQuality * 0.11f);
            settings.densityLift *= 1.06f + (highQuality * 0.20f);
            settings.loudnessFocus *= 1.0f + (highQuality * 0.07f);
            settings.safetyMarginDb += highQuality * 0.058f;
            settings.finalLimiterStrength *= 1.0f + (highQuality * 0.075f);
            bandAttackScale = 1.00f;
            bandReleaseScale = 1.04f + (highQuality * 0.08f);
            bandWeightScale = 0.92f + (highQuality * 0.12f);
            break;
        case Mode::DrumNBass:
            settings.thresholdDriveScale *= 0.97f + (highQuality * 0.07f);
            settings.softClipAmount *= 0.80f + (q * 0.22f);
            settings.hardClipBlend += highQuality * 0.018f;
            settings.spectralControlScale *= 0.95f + (highQuality * 0.016f);
            settings.transientScale *= 1.08f + (ecoBias * 0.06f);
            settings.loudnessFocus *= 1.06f + (highQuality * 0.18f);
            settings.densityLift *= 0.98f + (highQuality * 0.20f);
            settings.safetyMarginDb += highQuality * 0.052f;
            settings.finalLimiterStrength *= 1.0f + (highQuality * 0.068f);
            bandAttackScale = 0.84f - (highQuality * 0.05f);
            bandReleaseScale = 0.88f - (highQuality * 0.06f);
            bandWeightScale = 0.96f + (highQuality * 0.14f);
            break;
        case Mode::Acoustic:
            settings.thresholdDriveScale *= 0.92f;
            settings.softClipAmount *= 0.34f - (highQuality * 0.08f);
            settings.hardClipBlend *= 0.45f;
            settings.spectralControlScale *= 0.92f + (highQuality * 0.04f);
            settings.lowDetectorScale *= 0.96f;
            settings.stereoLink += highQuality * 0.04f;
            settings.safetyMarginDb += highQuality * 0.08f;
            bandRatioScale = 0.92f;
            bandAttackScale = 1.16f;
            bandReleaseScale = 1.18f + (highQuality * 0.12f);
            bandWeightScale = 0.82f + (highQuality * 0.08f);
            break;
        case Mode::Vocal:
            settings.thresholdDriveScale *= 0.92f + (highQuality * 0.03f);
            settings.softClipAmount *= 0.42f + (q * 0.06f);
            settings.spectralControlScale *= 0.96f + (highQuality * 0.06f);
            settings.loudnessFocus *= 0.94f + (highQuality * 0.10f);
            settings.safetyMarginDb += highQuality * 0.08f;
            bandAttackScale = 1.02f;
            bandReleaseScale = 1.05f + (highQuality * 0.10f);
            bandWeightScale = 0.90f + (highQuality * 0.14f);
            break;
        case Mode::Bass:
            settings.thresholdDriveScale *= 0.94f + (highQuality * 0.04f);
            settings.softClipAmount *= 0.60f + (q * 0.14f);
            settings.lowDetectorScale *= 0.82f - (highQuality * 0.06f);
            settings.stereoLink += 0.06f + (highQuality * 0.10f);
            settings.densityLift *= 1.02f + (highQuality * 0.10f);
            settings.safetyMarginDb += highQuality * 0.07f;
            bandAttackScale = 1.08f;
            bandReleaseScale = 1.12f + (highQuality * 0.10f);
            bandWeightScale = 0.88f + (highQuality * 0.10f);
            break;
        case Mode::Kick808:
            settings.thresholdDriveScale *= 0.92f + (highQuality * 0.04f);
            settings.softClipAmount *= 0.56f + (q * 0.12f);
            settings.lowDetectorScale *= 0.78f - (highQuality * 0.06f);
            settings.stereoLink += 0.10f + (highQuality * 0.12f);
            settings.densityLift *= 1.0f + (highQuality * 0.08f);
            settings.safetyMarginDb += highQuality * 0.060f;
            bandAttackScale = 1.10f;
            bandReleaseScale = 1.14f + (highQuality * 0.11f);
            bandWeightScale = 0.86f + (highQuality * 0.08f);
            break;
        case Mode::Bright:
            settings.thresholdDriveScale *= 0.90f + (highQuality * 0.04f);
            settings.softClipAmount *= 0.48f + (q * 0.10f);
            settings.spectralControlScale *= 0.94f + (highQuality * 0.014f);
            settings.loudnessFocus *= 0.98f + (highQuality * 0.10f);
            settings.safetyMarginDb += highQuality * 0.045f;
            bandRatioScale = 0.96f + (highQuality * 0.04f);
            bandAttackScale = 0.96f;
            bandReleaseScale = 1.08f + (highQuality * 0.10f);
            bandWeightScale = 0.96f + (highQuality * 0.16f);
            break;
        case Mode::Glue:
            settings.thresholdDriveScale *= 0.90f + (highQuality * 0.04f);
            settings.softClipAmount *= 0.38f + (q * 0.10f);
            settings.spectralControlScale *= 0.94f + (highQuality * 0.04f);
            settings.densityLift *= 0.96f + (highQuality * 0.10f);
            settings.safetyMarginDb += highQuality * 0.08f;
            bandRatioScale = 0.92f;
            bandAttackScale = 1.18f;
            bandReleaseScale = 1.28f + (highQuality * 0.14f);
            bandWeightScale = 0.86f + (highQuality * 0.08f);
            break;
        case Mode::Clean:
            settings.thresholdDriveScale *= 0.92f;
            settings.softClipAmount *= 0.16f + (ecoBias * 0.06f);
            settings.hardClipBlend *= 0.35f;
            settings.spectralControlScale *= 0.92f + (highQuality * 0.035f);
            settings.loudnessFocus *= 0.82f;
            settings.densityLift *= 0.70f + (highQuality * 0.08f);
            settings.safetyMarginDb += 0.04f + (highQuality * 0.09f);
            bandRatioScale = 0.86f;
            bandAttackScale = 1.22f;
            bandReleaseScale = 1.22f + (highQuality * 0.18f);
            bandWeightScale = 0.74f + (highQuality * 0.08f);
            break;
    }

    switch (mode) {
        case Mode::EDM:
        case Mode::House:
            settings.softClipAmount *= 1.0f - (highSafety * 0.025f) - (masterSafety * 0.025f);
            settings.hardClipBlend *= 1.0f - (highSafety * 0.045f) - (masterSafety * 0.025f);
            settings.spectralControlScale *= 1.0f + (masterSafety * 0.003f);
            settings.finalLimiterStrength *= 1.0f + (highSafety * 0.055f) + (masterSafety * 0.065f);
            settings.safetyMarginDb += highSafety * 0.015f;
            bandWeightScale *= 1.0f + (highSafety * 0.015f);
            break;
        case Mode::HipHop:
        case Mode::Bass:
        case Mode::Trap:
        case Mode::Kick808:
            settings.softClipAmount *= 1.0f - (highSafety * 0.035f) - (masterSafety * 0.025f);
            settings.hardClipBlend *= 1.0f - (highSafety * 0.045f) - (masterSafety * 0.025f);
            settings.lowDetectorScale *= 1.0f - (highSafety * 0.065f) - (masterSafety * 0.035f);
            settings.stereoLink += highSafety * 0.055f;
            settings.safetyMarginDb += highSafety * 0.018f;
            bandReleaseScale *= 1.0f + (masterSafety * 0.035f);
            break;
        case Mode::Drums:
        case Mode::OneShot:
        case Mode::Percs:
        case Mode::DrumNBass:
            settings.softClipAmount *= 1.0f - (highSafety * 0.020f) - (ultraSafety * 0.025f);
            settings.hardClipBlend *= 1.0f - (highSafety * 0.040f) - (masterSafety * 0.025f);
            settings.transientScale *= 1.0f + (highSafety * 0.08f);
            settings.spectralControlScale *= 1.0f + (masterSafety * 0.002f);
            settings.safetyMarginDb += (highSafety * 0.020f) + (masterSafety * 0.010f);
            bandAttackScale *= 1.0f + (masterSafety * 0.025f);
            bandReleaseScale *= 1.0f + (masterSafety * 0.03f);
            break;
        case Mode::OneShotClean:
            settings.softClipAmount *= 1.0f - (highSafety * 0.10f) - (masterSafety * 0.055f);
            settings.hardClipBlend *= 1.0f - (highSafety * 0.10f) - (masterSafety * 0.06f);
            settings.transientScale *= 1.0f + (highSafety * 0.09f);
            settings.spectralControlScale *= 1.0f - (masterSafety * 0.004f);
            settings.finalLimiterStrength *= 1.0f + (masterSafety * 0.07f) + (ultraSafety * 0.04f);
            settings.safetyMarginDb += (highSafety * 0.03f) + (masterSafety * 0.025f);
            bandAttackScale *= 1.0f + (masterSafety * 0.025f);
            bandReleaseScale *= 1.0f + (masterSafety * 0.045f) + (ultraSafety * 0.035f);
            break;
        case Mode::Dubstep:
            settings.softClipAmount *= 1.0f - (highSafety * 0.025f) - (masterSafety * 0.025f);
            settings.hardClipBlend *= 1.0f - (highSafety * 0.040f) - (masterSafety * 0.025f);
            settings.lowDetectorScale *= 1.0f - (highSafety * 0.07f) - (masterSafety * 0.04f);
            settings.stereoLink += highSafety * 0.055f;
            settings.spectralControlScale *= 1.0f + (masterSafety * 0.002f);
            settings.safetyMarginDb += highSafety * 0.016f;
            bandReleaseScale *= 1.0f + (masterSafety * 0.035f);
            break;
        case Mode::Acoustic:
        case Mode::Vocal:
        case Mode::Clean:
            settings.softClipAmount *= 1.0f - (highSafety * 0.10f) - (masterSafety * 0.055f);
            settings.hardClipBlend *= 1.0f - (highSafety * 0.08f) - (masterSafety * 0.055f);
            settings.spectralControlScale *= 1.0f - (masterSafety * 0.004f);
            settings.finalLimiterStrength *= 1.0f + (masterSafety * 0.075f) + (ultraSafety * 0.045f);
            settings.safetyMarginDb += (highSafety * 0.030f) + (masterSafety * 0.030f);
            bandReleaseScale *= 1.0f + (masterSafety * 0.04f) + (ultraSafety * 0.04f);
            bandWeightScale *= 1.0f - (highSafety * 0.025f);
            break;
        case Mode::Bright:
            settings.softClipAmount *= 1.0f - (highSafety * 0.070f) - (masterSafety * 0.040f);
            settings.hardClipBlend *= 1.0f - (highSafety * 0.085f) - (masterSafety * 0.040f);
            settings.spectralControlScale *= 1.0f - (highSafety * 0.006f) - (masterSafety * 0.004f);
            settings.safetyMarginDb += (highSafety * 0.030f) + (masterSafety * 0.025f);
            bandReleaseScale *= 1.0f + (masterSafety * 0.05f);
            break;
        case Mode::Glue:
            settings.softClipAmount *= 1.0f - (highSafety * 0.050f) - (masterSafety * 0.040f);
            settings.hardClipBlend *= 1.0f - (highSafety * 0.065f) - (masterSafety * 0.030f);
            settings.finalLimiterStrength *= 1.0f + (masterSafety * 0.045f);
            settings.safetyMarginDb += highSafety * 0.018f;
            bandReleaseScale *= 1.0f + (masterSafety * 0.07f) + (ultraSafety * 0.04f);
            break;
    }

    if (ecoQuality) {
        settings.spectralControlScale *= 0.94f;
        settings.safetyMarginDb *= 0.82f;
        settings.finalLimiterStrength *= 0.96f;
        switch (mode) {
            case Mode::EDM:
            case Mode::House:
            case Mode::Dubstep:
            case Mode::DrumNBass:
            case Mode::Trap:
                settings.softClipAmount *= 1.02f;
                settings.loudnessFocus *= 1.04f;
                settings.densityLift *= 1.04f;
                settings.transientScale *= 1.03f;
                bandWeightScale *= 0.96f;
                break;
            case Mode::HipHop:
            case Mode::Bass:
            case Mode::Kick808:
                settings.softClipAmount *= 1.03f;
                settings.lowDetectorScale *= 0.98f;
                settings.stereoLink += 0.025f;
                settings.densityLift *= 1.03f;
                bandReleaseScale *= 1.02f;
                break;
            case Mode::Drums:
            case Mode::OneShot:
            case Mode::Percs:
                settings.softClipAmount *= 1.03f;
                settings.transientScale *= 1.05f;
                settings.densityLift *= 0.98f;
                bandAttackScale *= 0.96f;
                break;
            case Mode::OneShotClean:
                settings.softClipAmount *= 0.92f;
                settings.transientScale *= 1.05f;
                settings.finalLimiterStrength *= 1.02f;
                settings.densityLift *= 0.96f;
                bandAttackScale *= 0.96f;
                break;
            case Mode::Bright:
                settings.spectralControlScale *= 0.96f;
                settings.loudnessFocus *= 1.03f;
                break;
            case Mode::Clean:
            case Mode::Acoustic:
            case Mode::Vocal:
                settings.softClipAmount *= 0.92f;
                settings.finalLimiterStrength *= 1.02f;
                settings.densityLift *= 0.98f;
                break;
            case Mode::Glue:
                settings.densityLift *= 1.03f;
                settings.loudnessFocus *= 1.02f;
                bandReleaseScale *= 0.98f;
                break;
        }
    }

    auto const renderBias = bandMultiplier >= 8 ? juce::jlimit(0.0f, 1.0f, (static_cast<float>(bandMultiplier) - 4.0f) / 28.0f) : 0.0f;
    auto const ultraBias = bandMultiplier >= 16 ? juce::jlimit(0.0f, 1.0f, (static_cast<float>(bandMultiplier) - 8.0f) / 24.0f) : 0.0f;
    settings.safetyMarginDb += renderBias * 0.035f;
    settings.finalLimiterStrength *= 1.0f + (renderBias * 0.10f);
    settings.spectralControlScale *= 1.0f + (renderBias * 0.004f);
    settings.softClipAmount *= 1.0f - (renderBias * 0.025f);
    settings.hardClipBlend *= 1.0f - (renderBias * 0.030f);

    auto const nihEfficiency = juce::jlimit(0.0f, 1.0f, (highQuality * 0.55f) + (renderBias * 0.45f));
    switch (mode) {
        case Mode::EDM:
        case Mode::House:
        case Mode::Dubstep:
        case Mode::DrumNBass:
            settings.loudnessFocus *= 1.0f + (nihEfficiency * 0.11f);
            settings.densityLift *= 1.0f + (nihEfficiency * 0.09f);
            settings.densityLift *= 1.0f + (ultraBias * 0.055f);
            settings.softClipAmount *= 1.0f + (ultraBias * 0.006f);
            settings.finalLimiterStrength *= 1.0f + (nihEfficiency * 0.035f);
            break;
        case Mode::HipHop:
        case Mode::Trap:
        case Mode::Bass:
        case Mode::Kick808:
            settings.densityLift *= 1.0f + (nihEfficiency * 0.10f);
            settings.loudnessFocus *= 1.0f + (ultraBias * 0.035f);
            settings.lowDetectorScale *= 1.0f - (nihEfficiency * 0.025f);
            settings.stereoLink += ultraBias * 0.018f;
            settings.finalLimiterStrength *= 1.0f + (nihEfficiency * 0.030f);
            break;
        case Mode::Drums:
        case Mode::OneShot:
        case Mode::Percs:
            settings.loudnessFocus *= 1.0f + (nihEfficiency * 0.07f);
            settings.transientScale *= 1.0f + (nihEfficiency * 0.045f);
            settings.densityLift *= 1.0f + (ultraBias * 0.025f);
            break;
        case Mode::OneShotClean:
            settings.loudnessFocus *= 1.0f + (nihEfficiency * 0.035f);
            settings.transientScale *= 1.0f + (nihEfficiency * 0.05f);
            settings.finalLimiterStrength *= 1.0f + (nihEfficiency * 0.03f);
            settings.spectralControlScale *= 1.0f - (nihEfficiency * 0.012f);
            break;
        case Mode::Bright:
            settings.loudnessFocus *= 1.0f + (nihEfficiency * 0.055f);
            settings.spectralControlScale *= 1.0f - (nihEfficiency * 0.015f);
            settings.densityLift *= 1.0f + (ultraBias * 0.018f);
            break;
        case Mode::Glue:
            settings.densityLift *= 1.0f + (nihEfficiency * 0.07f);
            settings.finalLimiterStrength *= 1.0f + (nihEfficiency * 0.025f);
            break;
        case Mode::Clean:
        case Mode::Acoustic:
        case Mode::Vocal:
            settings.finalLimiterStrength *= 1.0f + (nihEfficiency * 0.025f);
            settings.spectralControlScale *= 1.0f - (nihEfficiency * 0.012f);
            settings.loudnessFocus *= 1.0f + (ultraBias * 0.015f);
            break;
    }

#if defined(PEAKEATER_SPECTRAL_2_VARIANT)
    auto const spectral2Hq = juce::jlimit(0.0f, 1.0f, (highQuality * 0.55f) + (renderBias * 0.45f));
    if (spectral2Hq > 0.0001f) {
        settings.finalLimiterStrength *= 1.0f + (spectral2Hq * 0.045f);
        settings.loudnessFocus *= 1.0f + (spectral2Hq * 0.040f);
        settings.densityLift *= 1.0f + (spectral2Hq * 0.035f);
        settings.safetyMarginDb += spectral2Hq * 0.012f;
        settings.softClipAmount *= 1.0f - (spectral2Hq * 0.055f);
        settings.hardClipBlend *= 1.0f - (spectral2Hq * 0.070f);
    }
#endif
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    switch (mode) {
        case Mode::EDM:
            settings.spectralSustainScale = 1.15f;
            settings.spectralUpwardStartOffsetDb = 0.32f;
            settings.spectralMidFocusScale = 1.08f;
            settings.spectralGenreLift = 2.45f;
            break;
        case Mode::HipHop:
            settings.spectralSustainScale = 1.12f;
            settings.spectralUpwardStartOffsetDb = 0.28f;
            settings.spectralMidFocusScale = 1.05f;
            settings.spectralGenreLift = 1.19f;
            break;
        case Mode::Drums:
            settings.spectralSustainScale = 1.07f;
            settings.spectralUpwardStartOffsetDb = 0.14f;
            settings.spectralMidFocusScale = 1.03f;
            settings.spectralGenreLift = 0.71f;
            break;
        case Mode::OneShot:
            settings.spectralSustainScale = 1.055f;
            settings.spectralUpwardStartOffsetDb = 0.10f;
            settings.spectralMidFocusScale = 1.02f;
            settings.spectralGenreLift = 0.55f;
            break;
        case Mode::OneShotClean:
            settings.spectralSustainScale = 1.045f;
            settings.spectralUpwardStartOffsetDb = 0.08f;
            settings.spectralMidFocusScale = 1.02f;
            settings.spectralGenreLift = 0.38f;
            break;
        case Mode::Acoustic:
            settings.spectralSustainScale = 1.05f;
            settings.spectralUpwardStartOffsetDb = 0.09f;
            settings.spectralMidFocusScale = 1.03f;
            settings.spectralGenreLift = 0.35f;
            break;
        case Mode::Vocal:
            settings.spectralSustainScale = 1.07f;
            settings.spectralUpwardStartOffsetDb = 0.14f;
            settings.spectralMidFocusScale = 1.08f;
            settings.spectralGenreLift = 0.50f;
            break;
        case Mode::Bass:
            settings.spectralSustainScale = 1.09f;
            settings.spectralUpwardStartOffsetDb = 0.20f;
            settings.spectralMidFocusScale = 1.02f;
            settings.spectralGenreLift = 0.70f;
            break;
        case Mode::Bright:
            settings.spectralSustainScale = 1.08f;
            settings.spectralUpwardStartOffsetDb = 0.16f;
            settings.spectralMidFocusScale = 1.10f;
            settings.spectralGenreLift = 0.58f;
            break;
        case Mode::Glue:
            settings.spectralSustainScale = 1.09f;
            settings.spectralUpwardStartOffsetDb = 0.20f;
            settings.spectralMidFocusScale = 1.05f;
            settings.spectralGenreLift = 0.68f;
            break;
        case Mode::Clean:
            settings.spectralSustainScale = 1.045f;
            settings.spectralUpwardStartOffsetDb = 0.08f;
            settings.spectralMidFocusScale = 1.02f;
            settings.spectralGenreLift = 0.32f;
            break;
        case Mode::Percs:
            settings.spectralSustainScale = 1.085f;
            settings.spectralUpwardStartOffsetDb = 0.12f;
            settings.spectralMidFocusScale = 1.03f;
            settings.spectralGenreLift = 0.71f;
            break;
        case Mode::Dubstep:
            settings.spectralSustainScale = 1.15f;
            settings.spectralUpwardStartOffsetDb = 0.36f;
            settings.spectralMidFocusScale = 1.08f;
            settings.spectralGenreLift = 1.30f;
            break;
        case Mode::DrumNBass:
            settings.spectralSustainScale = 1.14f;
            settings.spectralUpwardStartOffsetDb = 0.30f;
            settings.spectralMidFocusScale = 1.07f;
            settings.spectralGenreLift = 1.87f;
            break;
        case Mode::House:
            settings.spectralSustainScale = 1.12f;
            settings.spectralUpwardStartOffsetDb = 0.27f;
            settings.spectralMidFocusScale = 1.06f;
            settings.spectralGenreLift = 0.95f;
            break;
        case Mode::Trap:
            settings.spectralSustainScale = 1.13f;
            settings.spectralUpwardStartOffsetDb = 0.30f;
            settings.spectralMidFocusScale = 1.04f;
            settings.spectralGenreLift = 1.28f;
            break;
        case Mode::Kick808:
            settings.spectralSustainScale = 1.075f;
            settings.spectralUpwardStartOffsetDb = 0.15f;
            settings.spectralMidFocusScale = 1.01f;
            settings.spectralGenreLift = 0.65f;
            break;
    }

    auto const spectral3Hq = juce::jlimit(0.0f, 1.0f, (highQuality * 0.45f) + (renderBias * 0.42f) + (ultraBias * 0.18f));
    if (spectral3Hq > 0.0001f) {
        settings.finalLimiterStrength *= 1.0f + (spectral3Hq * 0.030f);
        settings.safetyMarginDb += spectral3Hq * 0.006f;
        settings.softClipAmount *= 1.0f - (spectral3Hq * 0.028f);
        settings.hardClipBlend *= 1.0f - (spectral3Hq * 0.036f);
        settings.spectralControlScale *= 1.0f - (spectral3Hq * 0.010f);

        switch (mode) {
            case Mode::Drums:
            case Mode::OneShot:
            case Mode::OneShotClean:
            case Mode::Percs:
                settings.transientScale *= 1.0f + (spectral3Hq * 0.050f);
                settings.maxBandReductionDb *= 1.0f - (spectral3Hq * 0.025f);
                settings.densityLift *= 1.0f - (spectral3Hq * 0.020f);
                bandAttackScale *= 1.0f + (spectral3Hq * 0.030f);
                break;
            case Mode::DrumNBass:
                settings.transientScale *= 1.0f + (spectral3Hq * 0.040f);
                settings.loudnessFocus *= 1.0f + (spectral3Hq * 0.030f);
                settings.spectralControlScale *= 1.0f - (spectral3Hq * 0.012f);
                break;
            case Mode::Dubstep:
            case Mode::Trap:
            case Mode::HipHop:
            case Mode::Bass:
            case Mode::Kick808:
                settings.lowDetectorScale *= 1.0f - (spectral3Hq * 0.030f);
                settings.stereoLink += spectral3Hq * 0.018f;
                settings.finalLimiterStrength *= 1.0f + (spectral3Hq * 0.020f);
                break;
            case Mode::Bright:
            case Mode::Vocal:
                settings.spectralControlScale *= 1.0f - (spectral3Hq * 0.020f);
                settings.loudnessFocus *= 1.0f + (spectral3Hq * 0.018f);
                break;
            case Mode::Clean:
            case Mode::Acoustic:
                settings.finalLimiterStrength *= 1.0f + (spectral3Hq * 0.020f);
                settings.densityLift *= 1.0f - (spectral3Hq * 0.015f);
                break;
            default:
                break;
        }
    }
#endif

    for (auto& band : settings.bands) {
        band.ratio = juce::jlimit(1.05f, 8.0f, band.ratio * bandRatioScale);
        band.attackMs = juce::jlimit(0.03f, 420.0f, band.attackMs * bandAttackScale);
        band.releaseMs = juce::jlimit(4.0f, 520.0f, band.releaseMs * bandReleaseScale);
        band.weight = juce::jlimit(0.18f, 1.18f, band.weight * bandWeightScale);
    }

    settings.softClipAmount = juce::jlimit(0.0f, 1.0f, settings.softClipAmount);
    settings.hardClipBlend = juce::jlimit(0.0f, 0.82f, settings.hardClipBlend);
    settings.maxBandReductionDb = juce::jlimit(3.0f, 14.0f, settings.maxBandReductionDb);
    settings.spectralControlScale = juce::jlimit(0.12f, 1.16f, settings.spectralControlScale);
    settings.lowDetectorScale = juce::jlimit(0.18f, 1.0f, settings.lowDetectorScale);
    settings.stereoLink = juce::jlimit(0.32f, 1.0f, settings.stereoLink);
    settings.grLimitDb = juce::jlimit(2.0f, 14.0f, settings.grLimitDb + (highSafety * 0.32f) + (masterSafety * 0.32f)
                                                              + (ultraSafety * 0.22f));
    settings.finalLimiterStrength = juce::jlimit(0.42f, 1.54f, settings.finalLimiterStrength);
    settings.loudnessFocus = juce::jlimit(0.0f, 0.88f, settings.loudnessFocus);
    settings.densityLift = juce::jlimit(0.0f, 0.72f, settings.densityLift);
    settings.safetyMarginDb = juce::jlimit(0.0f, 0.72f, settings.safetyMarginDb);
    settings.thresholdDriveScale = juce::jlimit(0.62f, 1.42f, settings.thresholdDriveScale);
    settings.ceilingReleaseMs = juce::jlimit(12.0f, 280.0f, settings.ceilingReleaseMs * (1.0f + (highQuality * 0.04f)));
    return settings;
}

SpectralMaximizer::StyleSettings SpectralMaximizer::getStyleSettings(LimiterStyle limiterStyle) {
    switch (limiterStyle) {
        case LimiterStyle::Punch:
            return {0.28f, -0.18f, 1.05f, 0.02f, 0.04f, 0.0f, 0.96f};
        case LimiterStyle::Glue:
            return {0.04f, 0.28f, 0.82f, 0.08f, 0.02f, 0.03f, 0.9f};
        case LimiterStyle::Safe:
            return {0.0f, 0.22f, 0.72f, 0.22f, 0.0f, 0.28f, 1.08f};
        case LimiterStyle::Loud:
            return {0.12f, -0.08f, 1.0f, 0.04f, 0.34f, 0.05f, 1.14f};
        case LimiterStyle::Transparent:
        default:
            return {};
    }
}

SpectralMaximizer::SaturationSettings SpectralMaximizer::getSaturationSettings(SaturationStyle saturationStyle) {
    switch (saturationStyle) {
        case SaturationStyle::Warm:
            return {0.92f, 1.12f, 1.38f, 0.72f, 0.18f, 1.08f};
        case SaturationStyle::Tube:
            return {1.05f, 1.18f, 1.5f, 1.06f, 0.08f, 0.92f};
        case SaturationStyle::Tape:
            return {0.86f, 1.26f, 0.82f, 1.18f, 0.38f, 1.2f};
        case SaturationStyle::Transformer:
            return {1.0f, 1.34f, 1.08f, 1.28f, 0.16f, 0.98f};
        case SaturationStyle::Clean:
        default:
            return {};
    }
}

float SpectralMaximizer::dbToGain(float db) {
    return juce::Decibels::decibelsToGain(db, gMinDb);
}

float SpectralMaximizer::gainToDb(float gain) {
    return juce::Decibels::gainToDecibels(std::max(gain, 0.000001f), gMinDb);
}

float SpectralMaximizer::fastDbToGain(float db) {
#if defined(PEAKEATER_REFERENCE_MATH)
    return std::exp2(std::max(gMinDb, db) * 0.1660964047f);
#else
    auto const x = std::max(gMinDb, db) * 0.1660964047f;
    auto exponent = static_cast<int>(x);
    if (x < 0.0f && static_cast<float>(exponent) != x) {
        --exponent;
    }
    auto const fraction = x - static_cast<float>(exponent);
    auto const tablePosition = fraction * static_cast<float>(gConversionTableSize);
    auto const index = static_cast<size_t>(tablePosition);
    auto const interpolation = tablePosition - static_cast<float>(index);
    auto const lower = gConversionTables.exp2Fraction[index];
    auto const interpolated = lower + ((gConversionTables.exp2Fraction[index + 1] - lower) * interpolation);
    auto const scaleBits = static_cast<std::uint32_t>(exponent + 127) << 23;
    return interpolated * std::bit_cast<float>(scaleBits);
#endif
}

float SpectralMaximizer::fastGainToDb(float gain) {
#if defined(PEAKEATER_REFERENCE_MATH)
    return 6.020599913f * std::log2(std::max(gain, 0.000001f));
#else
    auto const safeGain = std::max(gain, 0.000001f);
    auto const bits = std::bit_cast<std::uint32_t>(safeGain);
    auto const exponent = static_cast<int>((bits >> 23) & 0xffu) - 127;
    auto const mantissaBits = bits & 0x007fffffu;
    auto const index = static_cast<size_t>(mantissaBits >> (23 - gConversionTableBits));
    auto const remainderMask = (std::uint32_t{1} << (23 - gConversionTableBits)) - 1u;
    auto const interpolation = static_cast<float>(mantissaBits & remainderMask)
                               * (1.0f / static_cast<float>(std::uint32_t{1} << (23 - gConversionTableBits)));
    auto const lower = gConversionTables.log2Mantissa[index];
    auto const logMantissa = lower + ((gConversionTables.log2Mantissa[index + 1] - lower) * interpolation);
    auto const log2Gain = static_cast<float>(exponent) + logMantissa;
    return 6.020599913f * log2Gain;
#endif
}

float SpectralMaximizer::thresholdToDriveDb(float thresholdDb) {
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    auto const drive = juce::jlimit(0.0f, 24.0f, -thresholdDb);
#else
    auto const drive = juce::jlimit(0.0f, 48.0f, -thresholdDb);
#endif
    auto const normalized = drive / 48.0f;
    auto const shapedLift = std::pow(normalized, 1.35f) * 9.0f;
    return juce::jlimit(0.0f, 54.0f, (drive * 1.32f) + shapedLift);
}

float SpectralMaximizer::compressionGainDb(float envelopeDb, float thresholdDb, float inverseRatio,
                                           float kneeDb, float inverseTwoKneeDb) {
    auto const overDb = envelopeDb - thresholdDb;
    auto const safeKneeDb = std::max(0.0f, kneeDb);
#if defined(PEAKEATER_REFERENCE_MATH)
    auto const safeRatio = 1.0f / inverseRatio;
    if (safeKneeDb <= 0.001f) {
        return overDb > 0.0f ? ((overDb / safeRatio) - overDb) : 0.0f;
    }
    auto const halfKnee = safeKneeDb * 0.5f;
    if (overDb <= -halfKnee) {
        return 0.0f;
    }
    if (overDb >= halfKnee) {
        return (overDb / safeRatio) - overDb;
    }
    auto const kneePosition = overDb + halfKnee;
    return ((1.0f / safeRatio) - 1.0f) * kneePosition * kneePosition / (2.0f * safeKneeDb);
#else

    if (safeKneeDb <= 0.001f) {
        return overDb > 0.0f ? ((overDb * inverseRatio) - overDb) : 0.0f;
    }

    auto const halfKnee = safeKneeDb * 0.5f;
    if (overDb <= -halfKnee) {
        return 0.0f;
    }
    if (overDb >= halfKnee) {
        return (overDb * inverseRatio) - overDb;
    }

    auto const kneePosition = overDb + halfKnee;
    return (inverseRatio - 1.0f) * kneePosition * kneePosition * inverseTwoKneeDb;
#endif
}

float SpectralMaximizer::roundedHardClip(float sample, float kneeWidth) {
    auto const sign = sample < 0.0f ? -1.0f : 1.0f;
    auto const magnitude = std::abs(sample);
    auto const safeKnee = juce::jlimit(0.02f, 0.45f, kneeWidth);
    auto const kneeStart = std::max(0.0f, 1.0f - safeKnee);
    auto const kneeEnd = 1.0f + safeKnee;

    if (magnitude <= kneeStart) {
        return sample;
    }
    if (magnitude >= kneeEnd) {
        return sign;
    }

    auto const position = (magnitude - kneeStart) / std::max(0.000001f, kneeEnd - kneeStart);
    auto const shaped = hermiteShape(position);
    return sign * (magnitude + ((1.0f - magnitude) * shaped));
}

float SpectralMaximizer::applySyncedAllpassPeakRelief(float sample, float amount, float coefficient, ChannelState& state) {
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    auto const safeAmount = juce::jlimit(0.0f, 0.14f, amount);
#else
    auto const safeAmount = juce::jlimit(0.0f, 0.10f, amount);
#endif
    if (safeAmount <= 0.0001f || !std::isfinite(sample)) {
        auto const safeSample = std::isfinite(sample) ? sample : 0.0f;
        state.peakReliefAllpassInput = safeSample;
        state.peakReliefAllpassOutput *= 0.12f;
        return safeSample;
    }

    auto const a = juce::jlimit(0.18f, 0.72f, coefficient);
    auto const previousInput = state.peakReliefAllpassInput;
    auto const previousOutput = state.peakReliefAllpassOutput;
    auto const allpass = (-a * sample) + previousInput + (a * previousOutput);
    state.peakReliefAllpassInput = sample;
    state.peakReliefAllpassOutput = allpass;

    // Synced allpass peak reduction idea: use phase dispersion only when the
    // blended allpass candidate actually lowers the instantaneous peak.
    auto const candidate = sample + ((allpass - sample) * safeAmount);
    return std::abs(candidate) < std::abs(sample) ? candidate : sample;
}

SpectralMaximizer::ClipResult SpectralMaximizer::applyMaskingShapedPeakResidual(
    float sample, float ceilingGain, float authority, float transientRisk, float lowRisk,
    float spectralFlatness, float highBandRatio, float previousInput, ChannelState& state) {
    if (!std::isfinite(sample) || !std::isfinite(ceilingGain)) {
        state.maskingResidual = 0.0f;
        state.maskingResidualEnvelope = 0.0f;
        return {0.0f, 24.0f};
    }

    auto const safeAuthority = juce::jlimit(0.0f, 0.72f, authority);
    auto const magnitude = std::abs(sample);
    auto const safeCeiling = std::max(0.000001f, ceilingGain);
    auto const onsetSlew = std::abs(sample - (std::isfinite(previousInput) ? previousInput : sample));
    auto const onsetRisk = hermiteShape(juce::jlimit(0.0f, 1.0f, onsetSlew / (safeCeiling * 1.8f)));

    if (safeAuthority <= 0.0001f || magnitude <= safeCeiling * 1.018f) {
        state.maskingResidual *= 0.62f;
        state.maskingResidualEnvelope *= 0.985f;
        if (std::abs(state.maskingResidual) < 1.0e-10f) {
            state.maskingResidual = 0.0f;
        }
        return {sample, 0.0f};
    }

    auto const transientGate = juce::jlimit(0.0f, 1.0f, 1.0f - std::max(transientRisk, onsetRisk) * 0.92f);
    auto const lowGate = juce::jlimit(0.0f, 1.0f, 1.0f - lowRisk * 0.78f);
    auto const maskingOpportunity =
        juce::jlimit(0.08f, 0.82f, 0.14f + juce::jlimit(0.0f, 1.0f, spectralFlatness) * 0.46f
                                         + juce::jlimit(0.0f, 1.0f, highBandRatio) * 0.12f);
    auto const gate = safeAuthority * transientGate * lowGate * maskingOpportunity;

    auto const residualStart = safeCeiling * juce::jmap(safeAuthority, 1.10f, 1.025f);
    auto const excess = std::max(0.0f, magnitude - residualStart);
    auto const sign = sample < 0.0f ? -1.0f : 1.0f;
    auto const targetResidual = -sign * excess * gate;

    // Bound and smooth the correction itself so it cannot become a narrow-band
    // click or a second transient. The release is deliberately faster than the
    // limiter envelope because this state is only a short pre-clip residual.
    auto const maxResidual = safeCeiling * juce::jlimit(0.010f, 0.16f, 0.035f + safeAuthority * 0.18f);
    auto const boundedTarget = juce::jlimit(-maxResidual, maxResidual, targetResidual);
    auto const residualCoefficient = std::abs(boundedTarget) > std::abs(state.maskingResidual) ? 0.28f : 0.72f;
    state.maskingResidual = residualCoefficient * state.maskingResidual
                            + (1.0f - residualCoefficient) * boundedTarget;
    state.maskingResidualEnvelope = std::max(std::abs(state.maskingResidual), state.maskingResidualEnvelope * 0.992f);

    auto const candidate = sample + state.maskingResidual;
    if (!std::isfinite(candidate) || std::abs(candidate) >= magnitude) {
        return {sample, 0.0f};
    }

    auto const reductionDb = std::max(0.0f, fastGainToDb(magnitude / std::max(std::abs(candidate), 0.000001f)));
    return {candidate, reductionDb};
}

SpectralMaximizer::ClipResult SpectralMaximizer::applyCeiling(float sample, float ceilingGain, float releaseMs, ModeSettings const& mode,
                                                              ChannelState& state, double sampleRate, bool truePeakLimit,
                                                              float truePeakMarginDb, float adaptiveRelease, float punchProtect,
                                                              float releaseShape, float safetyMarginOffsetDb,
                                                              float limiterStrengthScale, float grLimitDb, float rmsContourAmount,
                                                              float contourAttackCoefficient, float contourReleaseCoefficient) {
    if (!std::isfinite(sample)) {
        state.ceilingGain = 1.0f;
        return {0.0f, 24.0f};
    }

    auto const safeMarginDb = mode.safetyMarginDb + juce::jlimit(0.0f, 0.65f, safetyMarginOffsetDb)
                              + (truePeakLimit ? juce::jlimit(0.0f, 1.0f, truePeakMarginDb) : 0.0f);
    auto const safeCeiling = std::max(0.000001f, ceilingGain * dbToGain(-safeMarginDb));
    auto const magnitude = std::abs(sample);
    auto const previous = std::isfinite(state.previousCeilingInput) ? state.previousCeilingInput : sample;
    auto const previous2 = std::isfinite(state.previousCeilingInput2) ? state.previousCeilingInput2 : previous;
    auto const previousMagnitude = std::abs(previous);
    auto const transientRise = std::max(0.0f, magnitude - previousMagnitude);
    auto const ceilingTransient =
        magnitude > 0.000001f ? juce::jlimit(0.0f, 1.0f, (transientRise / magnitude) * 2.25f) : 0.0f;
    auto const punch = juce::jlimit(0.0f, 1.0f, punchProtect) * ceilingTransient;
    auto predictedMagnitude = magnitude;
    if (truePeakLimit) {
        auto const nextEstimate = sample + juce::jlimit(-1.25f, 1.25f, (sample - previous) * 0.42f + (previous - previous2) * 0.12f);
        auto const cubicInterpolate = [](float p0, float p1, float p2, float p3, float t) {
            auto const t2 = t * t;
            auto const t3 = t2 * t;
            return 0.5f * ((2.0f * p1) + ((-p0 + p2) * t) + (((2.0f * p0) - (5.0f * p1) + (4.0f * p2) - p3) * t2)
                           + ((-p0 + (3.0f * p1) - (3.0f * p2) + p3) * t3));
        };
        constexpr std::array<std::array<float, 4>, 5> phaseTaps{{
            {{-0.039f, 0.367f, 0.773f, -0.101f}},
            {{-0.062f, 0.562f, 0.562f, -0.062f}},
            {{-0.094f, 0.594f, 0.500f, 0.000f}},
            {{-0.062f, 0.265f, 0.900f, -0.103f}},
            {{-0.012f, 0.113f, 0.941f, -0.043f}},
        }};
        auto const curvatureBoost = std::abs((sample - previous) - (previous - previous2))
                                    * juce::jmap(juce::jlimit(0.0f, 1.0f, truePeakMarginDb), 0.018f, 0.058f);
        for (auto const t : {0.25f, 0.5f, 0.75f}) {
            auto const interpolated = cubicInterpolate(previous2, previous, sample, nextEstimate, t);
            predictedMagnitude = std::max(predictedMagnitude, std::abs(interpolated) + curvatureBoost);
        }
        for (auto const& taps : phaseTaps) {
            auto const interpolated = (previous2 * taps[0]) + (previous * taps[1]) + (sample * taps[2]) + (nextEstimate * taps[3]);
            auto const slopeBoost = std::abs(sample - previous) * juce::jmap(juce::jlimit(0.0f, 1.0f, truePeakMarginDb), 0.045f, 0.125f);
            predictedMagnitude = std::max(predictedMagnitude, std::abs(interpolated) + slopeBoost + (curvatureBoost * 0.5f));
        }
    }
    state.previousCeilingInput2 = previous;
    state.previousCeilingInput = sample;

    auto const sign = sample < 0.0f ? -1.0f : 1.0f;
    auto const controlMagnitude = truePeakLimit ? predictedMagnitude : magnitude;
    auto const targetGain = controlMagnitude > safeCeiling ? safeCeiling / std::max(controlMagnitude, 0.000001f) : 1.0f;
    auto const safeCeilingDb = fastGainToDb(safeCeiling);
    auto const currentToCeilingDb = fastGainToDb(controlMagnitude) - safeCeilingDb;
    auto const overDb = std::max(0.0f, currentToCeilingDb);
    auto const ceilingGrLimitDb =
        juce::jlimit(0.0f, 24.0f, std::max(grLimitDb, mode.grLimitDb) + (truePeakLimit ? overDb * 0.22f : overDb * 0.12f));
    auto const grFloor = dbToGain(-ceilingGrLimitDb);
    auto const protectedTargetGain = std::max(grFloor, targetGain);
    state.ceilingTargetGainWindow[state.ceilingTargetGainIndex] = protectedTargetGain;
    state.ceilingTargetGainIndex = (state.ceilingTargetGainIndex + 1) % state.ceilingTargetGainWindow.size();
    auto firstSafeGain = 1.0f;
    auto secondSafeGain = 1.0f;
    auto thirdSafeGain = 1.0f;
    for (auto const safeGain : state.ceilingTargetGainWindow) {
        if (safeGain <= firstSafeGain) {
            thirdSafeGain = secondSafeGain;
            secondSafeGain = firstSafeGain;
            firstSafeGain = safeGain;
        } else if (safeGain <= secondSafeGain) {
            thirdSafeGain = secondSafeGain;
            secondSafeGain = safeGain;
        } else if (safeGain < thirdSafeGain) {
            thirdSafeGain = safeGain;
        }
    }
    auto const orderStatisticGain = truePeakLimit ? firstSafeGain : thirdSafeGain;
    auto const nearCeiling = hermiteShape(juce::jlimit(0.0f, 1.0f, (currentToCeilingDb + 1.25f) / 4.0f));
    auto const peakHoldBlend =
        juce::jlimit(0.0f, 0.72f, (hermiteShape(juce::jlimit(0.0f, 1.0f, overDb / 9.0f)) * (truePeakLimit ? 0.46f : 0.34f))
                                      + (nearCeiling * adaptiveRelease * 0.18f));
    // DAFx order-statistics limiter idea: hold a short window of safe gains so
    // smoothing does not release into a clipped output after a recent peak.
    auto const orderSafeTargetGain = juce::jmap(peakHoldBlend, protectedTargetGain, std::min(protectedTargetGain, orderStatisticGain));
    auto const programReleaseScale = juce::jmap(juce::jlimit(0.0f, 1.0f, overDb / 12.0f), 0.72f, 1.55f);
    auto const attackTime = (0.035f / std::max(0.35f, mode.finalLimiterStrength * juce::jlimit(0.65f, 1.45f, limiterStrengthScale)))
                            * (1.0f + (punch * 2.4f));
    auto const attack = fastSmoothingCoefficient(attackTime, sampleRate);
    auto const adaptive = juce::jlimit(0.0f, 1.0f, adaptiveRelease);
    auto const sustainedScale = juce::jmap(adaptive, 1.0f, 1.0f + (programReleaseScale * 0.85f));
    auto const transientScale = juce::jmap(adaptive, 1.0f, 0.62f);
    auto const releaseShapeAmount = juce::jlimit(0.0f, 1.0f, releaseShape);
    auto const fastBias = juce::jlimit(0.0f, 1.0f, (0.5f - releaseShapeAmount) * 2.0f);
    auto const smoothBias = juce::jlimit(0.0f, 1.0f, (releaseShapeAmount - 0.5f) * 2.0f);
    auto releaseTime = overDb > 2.0f ? releaseMs * sustainedScale : releaseMs * transientScale;
    auto const sustainedPeak = adaptive * hermiteShape(juce::jlimit(0.0f, 1.0f, overDb / 10.0f)) * (1.0f - ceilingTransient);
    releaseTime *= 1.0f + (sustainedPeak * 0.45f);
    releaseTime *= 1.0f - (fastBias * 0.38f) + (smoothBias * 0.85f);
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    auto const recoverySafety = (1.0f - juce::jlimit(0.0f, 1.0f, state.spectralLowRisk))
                                * (1.0f - juce::jlimit(0.0f, 1.0f, state.spectralTransientRisk))
                                * (1.0f - smoothBias);
    auto const recoveryHeadroom = hermiteShape(juce::jlimit(0.0f, 1.0f, -currentToCeilingDb / 6.0f));
    releaseTime *= 1.0f - 0.45f * adaptive * recoverySafety * recoveryHeadroom;
#endif
    auto const release = fastSmoothingCoefficient(std::max(2.0f, releaseTime), sampleRate);
    auto const coefficient = orderSafeTargetGain < state.ceilingGain ? attack : release;
    state.ceilingGain = coefficient * state.ceilingGain + (1.0f - coefficient) * orderSafeTargetGain;
    if (truePeakLimit) {
        auto const predictiveGuard =
            hermiteShape(juce::jlimit(0.0f, 1.0f, overDb / 7.0f))
            * juce::jlimit(0.05f, 0.34f, 0.10f + (truePeakMarginDb * 0.14f) + (limiterStrengthScale * 0.055f))
            * juce::jlimit(0.72f, 1.0f, 1.0f - punch * 0.18f);
        if (predictiveGuard > 0.0001f) {
            auto const guardedGain = state.ceilingGain + ((orderSafeTargetGain - state.ceilingGain) * predictiveGuard);
            state.ceilingGain = std::min(state.ceilingGain, guardedGain);
        }
    }

    auto const limited = sample * std::min(1.0f, state.ceilingGain);
    auto const limitedMagnitude = std::abs(limited);
    auto const kneeStart = safeCeiling * juce::jmap(punch, 0.92f, 0.985f);
    float output = limited;

    if (limitedMagnitude > kneeStart && limitedMagnitude < safeCeiling) {
        auto const kneeWidth = safeCeiling - kneeStart;
        auto const kneePosition = (limitedMagnitude - kneeStart) / std::max(kneeWidth, 0.000001f);
        auto const curved = kneeStart + (kneeWidth * fastHalfPiSin(kneePosition));
        output = (limited < 0.0f ? -1.0f : 1.0f) * std::min(curved, safeCeiling);
    }

    // Raise sustained low and medium amplitudes after the limiter envelope,
    // while fixing both zero and the established safe ceiling as endpoints.
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    constexpr auto maxRmsContourAmount = 0.80f;
#else
    constexpr auto maxRmsContourAmount = 0.30f;
#endif
    auto const safeContourAmount = juce::jlimit(0.0f, maxRmsContourAmount, rmsContourAmount);
    if (safeContourAmount > 0.0001f) {
        auto const normalized = output / safeCeiling;
        auto const normalizedMagnitude = std::abs(normalized);
        auto const outputMagnitude = std::abs(output);
        auto const safeAttack = juce::jlimit(0.0f, 0.999999f, contourAttackCoefficient);
        auto const safeRelease = juce::jlimit(0.0f, 0.999999f, contourReleaseCoefficient);
        if (!std::isfinite(state.rmsContourPeakEnvelope)
            || !std::isfinite(state.rmsContourEnergyEnvelope)
            || !std::isfinite(state.rmsContourAdaptiveAmount)) {
            state.rmsContourPeakEnvelope = outputMagnitude;
            state.rmsContourEnergyEnvelope = outputMagnitude * outputMagnitude;
            state.rmsContourAdaptiveAmount = 0.0f;
        }
        auto const releasedPeak = (safeRelease * state.rmsContourPeakEnvelope) + ((1.0f - safeRelease) * outputMagnitude);
        state.rmsContourPeakEnvelope = std::max(outputMagnitude, releasedPeak);
        auto const outputEnergy = outputMagnitude * outputMagnitude;
        auto const energyCoefficient = outputEnergy > state.rmsContourEnergyEnvelope ? safeAttack : safeRelease;
        state.rmsContourEnergyEnvelope = (energyCoefficient * state.rmsContourEnergyEnvelope)
                                         + ((1.0f - energyCoefficient) * outputEnergy);
        auto const crestRatioSquared = (state.rmsContourPeakEnvelope * state.rmsContourPeakEnvelope)
                                       / std::max(state.rmsContourEnergyEnvelope, 1.0e-8f);
        auto const crestOpportunity = hermiteShape(juce::jlimit(0.0f, 1.0f, (crestRatioSquared - 1.60f) / 2.40f));
        auto const adaptiveTarget = juce::jlimit(0.0f, 0.40f, safeContourAmount * 1.35f * crestOpportunity);
        auto const adaptiveCoefficient = adaptiveTarget > state.rmsContourAdaptiveAmount ? safeAttack : safeRelease;
        state.rmsContourAdaptiveAmount = (adaptiveCoefficient * state.rmsContourAdaptiveAmount)
                                         + ((1.0f - adaptiveCoefficient) * adaptiveTarget);
        if (normalizedMagnitude < 1.0f) {
            auto const shoulder = 1.0f - normalizedMagnitude;
            auto const bodyGate = hermiteShape(juce::jlimit(0.0f, 1.0f, (normalizedMagnitude - 0.025f) / 0.22f));
            auto const contourGain = 1.0f + (safeContourAmount * shoulder * shoulder)
                                     + (state.rmsContourAdaptiveAmount * bodyGate * shoulder * shoulder);
            output = normalized * contourGain * safeCeiling;
        }
    }

    if (std::abs(output) > safeCeiling) {
        output = sign * safeCeiling;
    }

    auto const reductionDb = std::max(0.0f, fastGainToDb(std::max(magnitude, 0.000001f)
                                                         / std::max(std::abs(output), 0.000001f)));
    return {output, reductionDb};
}

SpectralMaximizer::ClipResult SpectralMaximizer::applyFinalClip(float sample, float ceilingGain, float amount, float previousInput) {
    auto const safeAmount = juce::jlimit(0.0f, 1.0f, amount);
    if (safeAmount <= 0.0001f || !std::isfinite(sample)) {
        return {std::isfinite(sample) ? sample : 0.0f, std::isfinite(sample) ? 0.0f : 24.0f};
    }

    auto const clipThreshold = std::max(0.000001f, ceilingGain * juce::jmap(safeAmount, 1.18f, 0.90f));
    auto const slewRisk =
        hermiteShape(juce::jlimit(0.0f, 1.0f, std::abs(sample - (std::isfinite(previousInput) ? previousInput : sample))
                                                   / (clipThreshold * 2.25f)));
    auto const transientRoundDodge = juce::jlimit(0.72f, 1.0f, 1.0f - (slewRisk * 0.18f));
    auto const drive = 1.0f + (safeAmount * juce::jlimit(1.70f, 2.18f, 2.12f - (slewRisk * 0.24f)));
    auto const normalized = sample / clipThreshold;
    auto const previousNormalized = (std::isfinite(previousInput) ? previousInput : sample) / clipThreshold;
    auto const soft = antialiasedTanh(normalized, previousNormalized, drive) * clipThreshold;
    auto const rounded = roundedHardClip(normalized, juce::jmap(safeAmount, 0.34f, 0.14f)) * clipThreshold;
    auto const hard = juce::jlimit(-clipThreshold, clipThreshold, sample);
    auto const blampStyleCorner = rounded + ((hard - rounded) * juce::jlimit(0.0f, 0.14f, safeAmount * 0.075f * transientRoundDodge));
    auto const clipBlend = juce::jlimit(0.0f, 0.30f, safeAmount * 0.22f * transientRoundDodge);
    auto const clipped = soft + ((blampStyleCorner - soft) * clipBlend);
    auto shapedClip = clipped;
#if !defined(PEAKEATER_SMALL_VARIANT)
    auto const faustClip = faustPrecisionClip(normalized, previousNormalized, safeAmount) * clipThreshold;
   #if defined(PEAKEATER_SPECTRAL_2_VARIANT)
    auto const precisionBlend = juce::jlimit(0.0f, 0.68f, 0.28f + safeAmount * 0.42f);
   #else
    auto const precisionBlend = juce::jlimit(0.0f, 0.52f, 0.20f + safeAmount * 0.34f);
   #endif
    shapedClip += (faustClip - shapedClip) * precisionBlend;
#endif
    auto const output = sample + ((shapedClip - sample) * juce::jlimit(0.0f, 0.68f, safeAmount * transientRoundDodge));
    auto const reductionDb = std::max(0.0f, fastGainToDb(std::max(std::abs(sample), 0.000001f)
                                                         / std::max(std::abs(output), 0.000001f)));
    return {output, reductionDb};
}

float SpectralMaximizer::applySaturation(float sample, float amount, float densityLift, float driveDb, float transientProtect,
                                         SaturationSettings const& settings, float saturationDensity, float previousInput) {
    auto const safeAmount = juce::jlimit(0.0f, 1.0f, amount);
    if (safeAmount <= 0.0001f) {
        return sample;
    }

    auto const typeDensity = juce::jlimit(0.0f, 1.0f, densityLift);
    auto const driveAssist = juce::jlimit(0.0f, 1.0f, driveDb / 42.0f);
    auto const userDensity = juce::jlimit(0.0f, 1.0f, saturationDensity);
    auto const protect = juce::jlimit(0.0f, 1.0f, transientProtect * settings.transientProtection);
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    auto const densityEmphasis = juce::jlimit(0.0f, 1.0f, (userDensity - 0.35f) / 0.65f);
    auto const shapedAmount = safeAmount * settings.driveScale
                              * (0.14f + (typeDensity * (0.14f + densityEmphasis * 0.03f))
                                 + (driveAssist * (0.035f + densityEmphasis * 0.01f))
                                 + (userDensity * settings.densityScale * (0.18f + densityEmphasis * 0.12f)));
    auto const saturationDrive = 1.0f + shapedAmount
                                            * (1.22f + (densityEmphasis * 0.10f)
                                               + userDensity * (0.66f + densityEmphasis * 0.28f));
#else
    auto const shapedAmount = safeAmount * settings.driveScale
                              * (0.14f + (typeDensity * 0.14f) + (driveAssist * 0.035f)
                                 + (userDensity * settings.densityScale * 0.18f));
    auto const saturationDrive = 1.0f + shapedAmount * (1.22f + userDensity * 0.66f);
#endif
    auto const soft = antialiasedTanh(sample, previousInput, saturationDrive);
    auto const evenHarmonic = (sample * sample - std::abs(sample) * 0.5f) * 0.03f * shapedAmount * settings.evenAmount;
    auto const cubic = sample - ((sample * sample * sample) * 0.048f * shapedAmount * settings.oddAmount);
    auto const tapeRounded = fastAtan(sample * saturationDrive) / fastAtan(saturationDrive);
    auto const rounded = soft + ((tapeRounded - soft) * settings.tapeSoftness);
    auto const saturated = (rounded * 0.55f) + (cubic * 0.41f) + evenHarmonic;
    auto const transientSafeMix = safeAmount * (1.0f - protect * 0.92f);
    auto shapedSaturation = saturated;
#if !defined(PEAKEATER_SMALL_VARIANT)
    auto const faustSat = faustPrecisionSaturator(sample, previousInput, shapedAmount, saturationDrive - 1.0f, userDensity, protect);
   #if defined(PEAKEATER_SPECTRAL_2_VARIANT)
    auto const precisionBlend = juce::jlimit(0.0f, 0.62f, 0.22f + shapedAmount * 0.86f);
   #else
    auto const precisionBlend = juce::jlimit(0.0f, 0.48f, 0.16f + shapedAmount * 0.72f);
   #endif
    shapedSaturation += (faustSat - shapedSaturation) * precisionBlend;
#endif
    return sample + ((shapedSaturation - sample) * transientSafeMix);
}

float SpectralMaximizer::smoothingCoefficient(float timeMs, double sampleRate) {
    auto const safeTimeMs = std::max(0.01f, timeMs);
    return std::exp(-1.0f / (0.001f * safeTimeMs * static_cast<float>(sampleRate)));
}

float SpectralMaximizer::fastSmoothingCoefficient(float timeMs, double sampleRate) {
    auto const safeTimeMs = std::max(0.01f, timeMs);
    auto const x = 1.0f / (0.001f * safeTimeMs * static_cast<float>(sampleRate));
    return juce::jlimit(0.0f, 0.999999f, 1.0f / (1.0f + x));
}

float SpectralMaximizer::hermiteShape(float amount) {
    auto const x = juce::jlimit(0.0f, 1.0f, amount);
    return x * x * (3.0f - (2.0f * x));
}

float SpectralMaximizer::smoothSuppressionShape(float amount, float smoothBias) {
    auto const x = hermiteShape(amount);
    auto const smooth = x * x * (3.0f - (2.0f * x));
    return juce::jmap(juce::jlimit(0.0f, 1.0f, smoothBias), x, smooth);
}

float SpectralMaximizer::smoothToward(float current, float target, float timeMs, double sampleRate) {
    auto const coefficient = smoothingCoefficient(timeMs, sampleRate);
    return (coefficient * current) + ((1.0f - coefficient) * target);
}

float SpectralMaximizer::onePoleCoefficient(float frequencyHz, double sampleRate) {
    auto const safeFrequency = juce::jlimit(1.0f, static_cast<float>(sampleRate * 0.45), frequencyHz);
    return 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * safeFrequency / static_cast<float>(sampleRate));
}

float SpectralMaximizer::mixDb(float dryDb, float wetDb, float amount) {
    return dryDb + ((wetDb - dryDb) * juce::jlimit(0.0f, 1.0f, amount));
}

size_t SpectralMaximizer::activeBandCount(size_t baseCount, size_t multiplier, size_t maximum) {
    auto const safeMultiplier = juce::jlimit<size_t>(1, maxRealtimeBandMultiplier, multiplier);
    auto const count = baseCount * safeMultiplier;
    return juce::jlimit<size_t>(baseCount, maximum, count);
}

SpectralMaximizer::BandSettings SpectralMaximizer::bandSettingsForFrequency(float frequencyHz, ModeSettings const& mode) {
    auto const safeFrequency = std::max(20.0f, frequencyHz);
    if (safeFrequency < 220.0f) {
        return mode.bands[0];
    }
    if (safeFrequency > 4200.0f) {
        return mode.bands[2];
    }

    if (safeFrequency < 900.0f) {
        auto low = mode.bands[0];
        auto const mid = mode.bands[1];
        auto const amount = juce::jlimit(0.0f, 1.0f, (std::log2(safeFrequency / 220.0f) / std::log2(900.0f / 220.0f)));
        low.ratio = juce::jmap(amount, low.ratio, mid.ratio);
        low.attackMs = juce::jmap(amount, low.attackMs, mid.attackMs);
        low.releaseMs = juce::jmap(amount, low.releaseMs, mid.releaseMs);
        low.weight = juce::jmap(amount, low.weight, mid.weight);
        low.thresholdOffsetDb = juce::jmap(amount, low.thresholdOffsetDb, mid.thresholdOffsetDb);
        low.transientScale = juce::jmap(amount, low.transientScale, mid.transientScale);
        return low;
    }

    auto mid = mode.bands[1];
    auto const high = mode.bands[2];
    auto const amount = juce::jlimit(0.0f, 1.0f, (std::log2(safeFrequency / 900.0f) / std::log2(4200.0f / 900.0f)));
    mid.ratio = juce::jmap(amount, mid.ratio, high.ratio);
    mid.attackMs = juce::jmap(amount, mid.attackMs, high.attackMs);
    mid.releaseMs = juce::jmap(amount, mid.releaseMs, high.releaseMs);
    mid.weight = juce::jmap(amount, mid.weight, high.weight);
    mid.thresholdOffsetDb = juce::jmap(amount, mid.thresholdOffsetDb, high.thresholdOffsetDb);
    mid.transientScale = juce::jmap(amount, mid.transientScale, high.transientScale);
    return mid;
}

float SpectralMaximizer::spectralTargetOffsetDb(float frequencyHz, ModeSettings const& mode, float tone,
                                                ToneStyle toneStyle, float quality) {
    auto const safeFrequency = std::max(20.0f, frequencyHz);
    auto const octavesFrom1k = std::log2(safeFrequency / 1000.0f);
    auto const pinkTarget = -3.0f * octavesFrom1k;
    auto const lowHeadroom = safeFrequency < 160.0f ? 2.8f * (1.0f - mode.lowDetectorScale) : 0.0f;
    auto const presenceShape = std::exp(-0.5f * std::pow(std::log2(safeFrequency / 5500.0f) / 0.58f, 2.0f));
    auto const presenceLift = presenceShape * mode.loudnessFocus * (2.35f + (quality * 0.35f));
    auto const highProtection = safeFrequency > 7600.0f ? mode.spectralControlScale * -(0.038f + (quality * 0.018f)) : 0.0f;
    auto const toneDepth = 1.0f + (juce::jlimit(0.0f, 1.0f, quality) * 1.1f);
    auto const toneTilt = tone * toneDepth * (safeFrequency < 300.0f ? 2.0f : (safeFrequency > 4500.0f ? -2.0f : 0.0f));

    auto styleOffset = 0.0f;
    switch (toneStyle) {
        case ToneStyle::Warm:
            styleOffset += safeFrequency < 260.0f ? 1.15f + (quality * 0.75f) : 0.0f;
            styleOffset += safeFrequency > 5200.0f ? -1.15f - (quality * 0.9f) : 0.0f;
            styleOffset += std::exp(-0.5f * std::pow(std::log2(safeFrequency / 420.0f) / 0.68f, 2.0f)) * (0.55f + quality * 0.45f);
            break;
        case ToneStyle::Bright:
            styleOffset += safeFrequency > 3600.0f ? 1.25f + (quality * 1.05f) : 0.0f;
            styleOffset += safeFrequency < 140.0f ? -0.55f - (quality * 0.45f) : 0.0f;
            styleOffset += presenceShape * (0.85f + quality * 0.8f);
            break;
        case ToneStyle::Clean:
        default:
            styleOffset += std::exp(-0.5f * std::pow(std::log2(safeFrequency / 2600.0f) / 1.15f, 2.0f)) * quality * 0.35f;
            break;
    }

    return pinkTarget + lowHeadroom + presenceLift + highProtection + toneTilt + styleOffset;
}

float SpectralMaximizer::spectralToneGainDb(float frequencyHz, float tone, ToneStyle toneStyle, float quality) {
    auto const safeFrequency = std::max(20.0f, frequencyHz);
    auto const safeQuality = juce::jlimit(0.0f, 1.0f, quality);
    auto const lowShelf = 1.0f / (1.0f + std::pow(safeFrequency / 260.0f, 1.35f));
    auto const highShelf = 1.0f / (1.0f + std::pow(3100.0f / safeFrequency, 1.55f));
    auto const lowMidBell = std::exp(-0.5f * std::pow(std::log2(safeFrequency / 430.0f) / 0.75f, 2.0f));
    auto const presenceBell = std::exp(-0.5f * std::pow(std::log2(safeFrequency / 5200.0f) / 0.72f, 2.0f));
    auto const toneDepthDb = 6.0f + (safeQuality * 5.0f);

    auto gainDb = tone * toneDepthDb * (lowShelf - highShelf);

    switch (toneStyle) {
        case ToneStyle::Warm:
            gainDb += (1.65f + safeQuality * 1.15f) * lowShelf;
            gainDb += (0.85f + safeQuality * 0.75f) * lowMidBell;
            gainDb -= (1.75f + safeQuality * 1.35f) * highShelf;
            break;
        case ToneStyle::Bright:
            gainDb -= (0.95f + safeQuality * 0.75f) * lowShelf;
            gainDb += (1.15f + safeQuality * 0.85f) * presenceBell;
            gainDb += (2.05f + safeQuality * 1.55f) * highShelf;
            break;
        case ToneStyle::Clean:
        default:
            gainDb += safeQuality * 0.35f * presenceBell;
            break;
    }

    return juce::jlimit(-7.5f, 7.5f, gainDb);
}

float SpectralMaximizer::spectralRatioForFrequency(float frequencyHz, float baseRatio, float rolloff) {
    auto const highAmount = juce::jlimit(0.0f, 1.0f, (frequencyHz - 4500.0f) / 12000.0f);
    return std::max(1.05f, baseRatio - (highAmount * rolloff * 1.35f));
}

void SpectralMaximizer::updateBankLayout(ModeSettings const& mode, float quality, size_t activeLimiterBands, size_t activeSpectralBins) {
    activeLimiterBands = juce::jlimit<size_t>(2, limiterBandCount, activeLimiterBands);
    activeSpectralBins = juce::jlimit<size_t>(2, spectralBinCount, activeSpectralBins);

    auto const limiterQualityWeight = juce::jmap(quality, 0.35f, 0.9f);
    for (size_t split = 0; split < activeLimiterBands - 1; ++split) {
        mLimiterCrossoverCoefficients[split] =
            onePoleCoefficient(crossoverFrequency(split, activeLimiterBands, gLimiterMinHz, gLimiterMaxHz), mSampleRate);
    }

    for (size_t band = 0; band < activeLimiterBands; ++band) {
        auto const frequencyHz = centerFrequency(band, activeLimiterBands, gLimiterMinHz, gLimiterMaxHz);
        auto const bark = hzToBark(frequencyHz);
        auto const presenceBarkWeight = std::exp(-0.5f * std::pow((bark - 16.7f) / 3.0f, 2.0f));
        auto const lowBarkWeight = juce::jlimit(0.0f, 1.0f, (6.5f - bark) / 5.5f);
        auto const settings = bandSettingsForFrequency(frequencyHz, mode);
        mLimiterCenterFrequencies[band] = frequencyHz;
        mLimiterBandSettings[band] = settings;
        mLimiterTargetOffsets[band] =
            spectralTargetOffsetDb(frequencyHz, mode, mSmoothedTone, mToneStyle, quality) + (settings.thresholdOffsetDb * 0.55f)
            - (settings.weight * limiterQualityWeight * (0.92f + presenceBarkWeight * 0.10f));
        mLimiterFocusWeights[band] =
            juce::jlimit(-0.18f, 1.04f, (frequencyHz > 4500.0f ? 0.78f : (frequencyHz > 700.0f ? 0.30f : -0.12f))
                                              + (presenceBarkWeight * 0.24f) - (lowBarkWeight * 0.08f));
        mLimiterLowBandWeights[band] = juce::jlimit(0.0f, 1.0f, (260.0f - frequencyHz) / 220.0f);
        mLimiterHighTransientWeights[band] = juce::jlimit(0.0f, 1.0f, (frequencyHz - 1800.0f) / 6200.0f);
        mLimiterInverseRatios[band] = 1.0f / std::max(1.0f, settings.ratio);

        auto const lookaheadAmount = juce::jlimit(0.0f, 1.0f, mLookaheadMs / 50.0f);
        auto const attackDivider = 1.0f + (lookaheadAmount * mode.lookaheadStrength * settings.transientScale);
        mLimiterAttackSlowCoefficients[band] =
            smoothingCoefficient(std::max(0.01f, (settings.attackMs * mAttackMs) / attackDivider), mSampleRate);
        mLimiterAttackFastCoefficients[band] =
            smoothingCoefficient(std::max(0.01f, (settings.attackMs * mAttackMs * 0.36f) / attackDivider), mSampleRate);
        mLimiterReleaseFastCoefficients[band] =
            smoothingCoefficient(std::max(1.0f, (settings.releaseMs + mReleaseMs) * 0.86f), mSampleRate);
        mLimiterReleaseSlowCoefficients[band] =
            smoothingCoefficient(std::max(1.0f, (settings.releaseMs + mReleaseMs) * 1.62f), mSampleRate);
        mLimiterTransientFastCoefficients[band] = smoothingCoefficient(std::max(0.05f, settings.attackMs * 0.65f), mSampleRate);
        mLimiterTransientSlowCoefficients[band] = smoothingCoefficient(std::max(8.0f, settings.releaseMs * 0.55f), mSampleRate);
    }

    auto const baseRatio = 1.22f + (mode.spectralControlScale * 2.45f);
    mLimiterKneeDb = std::max(0.0f, mode.kneeDb);
    mLimiterInverseTwoKneeDb = mLimiterKneeDb > 0.001f ? 1.0f / (2.0f * mLimiterKneeDb) : 0.0f;
    mSpectralFastKneeDb = std::max(0.0f, mode.kneeDb * 0.48f);
    mSpectralFastInverseTwoKneeDb = mSpectralFastKneeDb > 0.001f ? 1.0f / (2.0f * mSpectralFastKneeDb) : 0.0f;
    mSpectralSlowKneeDb = std::max(0.0f, mode.kneeDb * 1.15f);
    mSpectralSlowInverseTwoKneeDb = mSpectralSlowKneeDb > 0.001f ? 1.0f / (2.0f * mSpectralSlowKneeDb) : 0.0f;
    mSpectralAttackCoefficient = smoothingCoefficient(std::max(0.08f, mAttackMs * 0.6f), mSampleRate);
    mSpectralReleaseCoefficient = smoothingCoefficient(std::max(12.0f, mReleaseMs * 0.55f), mSampleRate);
    auto const releaseShape = juce::jlimit(0.0f, 1.0f, mReleaseShape);
    auto const fastBias = juce::jlimit(0.0f, 1.0f, (0.5f - releaseShape) * 2.0f);
    auto const smoothBias = juce::jlimit(0.0f, 1.0f, (releaseShape - 0.5f) * 2.0f);
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    mSpectralFastAttackCoefficient = smoothingCoefficient(std::max(0.02f, mAttackMs * 0.12f), mSampleRate);
    mSpectralFastReleaseCoefficient =
        smoothingCoefficient(std::max(2.0f, (mReleaseMs * (0.08f - fastBias * 0.025f)) + (smoothBias * 10.0f)), mSampleRate);
    mSpectralSlowAttackCoefficient = smoothingCoefficient(std::max(0.5f, mAttackMs * (0.85f + smoothBias * 0.55f)), mSampleRate);
    mSpectralSlowReleaseCoefficient =
        smoothingCoefficient(std::max(12.0f, mReleaseMs * (0.90f + smoothBias * 0.80f)), mSampleRate);
#else
    mSpectralFastAttackCoefficient = smoothingCoefficient(std::max(0.035f, mAttackMs * 0.24f), mSampleRate);
    mSpectralFastReleaseCoefficient =
        smoothingCoefficient(std::max(5.0f, (mReleaseMs * (0.13f - fastBias * 0.045f)) + (smoothBias * 18.0f)), mSampleRate);
    mSpectralSlowAttackCoefficient = smoothingCoefficient(std::max(1.8f, mAttackMs * (1.45f + smoothBias * 0.75f)), mSampleRate);
    mSpectralSlowReleaseCoefficient =
        smoothingCoefficient(std::max(45.0f, mReleaseMs * (1.25f + smoothBias * 1.1f)), mSampleRate);
#endif
    for (size_t split = 0; split < activeSpectralBins - 1; ++split) {
        mSpectralCrossoverCoefficients[split] =
            onePoleCoefficient(crossoverFrequency(split, activeSpectralBins, gSpectralMinHz, gSpectralMaxHz), mSampleRate);
    }

    for (size_t bin = 0; bin < activeSpectralBins; ++bin) {
        auto const frequencyHz = centerFrequency(bin, activeSpectralBins, gSpectralMinHz, gSpectralMaxHz);
        auto const bark = hzToBark(frequencyHz);
        auto const presenceBarkWeight = std::exp(-0.5f * std::pow((bark - 16.7f) / 3.0f, 2.0f));
        auto const airBarkWeight = std::exp(-0.5f * std::pow((bark - 21.0f) / 2.6f, 2.0f));
        mSpectralCenterFrequencies[bin] = frequencyHz;
        mSpectralTargetOffsets[bin] = spectralTargetOffsetDb(frequencyHz, mode, mSmoothedTone, mToneStyle, quality);
        mSpectralToneGains[bin] = spectralToneGainDb(frequencyHz, mSmoothedTone, mToneStyle, quality);
        mSpectralRatios[bin] =
            spectralRatioForFrequency(frequencyHz, baseRatio, mode.loudnessFocus) * (1.0f + presenceBarkWeight * 0.055f);
        mSpectralFastInverseRatios[bin] = 1.0f / std::max(1.0f, mSpectralRatios[bin] + 0.35f);
        mSpectralSlowInverseRatios[bin] = 1.0f / std::max(1.08f, mSpectralRatios[bin] * 0.72f);
        mSpectralHighGuardWeights[bin] =
            juce::jlimit(0.0f, 1.0f, ((std::log2(std::max(1000.0f, frequencyHz) / 3200.0f) / std::log2(18000.0f / 3200.0f)) * 0.72f)
                                          + (presenceBarkWeight * 0.34f) - (airBarkWeight * 0.12f));
        mSpectralLowLockWeights[bin] = juce::jlimit(0.0f, 1.0f, (220.0f - frequencyHz) / 170.0f);
        mSpectralMidDensityWeights[bin] =
            juce::jlimit(0.0f, 1.0f, 1.0f - std::abs(std::log2(std::max(80.0f, frequencyHz) / 1800.0f)) / 2.9f);
        mSpectralCenterFrequencySquares[bin] = frequencyHz * frequencyHz;
    }
}

float SpectralMaximizer::processLimiterBank(ChannelState& state, float detectorInput, float signalInput, ModeSettings const& mode,
                                            float thresholdDriveDb, float ceilingDb, size_t activeBands, float adaptiveRelease,
                                            float lowProtect, float userGrLimitDb, float punchProtect, float releaseShape,
                                            float lookaheadAmount, int holdSamples, float overshootReleaseCoefficient,
                                            PeakBudgetControls const& peakBudget,
                                            float& gainReductionDb) const {
    activeBands = juce::jlimit<size_t>(2, limiterBandCount, activeBands);
    detectorInput = std::isfinite(detectorInput) ? detectorInput : 0.0f;
    signalInput = std::isfinite(signalInput) ? signalInput : 0.0f;

    auto& detectorBands = state.limiterDetectorBands;
    auto& signalBands = state.limiterSignalBands;
    float previousDetectorLowpass = 0.0f;
    float previousSignalLowpass = 0.0f;

#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    auto const limiterIdleInput = std::max(std::abs(detectorInput), std::abs(signalInput));
    if (limiterIdleInput <= 1.0e-8f && peakBudget.peakPressure <= 0.001f) {
        auto limiterStateIdle = std::abs(state.limiterOvershootGain - 1.0f) <= 0.0005f;
        for (size_t split = 0; split < activeBands - 1 && limiterStateIdle; ++split) {
            limiterStateIdle = std::abs(state.limiterDetectorLowpass[split]) <= 1.0e-7f
                               && std::abs(state.limiterSignalLowpass[split]) <= 1.0e-7f;
        }

        for (size_t band = 0; band < activeBands && limiterStateIdle; ++band) {
            limiterStateIdle = std::abs(state.limiterEnvelope[band]) <= 1.0e-7f
                               && std::abs(state.limiterTransientFastEnvelope[band]) <= 1.0e-7f
                               && std::abs(state.limiterTransientSlowEnvelope[band]) <= 1.0e-7f
                               && std::abs(state.limiterPreviousMagnitude[band]) <= 1.0e-7f
                               && state.limiterHoldSamples[band] <= 0;
        }

        if (limiterStateIdle) {
            for (size_t split = 0; split < activeBands - 1; ++split) {
                state.limiterDetectorLowpass[split] = 0.0f;
                state.limiterSignalLowpass[split] = 0.0f;
            }

            for (size_t band = 0; band < activeBands; ++band) {
                state.limiterDetectorBands[band] = 0.0f;
                state.limiterSignalBands[band] = 0.0f;
                state.limiterEnvelope[band] = 0.0f;
                state.limiterTransientFastEnvelope[band] = 0.0f;
                state.limiterTransientSlowEnvelope[band] = 0.0f;
                state.limiterPreviousMagnitude[band] = 0.0f;
                state.limiterHoldSamples[band] = 0;
            }

            state.limiterOvershootGain = 1.0f;
            return signalInput;
        }
    }
#endif

    for (size_t split = 0; split < activeBands - 1; ++split) {
        auto const coefficient = mLimiterCrossoverCoefficients[split];
        state.limiterDetectorLowpass[split] += coefficient * (detectorInput - state.limiterDetectorLowpass[split]);
        state.limiterSignalLowpass[split] += coefficient * (signalInput - state.limiterSignalLowpass[split]);

        detectorBands[split] = state.limiterDetectorLowpass[split] - previousDetectorLowpass;
        signalBands[split] = state.limiterSignalLowpass[split] - previousSignalLowpass;
        previousDetectorLowpass = state.limiterDetectorLowpass[split];
        previousSignalLowpass = state.limiterSignalLowpass[split];
    }

    detectorBands[activeBands - 1] = detectorInput - previousDetectorLowpass;
    signalBands[activeBands - 1] = signalInput - previousSignalLowpass;

    auto const driveAmount = juce::jlimit(0.0f, 1.0f, thresholdDriveDb / 48.0f);
    auto const driveSafety = juce::jlimit(0.0f, 1.0f, (thresholdDriveDb - 4.0f) / 32.0f);
    auto const heavyDriveSafety = juce::jlimit(0.0f, 1.0f, (thresholdDriveDb - 16.0f) / 32.0f);
    auto const extremeDriveSafety = juce::jlimit(0.0f, 1.0f, (thresholdDriveDb - 30.0f) / 18.0f);
    auto const lookaheadDriveControl =
        hermiteShape(lookaheadAmount) * juce::jlimit(0.0f, 1.0f, 0.34f + (driveAmount * 0.38f)
                                                                       + (driveSafety * 0.20f) + (heavyDriveSafety * 0.12f));
    auto const grLimitDb = juce::jlimit(0.0f, 18.0f, userGrLimitDb);
    auto const punchProtectAmount = juce::jlimit(0.0f, 1.0f, punchProtect);
    auto const lowProtectAmount = juce::jlimit(0.0f, 1.0f, lowProtect);
    auto const releaseShapeAmount = juce::jlimit(0.0f, 1.0f, releaseShape);
    auto const fastShapeBias = juce::jlimit(0.0f, 1.0f, (0.5f - releaseShapeAmount) * 2.0f);
    auto const smoothShapeBias = juce::jlimit(0.0f, 1.0f, (releaseShapeAmount - 0.5f) * 2.0f);
    auto const adaptive = juce::jlimit(0.0f, 1.0f, adaptiveRelease);
    auto const driveTargetRelax = 0.020f - (driveSafety * 0.0040f) - (heavyDriveSafety * 0.0025f) - (extremeDriveSafety * 0.0015f);
    auto const driveRelax = juce::jlimit(0.50f, 1.05f,
                                         juce::jmap(juce::jlimit(0.0f, 48.0f, thresholdDriveDb), 0.0f, 48.0f, 1.0f, 0.50f)
                                             + (driveSafety * 0.08f) + (heavyDriveSafety * 0.04f)
                                             + (extremeDriveSafety * 0.03f));
    auto const modeReductionLimit = std::min(mode.maxBandReductionDb, mode.grLimitDb);
    auto const boundedReductionLimit = std::min(modeReductionLimit, grLimitDb);
    auto const microGrDriveScale = juce::jlimit(0.0f, 1.65f, 0.36f + (driveAmount * 0.78f) + (lookaheadDriveControl * 0.42f));
    auto const transientBudgetScale = peakBudget.transientReserve * 0.18f;
    auto output = 0.0f;

    for (size_t band = 0; band < activeBands; ++band) {
        auto const settings = mLimiterBandSettings[band];
        auto const magnitude = std::abs(detectorBands[band]);
        auto const magnitudeRise = std::max(0.0f, magnitude - state.limiterPreviousMagnitude[band]);
        auto const transientRatio = magnitude > 0.000001f ? juce::jlimit(0.0f, 1.0f, magnitudeRise / magnitude) : 0.0f;
        auto const punchAmount = juce::jlimit(0.0f, 1.0f,
                                              punchProtectAmount * transientRatio
                                                  * juce::jlimit(0.35f, 1.25f, settings.transientScale * mode.transientScale));
        auto const predictedMagnitude =
            magnitude + (magnitudeRise * lookaheadAmount * mode.lookaheadStrength * settings.transientScale
                         * (1.0f + lookaheadDriveControl * 0.42f) * (1.0f - (punchAmount * 0.54f)));
        auto const preEnvelopeDb = fastGainToDb(predictedMagnitude);
        auto const targetDb = ceilingDb + mLimiterTargetOffsets[band] + (thresholdDriveDb * driveTargetRelax);
        auto const lowBandProtect = lowProtectAmount * mLimiterLowBandWeights[band];
        auto const transientBudgetHeadroomDb =
            punchAmount * peakBudget.transientReserve * juce::jlimit(0.0f, 2.2f, 0.85f + (mode.transientScale * 0.72f));
        auto const lowBudgetHeadroomDb = lowBandProtect * peakBudget.lowReserve * 1.25f;
        auto const protectedTargetDb = targetDb + (lowBandProtect * 2.2f) + (punchAmount * 2.6f)
                                       + transientBudgetHeadroomDb + lowBudgetHeadroomDb
                                       - (lookaheadDriveControl * juce::jlimit(0.0f, 0.42f, 0.16f + driveSafety * 0.22f)
                                          * (1.0f - punchAmount * 0.72f));
        auto const overDb = std::max(0.0f, preEnvelopeDb - protectedTargetDb);
        auto const programAmount = juce::jlimit(0.0f, 1.0f, overDb / 18.0f);
        auto attack = juce::jmap(programAmount, mLimiterAttackSlowCoefficients[band], mLimiterAttackFastCoefficients[band]);
        attack = juce::jmap(punchAmount, attack, mLimiterAttackSlowCoefficients[band]);
        auto const rawRelease = juce::jmap(programAmount, mLimiterReleaseFastCoefficients[band], mLimiterReleaseSlowCoefficients[band]);
        auto const adaptiveReleaseCoefficient =
            juce::jmap(adaptive, rawRelease, juce::jmap(transientRatio, mLimiterReleaseFastCoefficients[band],
                                                        mLimiterReleaseSlowCoefficients[band]));
        auto shapedRelease = juce::jmap(fastShapeBias, adaptiveReleaseCoefficient, mLimiterReleaseFastCoefficients[band]);
        shapedRelease = juce::jmap(smoothShapeBias, shapedRelease, mLimiterReleaseSlowCoefficients[band]);
        auto const densityHold = adaptive * hermiteShape(programAmount) * (1.0f - transientRatio)
                                 * (0.35f + (lowBandProtect * 0.65f));
        shapedRelease = juce::jmap(densityHold, shapedRelease, mLimiterReleaseSlowCoefficients[band]);
        auto const release = juce::jmap(lowBandProtect, shapedRelease, mLimiterReleaseSlowCoefficients[band]);
        auto coefficient = predictedMagnitude > state.limiterEnvelope[band] ? attack : release;

        if (predictedMagnitude >= state.limiterEnvelope[band]) {
            state.limiterHoldSamples[band] = holdSamples;
        } else if (state.limiterHoldSamples[band] > 0) {
            --state.limiterHoldSamples[band];
            coefficient = 1.0f;
        }

        state.limiterEnvelope[band] = coefficient * state.limiterEnvelope[band] + (1.0f - coefficient) * predictedMagnitude;

        auto const zeroLatencyControl =
            juce::jmax(state.limiterEnvelope[band], juce::jmap(lookaheadAmount, state.limiterEnvelope[band], predictedMagnitude));
        auto const envelopeDb = fastGainToDb(zeroLatencyControl);
        auto const lowReductionRelax = 1.0f - (lowBandProtect * (0.52f + peakBudget.lowReserve * 0.10f));
        auto const transientBudgetRelax = 1.0f - (punchAmount * transientBudgetScale);
        auto const maxReduction =
            -boundedReductionLimit * driveRelax * lowReductionRelax * transientBudgetRelax
            * juce::jlimit(0.45f, 1.4f, settings.weight);
        auto rawGainDb = compressionGainDb(envelopeDb, protectedTargetDb, mLimiterInverseRatios[band],
                                           mLimiterKneeDb, mLimiterInverseTwoKneeDb) * mode.spectralControlScale
                         * (1.0f + (driveSafety * 0.05f) + (heavyDriveSafety * 0.035f) + (extremeDriveSafety * 0.025f)
                            + (lookaheadDriveControl * 0.035f));
        rawGainDb *= juce::jlimit(0.72f, 1.0f, 1.0f - (punchAmount * peakBudget.transientReserve * 0.18f));
        auto const microGrWindow = hermiteShape(juce::jlimit(0.0f, 1.0f, (overDb - 0.30f) / 9.5f));
        auto const microGrDodge =
            juce::jlimit(0.30f, 1.0f, 1.0f - (punchAmount * (0.42f + peakBudget.transientReserve * 0.22f))
                                        - (transientRatio * punchProtectAmount * 0.16f)
                                        - (lowBandProtect * peakBudget.lowReserve * 0.08f));
        auto const microGrDb =
            peakBudget.microGr * microGrWindow * settings.weight * microGrDodge
            * microGrDriveScale;
        rawGainDb -= microGrDb;
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
        auto const highTransientBand = mLimiterHighTransientWeights[band];
        auto const spectral3TransientOpen =
            juce::jlimit(0.84f, 1.0f, 1.0f - (punchAmount * peakBudget.transientReserve * (0.055f + highTransientBand * 0.045f)));
        auto const spectral3LowMonoOpen =
            juce::jlimit(0.92f, 1.0f, 1.0f - (lowBandProtect * peakBudget.lowReserve * 0.030f));
        rawGainDb *= spectral3TransientOpen * spectral3LowMonoOpen;
#endif
        auto const gainDb = std::max(maxReduction, rawGainDb);
        gainReductionDb = std::max(gainReductionDb, std::max(0.0f, -gainDb));

        auto const sustainFocus = juce::jlimit(0.0f, 1.0f, peakBudget.densityBudget * (1.0f - transientRatio * 0.68f)
                                                                * (1.0f - lowBandProtect * 0.28f));
        auto const loudnessFocusAmount = mode.loudnessFocus * driveAmount * (1.0f + sustainFocus * 0.12f);
        auto const cleanLiftFocusDb =
            peakBudget.cleanLift * sustainFocus * mLimiterFocusWeights[band]
            * juce::jlimit(0.0f, 1.18f, 0.32f + (mode.loudnessFocus * 0.58f) + (peakBudget.upwardQuality * 0.18f));
        auto const sustainLiftFocusDb =
            peakBudget.sustainLift * sustainFocus * mLimiterFocusWeights[band]
            * juce::jlimit(0.0f, 0.62f, 0.18f + (mode.loudnessFocus * 0.30f) + (peakBudget.loudnessDistribution * 0.14f))
            * juce::jlimit(0.35f, 1.0f, 1.0f - (punchAmount * peakBudget.transientReserve * 0.32f)
                                             - (lowBandProtect * peakBudget.lowReserve * 0.14f));
        auto const focusGainDb = (loudnessFocusAmount * mLimiterFocusWeights[band] * 2.2f) + cleanLiftFocusDb + sustainLiftFocusDb;
        auto const transientRecovery = juce::jlimit(0.0f, 0.95f, (mTransientRecovery + (punchProtectAmount * 0.42f))
                                                                    * settings.weight * settings.transientScale * mode.transientScale);
        if (transientRecovery <= 0.0001f) {
            auto const limitedBand = signalBands[band] * fastDbToGain(focusGainDb + gainDb);
            state.limiterTransientFastEnvelope[band] *= 0.9995f;
            state.limiterTransientSlowEnvelope[band] *= 0.9995f;
            output += limitedBand;
        } else {
            auto const focusedBand = signalBands[band] * fastDbToGain(focusGainDb);
            auto const limitedBand = focusedBand * fastDbToGain(gainDb);
            auto const transientMagnitude = std::abs(focusedBand);
            if (transientMagnitude <= 0.000001f) {
                state.limiterTransientFastEnvelope[band] *= 0.9995f;
                state.limiterTransientSlowEnvelope[band] *= 0.9995f;
                output += limitedBand;
                state.limiterPreviousMagnitude[band] = magnitude;
                continue;
            }
            auto const transientFast = mLimiterTransientFastCoefficients[band];
            auto const transientSlow = mLimiterTransientSlowCoefficients[band];
            state.limiterTransientFastEnvelope[band] =
                transientFast * state.limiterTransientFastEnvelope[band] + (1.0f - transientFast) * transientMagnitude;
            state.limiterTransientSlowEnvelope[band] =
                transientSlow * state.limiterTransientSlowEnvelope[band] + (1.0f - transientSlow) * transientMagnitude;
            auto const transientDelta =
                std::max(0.0f, state.limiterTransientFastEnvelope[band] - state.limiterTransientSlowEnvelope[band]);
            auto const transientGate =
                juce::jlimit(0.0f, 1.0f, (transientDelta / std::max(0.000001f, state.limiterTransientSlowEnvelope[band])) * 2.4f);
            output += limitedBand + ((focusedBand - limitedBand) * transientRecovery * transientGate);
        }
        state.limiterPreviousMagnitude[band] = magnitude;
    }

    auto const outputOverDb = std::max(0.0f, fastGainToDb(std::abs(output)) - ceilingDb);
    auto const inputOverDb = std::max(0.0f, fastGainToDb(std::abs(signalInput)) - ceilingDb);
    auto const crossoverOvershootDb = std::max(0.0f, outputOverDb - inputOverDb - 0.18f);
    auto const compensationDb = -juce::jlimit(0.0f, 1.65f, crossoverOvershootDb * (0.22f + driveSafety * 0.05f));
    auto const targetCompensationGain = fastDbToGain(compensationDb);
    auto const compensationAttack = mLimiterOvershootAttackCoefficient;
    auto const compensationRelease = overshootReleaseCoefficient;
    auto const compensationCoefficient = targetCompensationGain < state.limiterOvershootGain ? compensationAttack : compensationRelease;
    state.limiterOvershootGain =
        compensationCoefficient * state.limiterOvershootGain + (1.0f - compensationCoefficient) * targetCompensationGain;
    output *= state.limiterOvershootGain;
    gainReductionDb = std::max(gainReductionDb, std::max(0.0f, -fastGainToDb(state.limiterOvershootGain)));

    return output;
}

float SpectralMaximizer::processSpectralBank(ChannelState& state, float detectorInput, float signalInput, ModeSettings const& mode,
                                             float thresholdDriveDb, float ceilingDb, float depth, size_t activeBins,
                                             PeakBudgetControls const& peakBudget, SpectralSharedControls const& shared,
                                             float& gainReductionDb) const {
    detectorInput = std::isfinite(detectorInput) ? detectorInput : 0.0f;
    signalInput = std::isfinite(signalInput) ? signalInput : 0.0f;
    if (depth <= 0.0001f) {
        state.spectralUpwardAppliedDb = 0.0f;
        state.spectralTransientRisk = 0.0f;
        state.spectralLowRisk = 0.0f;
        return signalInput;
    }

    activeBins = juce::jlimit<size_t>(2, spectralBinCount, activeBins);

    auto const spectral3IdleInput = std::max(std::abs(detectorInput), std::abs(signalInput));
    if (spectral3IdleInput <= 1.0e-7f && peakBudget.peakPressure <= 0.001f) {
        constexpr auto idleDecay = 0.995f;
        constexpr auto idleGainReturn = 0.998f;
        for (size_t split = 0; split < activeBins - 1; ++split) {
            state.spectralDetectorLowpass[split] *= idleDecay;
            state.spectralSignalLowpass[split] *= idleDecay;
            if (std::abs(state.spectralDetectorLowpass[split]) < 1.0e-12f) {
                state.spectralDetectorLowpass[split] = 0.0f;
            }
            if (std::abs(state.spectralSignalLowpass[split]) < 1.0e-12f) {
                state.spectralSignalLowpass[split] = 0.0f;
            }
        }
        for (size_t bin = 0; bin < activeBins; ++bin) {
            state.spectralEnvelope[bin] *= idleDecay;
            state.spectralFastEnvelope[bin] *= idleDecay;
            state.spectralSlowEnvelope[bin] *= idleDecay;
            state.spectralPreviousMagnitude[bin] = 0.0f;
            state.spectralDetectorBins[bin] = 0.0f;
            state.spectralSignalBins[bin] = 0.0f;
            state.spectralGainDb[bin] *= idleGainReturn;
            if (std::abs(state.spectralEnvelope[bin]) < 1.0e-12f) {
                state.spectralEnvelope[bin] = 0.0f;
            }
            if (std::abs(state.spectralFastEnvelope[bin]) < 1.0e-12f) {
                state.spectralFastEnvelope[bin] = 0.0f;
            }
            if (std::abs(state.spectralSlowEnvelope[bin]) < 1.0e-12f) {
                state.spectralSlowEnvelope[bin] = 0.0f;
            }
            if (std::abs(state.spectralGainDb[bin]) < 0.0001f) {
                state.spectralGainDb[bin] = 0.0f;
            }
        }
        state.spectralUpwardAppliedDb = 0.0f;
        state.spectralTransientRisk = 0.0f;
        state.spectralLowRisk = 0.0f;
        return signalInput;
    }

    auto& detectorBins = state.spectralDetectorBins;
    auto& signalBins = state.spectralSignalBins;
    float previousDetectorLowpass = 0.0f;
    float previousSignalLowpass = 0.0f;

    for (size_t split = 0; split < activeBins - 1; ++split) {
        auto const coefficient = mSpectralCrossoverCoefficients[split];
        state.spectralDetectorLowpass[split] += coefficient * (detectorInput - state.spectralDetectorLowpass[split]);
        state.spectralSignalLowpass[split] += coefficient * (signalInput - state.spectralSignalLowpass[split]);

        detectorBins[split] = state.spectralDetectorLowpass[split] - previousDetectorLowpass;
        signalBins[split] = state.spectralSignalLowpass[split] - previousSignalLowpass;
        previousDetectorLowpass = state.spectralDetectorLowpass[split];
        previousSignalLowpass = state.spectralSignalLowpass[split];
    }

    detectorBins[activeBins - 1] = detectorInput - previousDetectorLowpass;
    signalBins[activeBins - 1] = signalInput - previousSignalLowpass;

    auto const driveAmount = shared.driveAmount;
    auto const driveSafety = shared.driveSafety;
    auto const heavyDriveSafety = shared.heavyDriveSafety;
    auto const extremeDriveSafety = shared.extremeDriveSafety;
    auto const hfGuardAmount = shared.hfGuardAmount;
    auto const smoothBias = shared.smoothBias;
    auto const adaptive = shared.adaptive;
    auto const transientProtect = shared.transientProtect;
    auto const bassLock = shared.bassLock;
    auto const lowCpuQuality = shared.lowCpuQuality;
    auto const upwardDriveGate = shared.upwardDriveGate;
    auto const cleanLoudnessDrive = shared.cleanLoudnessDrive;
    auto const genreSustainScale = shared.genreSustainScale;
    auto const genreUpwardStartOffsetDb = shared.genreUpwardStartOffsetDb;
    auto const genreUpwardMaxBoostDb = shared.genreUpwardMaxBoostDb;
    auto const genreUpwardSlopeBoost = shared.genreUpwardSlopeBoost;
    auto const loudnessEfficiency = shared.loudnessEfficiency;
    auto const spectral2DensityAssist = shared.spectral2DensityAssist;
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    auto const adaptiveDescriptorCadence = mBandMultiplier > 4;
    auto const descriptorInputMagnitude = std::max(std::abs(detectorInput), std::abs(signalInput));
    state.spectralDescriptorInputEnvelope += 0.02f * (descriptorInputMagnitude - state.spectralDescriptorInputEnvelope);
    if (state.spectralDescriptorEventCooldown > 0) {
        --state.spectralDescriptorEventCooldown;
    }
    auto const descriptorRiseEvent = descriptorInputMagnitude > 0.012f
                                     && descriptorInputMagnitude > state.spectralDescriptorInputEnvelope * 1.85f;
    auto const descriptorRiskEvent = state.spectralTransientRisk > 0.62f
                                     && peakBudget.peakPressure > 0.46f
                                     && descriptorInputMagnitude > state.spectralDescriptorInputEnvelope * 1.22f;
    auto const forceDescriptorUpdate = adaptiveDescriptorCadence
                                       && state.spectralDescriptorInterval > mSpectralDescriptorInterval
                                       && state.spectralDescriptorCountdown > 0
                                       && state.spectralDescriptorEventCooldown == 0
                                       && (descriptorRiseEvent || descriptorRiskEvent);
    auto const updateDescriptors = state.spectralDescriptorCountdown == 0 || forceDescriptorUpdate;
    if (!updateDescriptors) {
        --state.spectralDescriptorCountdown;
    }
    if (forceDescriptorUpdate) {
        state.spectralDescriptorEventCooldown = std::max<size_t>(2, mSpectralDescriptorInterval);
    }
#else
    auto const forceDescriptorUpdate = false;
    auto const updateDescriptors = state.spectralDescriptorCountdown == 0;
    state.spectralDescriptorCountdown = updateDescriptors ? mSpectralDescriptorInterval - 1 : state.spectralDescriptorCountdown - 1;
#endif
    auto descriptorTotal = 0.0f;
    auto descriptorHigh = 0.0f;
    auto descriptorWeightedHz = 0.0f;
    auto descriptorWeightedHz2 = 0.0f;
    auto descriptorRise = 0.0f;
    auto descriptorLogSum = 0.0f;

    auto const highBandRatio = state.spectralHighBandRatio;
    auto const binSpread = state.spectralBinSpread;
    auto const spectralFlatness = state.spectralFlatness;
    auto const centroidNorm = state.spectralCentroidNorm;
    auto const transientDensity = state.spectralTransientDensity;
    auto const percussiveSalience =
        juce::jlimit(0.0f, 1.0f, transientDensity * (0.48f + spectralFlatness * 0.62f + binSpread * 0.34f));
    auto const harmonicDensity =
        juce::jlimit(0.0f, 1.0f, (1.0f - transientDensity) * (1.0f - spectralFlatness * 0.72f)
                                      * (0.42f + mode.densityLift * 0.70f));
    auto const signalLevelDb = fastGainToDb(std::abs(signalInput));
    auto const upwardEnergyGate =
        smoothSuppressionShape(juce::jlimit(0.0f, 1.0f, (signalLevelDb + 44.0f) / 24.0f), smoothBias);
    auto const percussiveMode = shared.percussiveMode;
    auto const oneShotArtifactGuard =
        juce::jlimit(0.04f, 1.0f, upwardEnergyGate
                                      * (1.0f - percussiveSalience * (percussiveMode ? 0.74f : 0.48f))
                                      * (0.72f + harmonicDensity * 0.28f));
    auto const brightEventRisk = juce::jlimit(0.0f, 1.0f, highBandRatio * (0.36f + centroidNorm * 0.44f + spectralFlatness * 0.22f)
                                                              * (0.72f + driveAmount * 0.28f));
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    auto const spectral3TransientDodgeBoost =
        juce::jlimit(0.70f, 1.0f, 1.0f - (percussiveSalience * transientProtect * (0.10f + peakBudget.transientReserve * 0.08f)));
    auto const spectral3BrightDodge =
        juce::jlimit(0.76f, 1.0f, 1.0f - (brightEventRisk * hfGuardAmount * 0.12f));
    auto const spectral3CleanDensity =
        juce::jlimit(0.0f, 0.10f, peakBudget.upwardQuality * harmonicDensity * (1.0f - percussiveSalience) * (1.0f - brightEventRisk * 0.35f));
#else
    auto const spectral3TransientDodgeBoost = 1.0f;
    auto const spectral3BrightDodge = 1.0f;
    auto const spectral3CleanDensity = 0.0f;
#endif
    auto const maskingActivity = juce::jlimit(0.18f, 1.0f,
                                              0.24f + (driveAmount * 0.18f) + (highBandRatio * 0.30f)
                                                  + (centroidNorm * 0.22f) + (binSpread * 0.08f)
                                                  - (percussiveSalience * transientProtect * 0.18f));
    auto const fastAttack = shared.fastAttack;
    auto const slowAttack = shared.slowAttack;
    auto const slowRelease = shared.slowRelease;
    auto const driveTargetRelax = shared.driveTargetRelax;
    auto const spectralReductionLimit = shared.spectralReductionLimit;
    auto const upwardSmoothingBase = shared.upwardSmoothingBase;
    auto const reductionSmoothingBase = shared.reductionSmoothingBase;
    auto output = 0.0f;
    auto maxUpwardAppliedDb = 0.0f;
    auto maxTransientRisk = percussiveSalience * transientProtect;
    auto maxLowRisk = 0.0f;
    auto const spectralControlFrame = (state.spectralControlPhase++ & size_t{1}) == 0;

#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
#if defined(PEAKEATER_CPU_BENCHMARK)
    auto const perceptualAllocatorEnabled = mPerceptualAllocatorEnabled;
#else
    constexpr auto perceptualAllocatorEnabled = true;
#endif
    if (updateDescriptors && perceptualAllocatorEnabled && mBandMultiplier >= 4 && peakBudget.densityBudget > 0.001f) {
        std::array<float, spectralBinCount> utilities{};
        auto utilityWeightSum = 0.0f;
        auto weightedUtilitySum = 0.0f;
        auto const pressure = juce::jlimit(0.0f, 1.0f, peakBudget.peakPressure * 0.52f
                                                               + peakBudget.transientReserve * percussiveSalience * 0.30f
                                                               + peakBudget.lowSideRisk * 0.18f);
        auto const allocationAuthority =
            juce::jlimit(0.0f, 1.0f, (peakBudget.densityBudget * 0.46f + peakBudget.sustainLift * 0.34f
                                      + peakBudget.loudnessDistribution * 0.20f)
                                         * (1.0f - pressure * 0.58f));

        // A bounded projected allocation: spend the existing upward-density budget
        // where sustained energy has the lowest peak, transient, low-side and HF cost.
        for (size_t bin = 0; bin < activeBins; ++bin) {
            auto const fastEnvelope = state.spectralFastEnvelope[bin];
            auto const slowEnvelope = state.spectralSlowEnvelope[bin];
            auto const envelopeReference = std::max(0.000001f, fastEnvelope + slowEnvelope);
            auto const transientCost = juce::jlimit(0.0f, 1.0f, std::max(0.0f, fastEnvelope - slowEnvelope)
                                                                     / envelopeReference);
            auto const sustainEvidence = juce::jlimit(0.0f, 1.0f, slowEnvelope / envelopeReference * 2.0f);
            auto const lowWeight = mSpectralLowLockWeights[bin];
            auto const highWeight = mSpectralHighGuardWeights[bin];
            auto const midUtility = mSpectralMidDensityWeights[bin];
            auto const loudnessUtility = 0.46f + midUtility * 0.62f + highWeight * 0.08f
                                         + harmonicDensity * 0.26f + sustainEvidence * 0.32f;
            auto const peakCost = peakBudget.peakPressure * (0.24f + fastEnvelope / envelopeReference * 0.52f);
            auto const lowMonoCost = lowWeight * (peakBudget.lowReserve * 0.42f + peakBudget.lowSideRisk * 0.74f);
            auto const harshnessCost = highWeight * brightEventRisk * (0.44f + hfGuardAmount * 0.42f);
            auto const transientPenalty = transientCost * (0.56f + peakBudget.transientReserve * 0.62f);
            auto const cost = 0.62f + peakCost + lowMonoCost + harshnessCost + transientPenalty;
            auto const utility = juce::jlimit(0.05f, 2.5f, loudnessUtility * (0.28f + sustainEvidence * 0.72f) / cost);
            auto const utilityWeight = 0.04f + slowEnvelope;
            utilities[bin] = utility;
            utilityWeightSum += utilityWeight;
            weightedUtilitySum += utility * utilityWeight;
        }

        auto const meanUtility = weightedUtilitySum / std::max(0.000001f, utilityWeightSum);
        auto const inverseMeanUtility = 1.0f / std::max(0.000001f, meanUtility);
        auto const availableLift = 1.0f + allocationAuthority * (0.070f + mode.loudnessFocus * 0.030f);
        auto const smoothing = lowCpuQuality ? 0.10f : 0.16f;
        auto weightedTargetSum = 0.0f;
        for (size_t bin = 0; bin < activeBins; ++bin) {
            auto const lowConstraint = 1.0f - mSpectralLowLockWeights[bin]
                                                  * juce::jlimit(0.0f, 0.30f, peakBudget.lowSideRisk * 0.30f);
            auto const highConstraint = 1.0f - mSpectralHighGuardWeights[bin]
                                                   * juce::jlimit(0.0f, 0.28f, brightEventRisk * hfGuardAmount * 0.24f);
            auto const target = juce::jlimit(1.0f, 1.30f,
                                              utilities[bin] * inverseMeanUtility * availableLift
                                                  * lowConstraint * highConstraint);
            utilities[bin] = target;
            weightedTargetSum += target * (0.04f + state.spectralSlowEnvelope[bin]);
        }
        auto const targetMean = weightedTargetSum / std::max(0.000001f, utilityWeightSum);
        auto const projectionScale = availableLift / std::max(0.000001f, targetMean);
        for (size_t bin = 0; bin < activeBins; ++bin) {
            auto const lowConstraint = 1.0f - mSpectralLowLockWeights[bin]
                                                  * juce::jlimit(0.0f, 0.30f, peakBudget.lowSideRisk * 0.30f);
            auto const highConstraint = 1.0f - mSpectralHighGuardWeights[bin]
                                                   * juce::jlimit(0.0f, 0.28f, brightEventRisk * hfGuardAmount * 0.24f);
            auto const constrainedMaximum = std::max(1.0f, 1.30f * lowConstraint * highConstraint);
            auto const target = juce::jlimit(1.0f, constrainedMaximum, utilities[bin] * projectionScale);
            state.spectralLoudnessAllocation[bin] += smoothing * (target - state.spectralLoudnessAllocation[bin]);
        }
#if defined(PEAKEATER_CPU_BENCHMARK)
        ++mPerceptualAllocatorUpdateCount;
#endif
    } else if (updateDescriptors && (!perceptualAllocatorEnabled || mBandMultiplier < 4)) {
        for (size_t bin = 0; bin < activeBins; ++bin) {
            state.spectralLoudnessAllocation[bin] = 1.0f;
        }
    }
#endif

    for (size_t bin = 0; bin < activeBins; ++bin) {
        auto const magnitude = std::abs(detectorBins[bin]);
        auto const magnitudeRise = std::max(0.0f, magnitude - state.spectralPreviousMagnitude[bin]);
        if (updateDescriptors) {
            descriptorTotal += magnitude;
            descriptorWeightedHz += magnitude * mSpectralCenterFrequencies[bin];
            descriptorWeightedHz2 += magnitude * mSpectralCenterFrequencySquares[bin];
            descriptorRise += magnitudeRise;
            if (!lowCpuQuality) {
                descriptorLogSum += std::log(magnitude + 0.000001f);
            }
            if (mSpectralCenterFrequencies[bin] >= 4200.0f) {
                descriptorHigh += magnitude;
            }
        }
        auto const transientRatio = magnitude > 0.000001f ? juce::jlimit(0.0f, 1.0f, magnitudeRise / magnitude) : 0.0f;
        auto const binSignalMagnitude = std::abs(signalBins[bin]);
        if (std::max(magnitude, binSignalMagnitude) <= 1.0e-8f && std::abs(state.spectralGainDb[bin]) <= 0.0001f
            && std::abs(mSpectralToneGains[bin]) <= 0.0001f) {
            state.spectralFastEnvelope[bin] *= 0.9995f;
            state.spectralSlowEnvelope[bin] *= 0.9995f;
            state.spectralEnvelope[bin] *= 0.9995f;
            state.spectralPreviousMagnitude[bin] = 0.0f;
            output += signalBins[bin];
            continue;
        }
        auto const fastRelease = juce::jmap(adaptive, mSpectralFastReleaseCoefficient,
                                            juce::jmap(transientRatio, mSpectralFastReleaseCoefficient, mSpectralReleaseCoefficient));
        auto fastCoefficient = magnitude > state.spectralFastEnvelope[bin] ? fastAttack : fastRelease;
        auto slowCoefficient = magnitude > state.spectralSlowEnvelope[bin] ? slowAttack : slowRelease;
        if (magnitude > std::max(state.spectralFastEnvelope[bin], state.spectralSlowEnvelope[bin])) {
            state.spectralHoldSamples[bin] = mSpectralHoldSamples;
        } else if (state.spectralHoldSamples[bin] > 0) {
            --state.spectralHoldSamples[bin];
            fastCoefficient = 1.0f;
            slowCoefficient = 1.0f;
        }
        state.spectralFastEnvelope[bin] =
            fastCoefficient * state.spectralFastEnvelope[bin] + (1.0f - fastCoefficient) * magnitude;
        state.spectralSlowEnvelope[bin] =
            slowCoefficient * state.spectralSlowEnvelope[bin] + (1.0f - slowCoefficient) * magnitude;
        state.spectralEnvelope[bin] = std::max(state.spectralFastEnvelope[bin], state.spectralSlowEnvelope[bin] * 0.86f);

        auto const spectralControlEvent = updateDescriptors || transientRatio > 0.075f
                                          || magnitudeRise > std::max(0.00001f, magnitude * 0.055f);
        if (!spectralControlFrame && !spectralControlEvent) {
            auto const shapedGainDb = state.spectralGainDb[bin];
            gainReductionDb = std::max(gainReductionDb, std::max(0.0f, -shapedGainDb));
            output += signalBins[bin] * fastDbToGain((shapedGainDb * depth) + mSpectralToneGains[bin]);
            state.spectralPreviousMagnitude[bin] = magnitude;
            continue;
        }

        auto const highGuardBase = hfGuardAmount * mSpectralHighGuardWeights[bin];
        auto const staticHighGuardWeight = highGuardBase * (0.012f + (driveAmount * 0.018f))
                                           * (0.55f + (maskingActivity * 0.45f));
        auto const lowLockWeight = bassLock * mSpectralLowLockWeights[bin];
        auto const targetDb = ceilingDb + mSpectralTargetOffsets[bin] + (thresholdDriveDb * driveTargetRelax) - (staticHighGuardWeight * 0.25f)
                              + (lowLockWeight * 1.65f);
        auto const fastEnvelopeDb = fastGainToDb(state.spectralFastEnvelope[bin]);
        auto const slowEnvelopeDb = fastGainToDb(state.spectralSlowEnvelope[bin]);
        auto const fastOverDb = std::max(0.0f, fastEnvelopeDb - targetDb);
        auto const slowOverDb = std::max(0.0f, slowEnvelopeDb - targetDb);
        auto const highGuardEvent = highGuardBase
                                    * hermiteShape(juce::jlimit(0.0f, 1.0f, ((fastOverDb * 0.72f) + slowOverDb) / 10.0f))
                                    * (0.66f + (driveAmount * 0.34f))
                                    * juce::jlimit(0.25f, 1.16f, maskingActivity + (highBandRatio * 0.18f)
                                                                   + (brightEventRisk * 0.16f))
                                    * (1.0f - (transientRatio * transientProtect * 0.42f));
        auto const transientSalience = hermiteShape(juce::jlimit(0.0f, 1.0f, fastOverDb / 10.0f)) * transientRatio;
        auto const densitySalience = smoothSuppressionShape(juce::jlimit(0.0f, 1.0f, slowOverDb / 14.0f), smoothBias)
                                     * (0.86f + harmonicDensity * 0.22f);
        auto const densityPresence = juce::jlimit(0.0f, 1.0f, (mSpectralHighGuardWeights[bin] * 0.35f) + (mode.loudnessFocus * 0.42f)
                                                                 - (lowLockWeight * 0.26f));
        auto const midDensityFocus = juce::jlimit(0.0f, 1.0f, mSpectralMidDensityWeights[bin] * mode.spectralMidFocusScale);
        auto const hpssTransientProtect = juce::jlimit(0.0f, 1.0f, transientProtect + (percussiveSalience * 0.42f));
        auto const attackProtectionDb = hpssTransientProtect * transientRatio * juce::jlimit(0.35f, 1.25f, mode.transientScale) * 2.45f;
        auto const fastTargetDb = targetDb + attackProtectionDb + (lowLockWeight * 1.4f);
        auto const slowTargetDb = targetDb + (lowLockWeight * 0.85f);
        auto fastGainDb = compressionGainDb(fastEnvelopeDb, fastTargetDb, mSpectralFastInverseRatios[bin],
                                            mSpectralFastKneeDb, mSpectralFastInverseTwoKneeDb);
        auto slowGainDb = compressionGainDb(slowEnvelopeDb, slowTargetDb, mSpectralSlowInverseRatios[bin],
                                            mSpectralSlowKneeDb, mSpectralSlowInverseTwoKneeDb);
        fastGainDb *= juce::jlimit(0.18f, 0.96f, 0.54f + (mode.spectralControlScale * 0.10f) + (highGuardEvent * 0.20f)
                                                        + (driveSafety * 0.04f) + (heavyDriveSafety * 0.025f)
                                                        + (extremeDriveSafety * 0.018f)
                                                        - (hpssTransientProtect * transientRatio * 0.48f));
        slowGainDb *= juce::jlimit(0.35f, 1.02f, 0.68f + (mode.densityLift * 0.15f) + (smoothBias * 0.10f)
                                                        + (driveSafety * 0.05f) + (heavyDriveSafety * 0.035f)
                                                        + (extremeDriveSafety * 0.025f) + (harmonicDensity * 0.07f));
        auto gainDb = (fastGainDb * transientSalience) + (slowGainDb * juce::jmax(densitySalience, 0.22f));

        auto const underDb = targetDb - slowEnvelopeDb;
        auto const fastSlowGapDb = std::max(0.0f, fastEnvelopeDb - slowEnvelopeDb);
        auto const sustainStability =
            juce::jlimit(0.0f, 1.0f, 1.0f - (transientRatio * (0.84f + peakBudget.transientReserve * 0.10f))
                                      - (fastSlowGapDb / 8.0f) - (percussiveSalience * (0.22f + peakBudget.transientReserve * 0.10f)));
        auto const lowUpwardDodge = juce::jlimit(0.12f, 1.0f, 1.0f - (lowLockWeight * (0.48f + peakBudget.lowReserve * 0.28f)));
        maxLowRisk = std::max(maxLowRisk, juce::jlimit(0.0f, 1.0f, lowLockWeight * bassLock));
        auto const guardDodge = juce::jlimit(0.08f, 1.0f, 1.0f - (highGuardEvent * 0.94f) - (brightEventRisk * highGuardBase * 0.20f));
        auto const transientFeedbackDodge =
            juce::jlimit(0.10f, 1.0f, 1.0f - (peakBudget.transientReserve * maxTransientRisk * (percussiveMode ? 0.28f : 0.14f)));
        auto const lowFeedbackDodge =
            juce::jlimit(0.22f, 1.0f, 1.0f - (peakBudget.lowReserve * maxLowRisk * 0.18f));
        auto const transientDodge =
            juce::jlimit(0.06f, 1.0f, 1.0f - (transientRatio * hpssTransientProtect * (0.78f + peakBudget.transientReserve * 0.18f))
                                      - (percussiveSalience * (0.18f + peakBudget.transientReserve * 0.10f)));
        maxTransientRisk = std::max(maxTransientRisk, juce::jlimit(0.0f, 1.0f, transientRatio * hpssTransientProtect));
        auto const cleanLoudnessWindow =
            juce::jlimit(0.0f, 1.0f, cleanLoudnessDrive * harmonicDensity * midDensityFocus
                                          * guardDodge * transientDodge * lowUpwardDodge * oneShotArtifactGuard
                                          * transientFeedbackDodge * lowFeedbackDodge
                                          * spectral3TransientDodgeBoost * spectral3BrightDodge
                                          * (1.0f - peakBudget.peakPressure * 0.34f)
                                          * (0.55f + mode.loudnessFocus * 0.45f + peakBudget.cleanLift * 0.18f
                                             + peakBudget.loudnessDistribution * 0.10f)
                                          + spectral2DensityAssist * harmonicDensity * midDensityFocus
                                                * guardDodge * transientDodge * lowUpwardDodge * oneShotArtifactGuard
                                                * spectral3TransientDodgeBoost * spectral3BrightDodge * 0.16f
                                          + spectral3CleanDensity * midDensityFocus * guardDodge * 0.12f);
        auto const sustainOnlyGate =
            juce::jlimit(0.0f, 1.0f, (sustainStability * transientDodge * guardDodge * lowUpwardDodge
                                           * upwardDriveGate * oneShotArtifactGuard
                                           * spectral3TransientDodgeBoost * spectral3BrightDodge
                                           * (0.42f + harmonicDensity * 0.58f + peakBudget.densityBudget * 0.42f
                                              + peakBudget.sustainLift * 0.16f)
                                           + (cleanLoudnessWindow * 0.18f)
                                           + (spectral2DensityAssist * harmonicDensity * midDensityFocus
                                              * oneShotArtifactGuard * spectral3TransientDodgeBoost * 0.035f))
                                           * genreSustainScale);
        auto const upwardStartDb = 6.7f - (peakBudget.densityBudget * 1.08f) - (peakBudget.upwardQuality * 0.28f)
                                    - (cleanLoudnessWindow * (0.42f + mode.loudnessFocus * 0.34f))
                                    - (peakBudget.cleanLift * harmonicDensity * midDensityFocus * guardDodge * transientDodge * 0.24f)
                                    - (peakBudget.sustainLift * harmonicDensity * midDensityFocus * guardDodge
                                       * transientDodge * lowUpwardDodge * 0.20f)
                                    - (spectral2DensityAssist * harmonicDensity * midDensityFocus * guardDodge * 0.20f)
                                    - (spectral3CleanDensity * midDensityFocus * 0.16f)
                                    - genreUpwardStartOffsetDb
                                   + (percussiveSalience * peakBudget.transientReserve * 0.55f)
                                   + ((1.0f - oneShotArtifactGuard) * (percussiveMode ? 2.4f : 1.2f))
                                   + ((1.0f - upwardDriveGate) * 1.45f)
                                   + (lowLockWeight * peakBudget.lowReserve * 0.35f);
        if (underDb > upwardStartDb && sustainOnlyGate > 0.002f) {
            auto const upwardGate =
                smoothSuppressionShape(juce::jlimit(0.0f, 1.0f, (underDb - upwardStartDb) / 18.0f), smoothBias);
            auto const airRecovery =
                juce::jlimit(0.0f, 0.28f, mSpectralHighGuardWeights[bin] * peakBudget.upwardQuality
                                              * (1.0f - highGuardEvent) * (1.0f - transientRatio)
                                              * (1.0f - brightEventRisk * highGuardBase * 0.32f));
            auto const maxUpwardDb =
                juce::jlimit(1.1f, 4.85f, 1.8f + (peakBudget.densityBudget * 1.72f) + (mode.loudnessFocus * 0.70f)
                                             + (peakBudget.upwardQuality * 0.28f) + (cleanLoudnessWindow * 0.72f)
                                             + (peakBudget.cleanLift * harmonicDensity * midDensityFocus * 0.42f)
                                             + (peakBudget.sustainLift * harmonicDensity * midDensityFocus * 0.28f)
                                             + genreUpwardMaxBoostDb
                                             + (spectral2DensityAssist * midDensityFocus * 0.22f))
                * juce::jlimit(0.45f, 1.0f, 0.45f + (upwardDriveGate * 0.55f))
                * juce::jlimit(0.38f, 1.0f, oneShotArtifactGuard + harmonicDensity * 0.22f)
                * juce::jlimit(0.58f, 1.0f, 1.0f - peakBudget.peakPressure * 0.16f
                                             - peakBudget.transientReserve * transientRatio * 0.08f
                                             - highGuardEvent * 0.10f)
                * transientFeedbackDodge * lowFeedbackDodge;
            auto const upwardDb = juce::jlimit(0.0f, maxUpwardDb,
                                               (underDb - upwardStartDb) * (0.05f + (loudnessEfficiency * 0.07f)
                                                                            + (peakBudget.densityBudget * 0.070f)
                                                                            + (cleanLoudnessWindow * 0.026f)
                                                                            + (peakBudget.cleanLift * 0.018f)
                                                                            + (peakBudget.sustainLift * 0.012f)
                                                                            + genreUpwardSlopeBoost)
                                                    * (0.32f + densityPresence * 0.30f + harmonicDensity * 0.38f + airRecovery
                                                       + cleanLoudnessWindow * 0.12f)
                                                    * upwardGate * sustainOnlyGate)
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
                                      * state.spectralLoudnessAllocation[bin]
#endif
            ;
            gainDb += upwardDb;
            maxUpwardAppliedDb = std::max(maxUpwardAppliedDb, upwardDb);
        }
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
        auto const allocatedSustainLiftDb =
            std::max(0.0f, state.spectralLoudnessAllocation[bin] - 1.0f)
            * (0.18f + peakBudget.densityBudget * 0.22f + peakBudget.sustainLift * 0.12f)
            * sustainOnlyGate * upwardDriveGate * transientDodge * guardDodge * lowUpwardDodge;
        gainDb += allocatedSustainLiftDb;
        maxUpwardAppliedDb = std::max(maxUpwardAppliedDb, allocatedSustainLiftDb);
#endif

        if (highGuardEvent > 0.0001f && slowEnvelopeDb > targetDb) {
            gainDb -= juce::jlimit(0.0f, 3.2f, (slowEnvelopeDb - targetDb) * highGuardEvent * (0.14f + driveAmount * 0.16f));
        }

        auto const lowRelax = 1.0f - (lowLockWeight * 0.52f);
        auto const transientRelax = 1.0f - (hpssTransientProtect * transientRatio * (0.46f + peakBudget.transientReserve * 0.12f));
        auto const maxReduction = -spectralReductionLimit * lowRelax * transientRelax;
        gainDb = juce::jlimit(maxReduction, 6.0f, gainDb);
        auto const gainRise = gainDb > state.spectralGainDb[bin];
        auto const upwardSmoothing = juce::jlimit(0.62f, 0.92f, upwardSmoothingBase + densityPresence * 0.04f);
        auto const reductionSmoothing =
            juce::jlimit(0.14f, 0.46f, reductionSmoothingBase + (transientProtect * transientRatio * 0.08f));
        auto const gainCoefficient = gainRise ? upwardSmoothing : reductionSmoothing;
        state.spectralGainDb[bin] = (gainCoefficient * state.spectralGainDb[bin]) + ((1.0f - gainCoefficient) * gainDb);
        auto const shapedGainDb = state.spectralGainDb[bin];
        gainReductionDb = std::max(gainReductionDb, std::max(0.0f, -shapedGainDb));
        output += signalBins[bin] * fastDbToGain((shapedGainDb * depth) + mSpectralToneGains[bin]);
        state.spectralPreviousMagnitude[bin] = magnitude;
    }
    if (updateDescriptors) {
#if defined(PEAKEATER_CPU_BENCHMARK)
        ++mSpectralDescriptorUpdateCount;
        if (forceDescriptorUpdate) {
            ++mSpectralDescriptorEventUpdateCount;
        }
#endif
        auto const safeTotal = std::max(0.000001f, descriptorTotal);
        auto const inverseTotal = 1.0f / safeTotal;
        auto const inverseActiveBins = 1.0f / static_cast<float>(activeBins);
        auto const centroidHz = descriptorWeightedHz * inverseTotal;
        auto const varianceHz = std::max(0.0f, (descriptorWeightedHz2 * inverseTotal) - (centroidHz * centroidHz));
        auto const nextHighBandRatio = juce::jlimit(0.0f, 1.0f, descriptorHigh * inverseTotal);
        auto const nextBinSpread = juce::jlimit(0.0f, 1.0f, std::sqrt(varianceHz) / std::max(1200.0f, centroidHz * 1.4f));
        auto const arithmeticMean = descriptorTotal * inverseActiveBins;
        auto const nextFlatness =
            lowCpuQuality ? juce::jlimit(0.18f, 0.72f, 0.30f + (nextBinSpread * 0.24f) + (nextHighBandRatio * 0.12f))
                          : juce::jlimit(0.0f, 1.0f, std::exp(descriptorLogSum * inverseActiveBins)
                                                          / std::max(0.000001f, arithmeticMean));
        auto const nextCentroidNorm = juce::jlimit(0.0f, 1.0f, std::log2(std::max(1000.0f, centroidHz) / 1000.0f)
                                                                    / std::log2(18000.0f / 1000.0f));
        auto const nextTransientDensity = juce::jlimit(0.0f, 1.0f, descriptorRise * inverseTotal);
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
        if (adaptiveDescriptorCadence) {
            auto const descriptorChange = std::abs(nextHighBandRatio - state.spectralHighBandRatio)
                                          + std::abs(nextBinSpread - state.spectralBinSpread)
                                          + std::abs(nextFlatness - state.spectralFlatness)
                                          + std::abs(nextCentroidNorm - state.spectralCentroidNorm);
            auto const rapidlyChanging = descriptorChange > 0.60f || nextTransientDensity > 0.82f || forceDescriptorUpdate;
            if (rapidlyChanging) {
                state.spectralDescriptorStableUpdates = 0;
                state.spectralDescriptorInterval = mSpectralDescriptorInterval;
            } else if (descriptorChange < 0.38f && nextTransientDensity < 0.58f) {
                state.spectralDescriptorStableUpdates = std::min<size_t>(8, state.spectralDescriptorStableUpdates + 1);
                if (state.spectralDescriptorStableUpdates >= 2) {
                    auto const maximumInterval = mSpectralDescriptorInterval * (mPercussiveMode ? size_t{3} : size_t{5});
                    auto const intervalStep = std::max<size_t>(1, mSpectralDescriptorInterval / 2);
                    state.spectralDescriptorInterval = std::min(maximumInterval, state.spectralDescriptorInterval + intervalStep);
                }
            } else {
                state.spectralDescriptorStableUpdates = 0;
                state.spectralDescriptorInterval = std::max(mSpectralDescriptorInterval,
                                                             state.spectralDescriptorInterval - std::min(state.spectralDescriptorInterval,
                                                                                                         mSpectralDescriptorInterval));
            }
            state.spectralDescriptorCountdown = std::max<size_t>(1, state.spectralDescriptorInterval) - 1;
        } else {
            state.spectralDescriptorCountdown = mSpectralDescriptorInterval - 1;
        }
#endif
        state.spectralHighBandRatio = nextHighBandRatio;
        state.spectralBinSpread = nextBinSpread;
        state.spectralFlatness = nextFlatness;
        state.spectralCentroidNorm = nextCentroidNorm;
        state.spectralTransientDensity = nextTransientDensity;
    }
    state.spectralUpwardAppliedDb = std::isfinite(maxUpwardAppliedDb) ? juce::jlimit(0.0f, 6.0f, maxUpwardAppliedDb) : 0.0f;
    state.spectralTransientRisk = std::isfinite(maxTransientRisk) ? juce::jlimit(0.0f, 1.0f, maxTransientRisk) : 0.0f;
    state.spectralLowRisk = std::isfinite(maxLowRisk) ? juce::jlimit(0.0f, 1.0f, maxLowRisk) : 0.0f;

    return output;
}

float SpectralMaximizer::toneWeight(size_t bandIndex) const {
    if (bandIndex == 0) {
        return mTone;
    }
    if (bandIndex == 2) {
        return -mTone;
    }
    return 0.0f;
}

float SpectralMaximizer::getClipAmountDb() const {
    return mClipAmountDb.load();
}

float SpectralMaximizer::getGainReductionDb() const {
    return mGainReductionDb.load();
}

}  // namespace pe::processor

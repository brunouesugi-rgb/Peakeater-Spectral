#include "DynamicsMeter.h"

#include <algorithm>
#include <cmath>

namespace pe::processor {

namespace {
float constexpr gMinusInfinityDb = -120.0f;
double constexpr gAbsoluteGateLufs = -70.0;
double constexpr gRelativeGateOffsetDb = -10.0;
double constexpr gMomentaryWindowSeconds = 0.4;
double constexpr gShortTermWindowSeconds = 3.0;
double constexpr gMaxHistorySeconds = 30.0;
double constexpr gIntegratedBlockSeconds = 0.4;
double constexpr gMaxIntegratedHistorySeconds = 180.0;

struct BiquadCoefficients {
    double b0;
    double b1;
    double b2;
    double a1;
    double a2;
};

BiquadCoefficients makeHighPass(double sampleRate) {
    auto const k = std::tan(juce::MathConstants<double>::pi * 38.0 / sampleRate);
    auto const q = 0.5;
    auto const norm = 1.0 / (1.0 + (k / q) + (k * k));
    return {norm, -2.0 * norm, norm, 2.0 * (k * k - 1.0) * norm, (1.0 - (k / q) + (k * k)) * norm};
}

BiquadCoefficients makeHighShelf(double sampleRate) {
    auto const gain = std::pow(10.0, 4.0 / 40.0);
    auto const w0 = 2.0 * juce::MathConstants<double>::pi * 1681.974450955533 / sampleRate;
    auto const alpha = std::sin(w0) / (2.0 * 0.7071752369554196);
    auto const cosW0 = std::cos(w0);
    auto const sqrtA = std::sqrt(gain);

    auto const b0 = gain * ((gain + 1.0) + ((gain - 1.0) * cosW0) + (2.0 * sqrtA * alpha));
    auto const b1 = -2.0 * gain * ((gain - 1.0) + ((gain + 1.0) * cosW0));
    auto const b2 = gain * ((gain + 1.0) + ((gain - 1.0) * cosW0) - (2.0 * sqrtA * alpha));
    auto const a0 = (gain + 1.0) - ((gain - 1.0) * cosW0) + (2.0 * sqrtA * alpha);
    auto const a1 = 2.0 * ((gain - 1.0) - ((gain + 1.0) * cosW0));
    auto const a2 = (gain + 1.0) - ((gain - 1.0) * cosW0) - (2.0 * sqrtA * alpha);

    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

}

void DynamicsMeter::update(float clipAmountDb, float gainReductionDb, juce::AudioBuffer<float> const& outputBuffer, double sampleRate,
                           bool updateTruePeakAndLoudness, bool updateLoudnessOnly) {
    juce::ScopedNoDenormals noDenormals;
    mClipAmountDb = std::max(0.0f, clipAmountDb);
    mGainReductionDb = std::max(0.0f, gainReductionDb);
    mClipAmountMaxDb = std::max(mClipAmountMaxDb.load(), mClipAmountDb.load());
    mGainReductionMaxDb = std::max(mGainReductionMaxDb.load(), mGainReductionDb.load());

    if (outputBuffer.getNumSamples() <= 0 || outputBuffer.getNumChannels() <= 0 || sampleRate <= 0.0) {
        mOutputPeakDb = gMinusInfinityDb;
        mTruePeakDb = gMinusInfinityDb;
        mMomentaryLufs = gMinusInfinityDb;
        mShortTermLufs = gMinusInfinityDb;
        mIntegratedLufs = gMinusInfinityDb;
        mCrestDb = 0.0f;
        return;
    }

    auto const outputPeakDb = bufferPeakDb(outputBuffer);
    mOutputPeakDb = outputPeakDb;
    mOutputPeakMaxDb = std::max(mOutputPeakMaxDb.load(), outputPeakDb);
    if (updateTruePeakAndLoudness) {
        auto const truePeakDb = outputPeakDb <= -18.0f ? outputPeakDb : estimateTruePeakDb4x(outputBuffer);
        mTruePeakDb = truePeakDb;
        mTruePeakMaxDb = std::max(mTruePeakMaxDb.load(), truePeakDb);
    }

    if (updateLoudnessOnly) {
        auto const meanSquare = weightedMeanSquare(outputBuffer, sampleRate);
        auto const blockSamples = static_cast<uint64_t>(std::max(0, outputBuffer.getNumSamples()));
        pushWindowEnergy(meanSquare, blockSamples, sampleRate);

        auto const momentaryEnergy = windowEnergy(static_cast<uint64_t>(gMomentaryWindowSeconds * sampleRate));
        auto const shortTermEnergy = windowEnergy(static_cast<uint64_t>(gShortTermWindowSeconds * sampleRate));
        mMomentaryLufs = lufsFromMeanSquare(momentaryEnergy);
        mShortTermLufs = lufsFromMeanSquare(shortTermEnergy);
        mIntegratedLufs = lufsFromMeanSquare(gatedIntegratedEnergy());

        auto const rmsDb =
            juce::Decibels::gainToDecibels(static_cast<float>(std::sqrt(std::max(shortTermEnergy, 0.0))), gMinusInfinityDb);
        auto const crestDb = std::max(0.0f, outputPeakDb - rmsDb);
        mCrestDb = crestDb;
        mCrestMaxDb = std::max(mCrestMaxDb.load(), crestDb);
    }
}

void DynamicsMeter::reset() {
    mClipAmountDb = 0.0f;
    mGainReductionDb = 0.0f;
    mTruePeakDb = gMinusInfinityDb;
    mOutputPeakDb = gMinusInfinityDb;
    mMomentaryLufs = gMinusInfinityDb;
    mShortTermLufs = gMinusInfinityDb;
    mIntegratedLufs = gMinusInfinityDb;
    mCrestDb = 0.0f;
    mEnergyHistory.clear();
    mIntegratedBlocks.clear();
    mHistorySamples = 0;
    mIntegratedSamples = 0;
    mIntegratedAccumulatorEnergy = 0.0;
    mIntegratedAccumulatorSamples = 0;
    mCurrentSampleRate = 0.0;
    mWeightingCoefficients = {};
    for (auto& state : mWeightingStates) {
        state = {};
    }
    resetMaxima();
}

void DynamicsMeter::resetMaxima() {
    mTruePeakMaxDb = gMinusInfinityDb;
    mOutputPeakMaxDb = gMinusInfinityDb;
    mClipAmountMaxDb = 0.0f;
    mGainReductionMaxDb = 0.0f;
    mCrestMaxDb = 0.0f;
}

float DynamicsMeter::getClipAmountDb() const {
    return mClipAmountDb.load();
}

float DynamicsMeter::getGainReductionDb() const {
    return mGainReductionDb.load();
}

float DynamicsMeter::getTruePeakDb() const {
    return mTruePeakDb.load();
}

float DynamicsMeter::getTruePeakMaxDb() const {
    return mTruePeakMaxDb.load();
}

float DynamicsMeter::getOutputPeakDb() const {
    return mOutputPeakDb.load();
}

float DynamicsMeter::getOutputPeakMaxDb() const {
    return mOutputPeakMaxDb.load();
}

float DynamicsMeter::getClipAmountMaxDb() const {
    return mClipAmountMaxDb.load();
}

float DynamicsMeter::getGainReductionMaxDb() const {
    return mGainReductionMaxDb.load();
}

float DynamicsMeter::getMomentaryLufs() const {
    return mMomentaryLufs.load();
}

float DynamicsMeter::getShortTermLufs() const {
    return mShortTermLufs.load();
}

float DynamicsMeter::getIntegratedLufs() const {
    return mIntegratedLufs.load();
}

float DynamicsMeter::getCrestDb() const {
    return mCrestDb.load();
}

float DynamicsMeter::getCrestMaxDb() const {
    return mCrestMaxDb.load();
}

float DynamicsMeter::estimateTruePeakDb4x(juce::AudioBuffer<float> const& buffer) {
    if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0) {
        return gMinusInfinityDb;
    }

    auto peak = 0.0f;
    auto const cubicInterpolate = [](float p0, float p1, float p2, float p3, float t) {
        auto const t2 = t * t;
        auto const t3 = t2 * t;
        return 0.5f * ((2.0f * p1) + ((-p0 + p2) * t) + (((2.0f * p0) - (5.0f * p1) + (4.0f * p2) - p3) * t2)
                       + ((-p0 + (3.0f * p1) - (3.0f * p2) + p3) * t3));
    };
    constexpr std::array<std::array<float, 4>, 5> phaseTaps{{
        {{-0.0390625f, 0.3671875f, 0.7734375f, -0.1015625f}},
        {{-0.0625f, 0.5625f, 0.5625f, -0.0625f}},
        {{-0.09375f, 0.59375f, 0.50000f, 0.00000f}},
        {{-0.0625f, 0.265625f, 0.8984375f, -0.1015625f}},
        {{-0.01171875f, 0.11328125f, 0.94140625f, -0.04296875f}},
    }};
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
        auto const* samples = buffer.getReadPointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            auto const current = std::isfinite(samples[sample]) ? samples[sample] : 0.0f;
            auto const previous2 = sample > 1 ? (std::isfinite(samples[sample - 2]) ? samples[sample - 2] : 0.0f) : current;
            auto const previous = sample > 0 ? (std::isfinite(samples[sample - 1]) ? samples[sample - 1] : 0.0f) : current;
            auto const next = sample + 1 < buffer.getNumSamples() ? (std::isfinite(samples[sample + 1]) ? samples[sample + 1] : 0.0f) : current;

            peak = std::max(peak, std::abs(current));
            auto const slopeGuard = std::abs(current - previous) * 0.018f;
            auto const curvatureGuard = std::abs((current - previous) - (previous - previous2)) * 0.012f;
            for (auto const t : {0.25f, 0.5f, 0.75f}) {
                auto const interpolated = cubicInterpolate(previous2, previous, current, next, t);
                peak = std::max(peak, std::abs(interpolated) + slopeGuard + curvatureGuard);
            }
            for (auto const& taps : phaseTaps) {
                auto const interpolated = (previous2 * taps[0]) + (previous * taps[1]) + (current * taps[2]) + (next * taps[3]);
                peak = std::max(peak, std::abs(interpolated) + (slopeGuard * 0.65f));
            }
        }
    }

    return juce::Decibels::gainToDecibels(std::max(peak, 0.000001f), gMinusInfinityDb);
}

float DynamicsMeter::bufferPeakDb(juce::AudioBuffer<float> const& buffer) {
    auto peak = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
        auto const* samples = buffer.getReadPointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            auto const value = std::isfinite(samples[sample]) ? samples[sample] : 0.0f;
            peak = std::max(peak, std::abs(value));
        }
    }

    return juce::Decibels::gainToDecibels(std::max(peak, 0.000001f), gMinusInfinityDb);
}

double DynamicsMeter::weightedMeanSquare(juce::AudioBuffer<float> const& buffer, double sampleRate) {
    auto const channels = std::min<int>(buffer.getNumChannels(), static_cast<int>(mWeightingStates.size()));
    if (channels <= 0 || buffer.getNumSamples() <= 0 || sampleRate <= 0.0) {
        return 0.0;
    }

    if (std::abs(mWeightingCoefficients.sampleRate - sampleRate) > 1.0) {
        auto const highPass = makeHighPass(sampleRate);
        auto const highShelf = makeHighShelf(sampleRate);
        mWeightingCoefficients = {sampleRate, highPass.b0, highPass.b1, highPass.b2, highPass.a1, highPass.a2,
                                  highShelf.b0, highShelf.b1, highShelf.b2, highShelf.a1, highShelf.a2};
        for (auto& state : mWeightingStates) {
            state = {};
        }
    }
    auto processStage1 = [](double input, BiquadCoefficients const& c, WeightingState& state) {
        auto const output = (c.b0 * input) + (c.b1 * state.stage1X1) + (c.b2 * state.stage1X2)
                            - (c.a1 * state.stage1Y1) - (c.a2 * state.stage1Y2);
        state.stage1X2 = state.stage1X1;
        state.stage1X1 = input;
        state.stage1Y2 = state.stage1Y1;
        state.stage1Y1 = output;
        return output;
    };
    auto processStage2 = [](double input, BiquadCoefficients const& c, WeightingState& state) {
        auto const output = (c.b0 * input) + (c.b1 * state.stage2X1) + (c.b2 * state.stage2X2)
                            - (c.a1 * state.stage2Y1) - (c.a2 * state.stage2Y2);
        state.stage2X2 = state.stage2X1;
        state.stage2X1 = input;
        state.stage2Y2 = state.stage2Y1;
        state.stage2Y1 = output;
        return output;
    };
    auto sumSquares = 0.0;
    auto samplesPerChannel = buffer.getNumSamples();
    BiquadCoefficients const highPass{mWeightingCoefficients.hpB0, mWeightingCoefficients.hpB1, mWeightingCoefficients.hpB2,
                                      mWeightingCoefficients.hpA1, mWeightingCoefficients.hpA2};
    BiquadCoefficients const highShelf{mWeightingCoefficients.shelfB0, mWeightingCoefficients.shelfB1, mWeightingCoefficients.shelfB2,
                                       mWeightingCoefficients.shelfA1, mWeightingCoefficients.shelfA2};

    for (int channel = 0; channel < channels; ++channel) {
        auto& state = mWeightingStates[static_cast<size_t>(channel)];
        auto const* samples = buffer.getReadPointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            auto const input = std::isfinite(samples[sample]) ? samples[sample] : 0.0f;
            auto const highPassed = processStage1(input, highPass, state);
            auto const weighted = processStage2(highPassed, highShelf, state);
            sumSquares += static_cast<double>(weighted) * static_cast<double>(weighted);
        }
    }

    // ITU-R BS.1770 sums weighted channel powers. Dividing only by samples per channel
    // avoids under-reading stereo dual-mono material by roughly 3 dB.
    return samplesPerChannel > 0 ? sumSquares / static_cast<double>(samplesPerChannel) : 0.0;
}

void DynamicsMeter::pushWindowEnergy(double energy, uint64_t samples, double sampleRate) {
    if (samples == 0 || sampleRate <= 0.0) {
        return;
    }
    if (std::abs(mCurrentSampleRate - sampleRate) > 1.0) {
        mEnergyHistory.clear();
        mIntegratedBlocks.clear();
        mHistorySamples = 0;
        mIntegratedSamples = 0;
        mIntegratedAccumulatorEnergy = 0.0;
        mIntegratedAccumulatorSamples = 0;
        mCurrentSampleRate = sampleRate;
    }

    mEnergyHistory.push_back({std::max(0.0, energy), samples});
    mHistorySamples += samples;
    auto const maxSamples = static_cast<uint64_t>(gMaxHistorySeconds * sampleRate);
    while (!mEnergyHistory.empty() && mHistorySamples > maxSamples) {
        mHistorySamples -= mEnergyHistory.front().samples;
        mEnergyHistory.pop_front();
    }

    mIntegratedAccumulatorEnergy += std::max(0.0, energy) * static_cast<double>(samples);
    mIntegratedAccumulatorSamples += samples;
    auto const integratedBlockSamples = static_cast<uint64_t>(std::max(1.0, gIntegratedBlockSeconds * sampleRate));
    if (mIntegratedAccumulatorSamples >= integratedBlockSamples) {
        auto const blockEnergy = mIntegratedAccumulatorEnergy / static_cast<double>(mIntegratedAccumulatorSamples);
        mIntegratedBlocks.push_back({blockEnergy, mIntegratedAccumulatorSamples});
        mIntegratedSamples += mIntegratedAccumulatorSamples;
        mIntegratedAccumulatorEnergy = 0.0;
        mIntegratedAccumulatorSamples = 0;
    }

    auto const maxIntegratedSamples = static_cast<uint64_t>(gMaxIntegratedHistorySeconds * sampleRate);
    while (!mIntegratedBlocks.empty() && mIntegratedSamples > maxIntegratedSamples) {
        mIntegratedSamples -= mIntegratedBlocks.front().samples;
        mIntegratedBlocks.pop_front();
    }
}

double DynamicsMeter::windowEnergy(uint64_t maxSamples) const {
    if (maxSamples == 0 || mEnergyHistory.empty()) {
        return 0.0;
    }

    auto remaining = maxSamples;
    auto sum = 0.0;
    uint64_t samples = 0;
    for (auto it = mEnergyHistory.rbegin(); it != mEnergyHistory.rend() && remaining > 0; ++it) {
        auto const take = std::min(remaining, it->samples);
        sum += it->energy * static_cast<double>(take);
        samples += take;
        remaining -= take;
    }

    return samples > 0 ? sum / static_cast<double>(samples) : 0.0;
}

double DynamicsMeter::gatedIntegratedEnergy() const {
    auto const& blocks = mIntegratedBlocks.empty() ? mEnergyHistory : mIntegratedBlocks;
    auto ungatedSum = 0.0;
    uint64_t ungatedSamples = 0;
    for (auto const& block : blocks) {
        auto const lufs = lufsFromMeanSquare(block.energy);
        if (lufs > gAbsoluteGateLufs) {
            ungatedSum += block.energy * static_cast<double>(block.samples);
            ungatedSamples += block.samples;
        }
    }

    if (ungatedSamples == 0) {
        return 0.0;
    }

    auto const ungatedEnergy = ungatedSum / static_cast<double>(ungatedSamples);
    auto const relativeGate = static_cast<double>(lufsFromMeanSquare(ungatedEnergy)) + gRelativeGateOffsetDb;
    auto gatedSum = 0.0;
    uint64_t gatedSamples = 0;
    for (auto const& block : blocks) {
        auto const lufs = lufsFromMeanSquare(block.energy);
        if (lufs > gAbsoluteGateLufs && lufs > relativeGate) {
            gatedSum += block.energy * static_cast<double>(block.samples);
            gatedSamples += block.samples;
        }
    }

    return gatedSamples > 0 ? gatedSum / static_cast<double>(gatedSamples) : ungatedEnergy;
}

float DynamicsMeter::lufsFromMeanSquare(double meanSquare) {
    if (meanSquare <= 0.0) {
        return gMinusInfinityDb;
    }
    return juce::jlimit(gMinusInfinityDb, 24.0f, -0.691f + (10.0f * std::log10(static_cast<float>(meanSquare))));
}

}  // namespace pe::processor

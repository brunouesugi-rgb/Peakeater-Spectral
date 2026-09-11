#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

#include "processor/Clipper.h"
#include "processor/DynamicsMeter.h"
#include "processor/LevelMeter.h"
#include "processor/OscilloscopeBuffer.h"
#include "processor/SpectralMaximizer.h"
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
#include "processor/LookaheadGainScheduler.h"
#endif

namespace pe::processor {

class PeakEaterAudioProcessor : public juce::AudioProcessor
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    , private juce::Timer
#endif
{
   public:
    PeakEaterAudioProcessor();
    ~PeakEaterAudioProcessor() override;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(BusesLayout const& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    void processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
#endif

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    void setEditorActive(bool isActive) noexcept;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, juce::String const& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(void const* data, int sizeInBytes) override;

    // Hack to preserve plugin size
    struct PluginSizeConstraints {
       #if PEAKEATER_SMALL_VARIANT
        int const minWidth = 860;
        int const minHeight = 580;
       #else
        int const minWidth = 920;
        int const minHeight = 620;
       #endif
        int const maxWidth = 3840;
        int const maxHeight = 2400;
        double const aspectRatio = 21.0 / 10.0;
    };
    struct PluginSizeState {
        int width;
        int height;
    };
    PluginSizeConstraints getPluginSizeConstraints() const;
    void setPluginSizeState(PluginSizeState const&& pluginSizeState);
    PluginSizeState getPluginSizeState() const;

   private:
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    void timerCallback() override;
    void runWetScheduler(juce::AudioBuffer<float>& buffer);
    LookaheadGainScheduler mWetScheduler;
    ParallelAlignmentDelay mDryAlignment;
    juce::dsp::Gain<float> mParallelOutputGain;
    std::atomic<int> mPreparedSchedulerDelay{0};
    std::atomic<bool> mSchedulerHighQuality{false};
    bool mSchedulerWasHighQuality = false;
    bool mSchedulerWasPassthrough = false;
#endif
    std::shared_ptr<juce::AudioProcessorValueTreeState> mParameters;
    juce::AudioParameterFloat* mInputGain;
    juce::AudioParameterFloat* mOutputGain;
    juce::AudioParameterBool* mLinkInOut;
    juce::AudioParameterBool* mBypass;
    juce::AudioParameterFloat* mThreshold;
    juce::AudioParameterFloat* mCeiling;
    juce::AudioParameterFloat* mTone;
    juce::AudioParameterChoice* mToneStyle;
    juce::AudioParameterChoice* mClippingType;
    juce::AudioParameterChoice* mOversampleRate;
    juce::AudioParameterFloat* mDryWet;
    juce::AudioParameterFloat* mAttack;
    juce::AudioParameterFloat* mHold;
    juce::AudioParameterFloat* mRelease;
    juce::AudioParameterFloat* mTransientRecovery;
    juce::AudioParameterFloat* mLookahead;
    juce::AudioParameterFloat* mDetectorHp;
    juce::AudioParameterFloat* mSaturation;
    juce::AudioParameterBool* mTruePeakLimit;
    juce::AudioParameterFloat* mAdaptiveRelease;
    juce::AudioParameterFloat* mStereoLink;
    juce::AudioParameterFloat* mLowProtect;
    juce::AudioParameterFloat* mTruePeakMargin;
    juce::AudioParameterFloat* mGrLimit;
    juce::AudioParameterFloat* mPunchProtect;
    juce::AudioParameterFloat* mReleaseShape;
    juce::AudioParameterChoice* mLimiterStyle;
    juce::AudioParameterFloat* mHfGuard;
    juce::AudioParameterFloat* mDetectorTilt;
    juce::AudioParameterBool* mDeltaListen;
    juce::AudioParameterFloat* mFinalClip;
    juce::AudioParameterChoice* mSaturationMode;
    juce::AudioParameterFloat* mSaturationTone;
    juce::AudioParameterFloat* mSaturationDensity;
    juce::AudioParameterFloat* mBassSafe;
    juce::AudioParameterBool* mGainMatch;
    juce::AudioParameterChoice* mDeltaSource;
    juce::AudioParameterFloat* mMeterTargetLufs;
    juce::AudioParameterFloat* mBassRecover;

    juce::AudioBuffer<float> dryBuffer;
    int mDryBufferCapacityChannels = 0;
    int mDryBufferCapacitySamples = 0;
    float mGainMatchDb = 0.0f;
    float mDryWetSmoothed = 1.0f;
    float mDryWetCompensationDb = 0.0f;
    float mDryWetPeakTrimGain = 1.0f;
    float mPostMixPeakTrimGain = 1.0f;
    int mPeakMeterUpdateCountdown = 0;
    int mLoudnessMeterUpdateCountdown = 0;
    int mVisualUpdateCountdown = 0;
    int mOscilloscopeUpdateCountdown = 0;
    bool mWasBypassed = false;
    std::atomic<bool> mEditorActive{false};
    juce::dsp::Gain<float> inputGain;
    std::array<Clipper<float>, 6> clippers{Clipper<float>{0}, Clipper<float>{1}, Clipper<float>{2},
                                           Clipper<float>{3}, Clipper<float>{4}, Clipper<float>{5}};
    SpectralMaximizer spectralMaximizer;
    juce::dsp::Gain<float> outputGain;

    std::shared_ptr<processor::LevelMeter<float>> mLevelMeterPostIn;
    std::shared_ptr<processor::LevelMeter<float>> mLevelMeterPostClipper;
    std::shared_ptr<processor::LevelMeter<float>> mLevelMeterPostOut;
    std::shared_ptr<processor::DynamicsMeter> mDynamicsMeter;
    std::shared_ptr<processor::OscilloscopeBuffer> mOscilloscopeBuffer;

    PluginSizeConstraints mPluginSizeConstraints;
    PluginSizeState mPluginSizeState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PeakEaterAudioProcessor)
};

}  // namespace pe::processor

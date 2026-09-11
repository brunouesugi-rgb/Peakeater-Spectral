#include "PluginProcessor.h"

#include "Parameters.h"
#include "PluginEditor.h"

#include <cmath>

//==============================================================================
namespace pe::processor {

namespace {
[[nodiscard]] SpectralMaximizer::Mode parameterChoiceToSpectralMode(int const &parameterChoice) {
    auto mode = SpectralMaximizer::Mode::EDM;
    switch (parameterChoice) {
        case 0:
            mode = SpectralMaximizer::Mode::EDM;
            break;
        case 1:
            mode = SpectralMaximizer::Mode::HipHop;
            break;
        case 2:
            mode = SpectralMaximizer::Mode::Drums;
            break;
        case 3:
            mode = SpectralMaximizer::Mode::OneShot;
            break;
        case 4:
            mode = SpectralMaximizer::Mode::Acoustic;
            break;
        case 5:
            mode = SpectralMaximizer::Mode::Vocal;
            break;
        case 6:
            mode = SpectralMaximizer::Mode::Bass;
            break;
        case 7:
            mode = SpectralMaximizer::Mode::Bright;
            break;
        case 8:
            mode = SpectralMaximizer::Mode::Glue;
            break;
        case 9:
            mode = SpectralMaximizer::Mode::Clean;
            break;
        case 10:
            mode = SpectralMaximizer::Mode::Percs;
            break;
        case 11:
            mode = SpectralMaximizer::Mode::Dubstep;
            break;
        case 12:
            mode = SpectralMaximizer::Mode::DrumNBass;
            break;
        case 13:
            mode = SpectralMaximizer::Mode::House;
            break;
        case 14:
            mode = SpectralMaximizer::Mode::Trap;
            break;
        case 15:
            mode = SpectralMaximizer::Mode::Kick808;
            break;
        case 16:
            mode = SpectralMaximizer::Mode::OneShotClean;
            break;
        default:
            mode = SpectralMaximizer::Mode::EDM;
            break;
    }
    return mode;
}

[[nodiscard]] size_t oversampleChoiceToBandMultiplier(int const &parameterChoice) {
#if PEAKEATER_SMALL_VARIANT
    switch (parameterChoice) {
        case 0:
        case 1:
        case 2:
        case 3:
            return 1;
        case 4:
        case 5:
            return 2;
        default:
            return 1;
    }
#else
    switch (parameterChoice) {
        case 0:
            return 1;
        case 1:
            return 2;
        case 2:
            return 4;
        case 3:
            return 8;
        case 4:
            return 16;
        case 5:
            return 32;
        default:
            return 1;
    }
#endif
}

[[nodiscard]] SpectralMaximizer::ToneStyle parameterChoiceToToneStyle(int const &parameterChoice) {
    switch (parameterChoice) {
        case 0:
            return SpectralMaximizer::ToneStyle::Warm;
        case 2:
            return SpectralMaximizer::ToneStyle::Bright;
        case 1:
        default:
            return SpectralMaximizer::ToneStyle::Clean;
    }
}

[[nodiscard]] SpectralMaximizer::LimiterStyle parameterChoiceToLimiterStyle(int const &parameterChoice) {
    switch (parameterChoice) {
        case 1:
            return SpectralMaximizer::LimiterStyle::Punch;
        case 2:
            return SpectralMaximizer::LimiterStyle::Glue;
        case 3:
            return SpectralMaximizer::LimiterStyle::Safe;
        case 4:
            return SpectralMaximizer::LimiterStyle::Loud;
        case 0:
        default:
            return SpectralMaximizer::LimiterStyle::Transparent;
    }
}

[[nodiscard]] SpectralMaximizer::SaturationStyle parameterChoiceToSaturationStyle(int const &parameterChoice) {
    switch (parameterChoice) {
        case 1:
            return SpectralMaximizer::SaturationStyle::Warm;
        case 2:
            return SpectralMaximizer::SaturationStyle::Tube;
        case 3:
            return SpectralMaximizer::SaturationStyle::Tape;
        case 4:
            return SpectralMaximizer::SaturationStyle::Transformer;
        case 0:
        default:
            return SpectralMaximizer::SaturationStyle::Clean;
    }
}

[[nodiscard]] SpectralMaximizer::DeltaSource parameterChoiceToDeltaSource(int const &parameterChoice) {
    switch (parameterChoice) {
        case 1:
            return SpectralMaximizer::DeltaSource::Limiter;
        case 2:
            return SpectralMaximizer::DeltaSource::Spectral;
        case 3:
            return SpectralMaximizer::DeltaSource::SatClip;
        case 4:
            return SpectralMaximizer::DeltaSource::Ceiling;
        case 0:
        default:
            return SpectralMaximizer::DeltaSource::All;
    }
}

[[nodiscard]] std::pair<float, float> dryWetParallelGains(float wetAmount) {
    auto const wet = juce::jlimit(0.0f, 1.0f, wetAmount);
    return {1.0f - wet, wet};
}

[[nodiscard]] juce::Identifier peakeaterStateType() {
    return juce::Identifier("PeakeaterSpectralState");
}

[[nodiscard]] bool isLegacyOrCurrentPeakeaterState(juce::XmlElement const& xml) {
    auto const tag = xml.getTagName();
    if (tag == peakeaterStateType().toString() || tag == juce::String(JucePlugin_Name)) {
        return true;
    }

    // Older builds used the product name as the APVTS ValueTree type. Accept
    // state chunks that clearly contain Peakeater's parameter properties even
    // when the wrapper/product name has changed between Beta/2/3.
    return xml.hasAttribute("InputGain") || xml.hasAttribute("OutputGain") || xml.hasAttribute("Threshold") ||
           xml.hasAttribute("ClippingType") || xml.hasAttribute("DryWet");
}

[[nodiscard]] juce::ValueTree withCurrentStateType(juce::ValueTree const& restoredState, juce::Identifier const& targetType) {
    if (!restoredState.isValid() || restoredState.getType() == targetType) {
        return restoredState;
    }

    juce::ValueTree migrated(targetType);
    migrated.copyPropertiesAndChildrenFrom(restoredState, nullptr);
    return migrated;
}

[[nodiscard]] float bufferRmsDb(juce::AudioBuffer<float> const& buffer) {
    auto sumSquares = 0.0;
    auto sampleCount = 0;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
        auto const* samples = buffer.getReadPointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            auto const value = std::isfinite(samples[sample]) ? samples[sample] : 0.0f;
            sumSquares += static_cast<double>(value) * static_cast<double>(value);
            ++sampleCount;
        }
    }
    auto const rms = sampleCount > 0 ? std::sqrt(sumSquares / static_cast<double>(sampleCount)) : 0.0;
    return juce::Decibels::gainToDecibels(static_cast<float>(std::max(rms, 0.000001)), -120.0f);
}

[[nodiscard]] float bufferPeakGain(juce::AudioBuffer<float> const& buffer) {
    auto peak = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
        auto const* samples = buffer.getReadPointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            peak = std::max(peak, std::abs(std::isfinite(samples[sample]) ? samples[sample] : 0.0f));
        }
    }
    return peak;
}

void applyPeakSafetyTrim(juce::AudioBuffer<float>& buffer, float maxAllowedGain, float& trimGain, double sampleRate) {
    maxAllowedGain = juce::jlimit(0.000001f, 1.0f, maxAllowedGain);
    auto peak = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
        auto* samples = buffer.getWritePointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            auto const value = std::isfinite(samples[sample]) ? samples[sample] : 0.0f;
            samples[sample] = value;
            peak = std::max(peak, std::abs(value));
        }
    }

    auto const targetTrim = peak > maxAllowedGain && peak > 0.000001f ? juce::jlimit(0.0f, 1.0f, maxAllowedGain / peak) : 1.0f;
    if (targetTrim < trimGain) {
        trimGain = targetTrim;
    } else {
        auto const releaseCoefficient =
            std::exp(-static_cast<float>(buffer.getNumSamples()) / (0.030f * static_cast<float>(std::max(1.0, sampleRate))));
        trimGain = (releaseCoefficient * trimGain) + ((1.0f - releaseCoefficient) * targetTrim);
    }
    trimGain = juce::jlimit(0.0f, 1.0f, trimGain);

    if (trimGain < 0.999999f) {
        buffer.applyGain(trimGain);
    }
}

void applyParallelMixLevelTrim(juce::AudioBuffer<float>& buffer, float targetPeakGain, float& trimGain, double sampleRate) {
    targetPeakGain = std::max(0.000001f, targetPeakGain);
    auto peak = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
        auto* samples = buffer.getWritePointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            auto const value = std::isfinite(samples[sample]) ? samples[sample] : 0.0f;
            samples[sample] = value;
            peak = std::max(peak, std::abs(value));
        }
    }

    auto const targetTrim = peak > targetPeakGain && peak > 0.000001f ? juce::jlimit(0.0f, 1.0f, targetPeakGain / peak) : 1.0f;
    if (targetTrim < trimGain) {
        trimGain = targetTrim;
    } else {
        auto const releaseCoefficient =
            std::exp(-static_cast<float>(buffer.getNumSamples()) / (0.018f * static_cast<float>(std::max(1.0, sampleRate))));
        trimGain = (releaseCoefficient * trimGain) + ((1.0f - releaseCoefficient) * targetTrim);
    }
    trimGain = juce::jlimit(0.0f, 1.0f, trimGain);

    if (trimGain < 0.999999f) {
        buffer.applyGain(trimGain);
    }
}

[[nodiscard]] int realtimeDryBufferSampleCapacity(int expectedSamplesPerBlock) {
#if PEAKEATER_SMALL_VARIANT
    return juce::jmax(4096, expectedSamplesPerBlock * 3);
#else
    return juce::jmax(8192, expectedSamplesPerBlock * 4);
#endif
}
}  // namespace

PeakEaterAudioProcessor::PeakEaterAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if !JucePlugin_IsMidiEffect
#if !JucePlugin_IsSynth
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
                         ),
#endif
      mParameters(std::make_shared<juce::AudioProcessorValueTreeState>(
          *this, nullptr, peakeaterStateType(), pe::params::ParametersProvider::getInstance().createParameterLayout())),
      mInputGain(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getInputGain().getId().getParamID()))),
      mOutputGain(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getOutputGain().getId().getParamID()))),
      mLinkInOut(static_cast<juce::AudioParameterBool *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getLinkInOut().getId().getParamID()))),
      mBypass(static_cast<juce::AudioParameterBool *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getBypass().getId().getParamID()))),
      mThreshold(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getThreshold().getId().getParamID()))),
      mCeiling(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getCeiling().getId().getParamID()))),
      mTone(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getTone().getId().getParamID()))),
      mToneStyle(static_cast<juce::AudioParameterChoice *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getToneStyle().getId().getParamID()))),
      mClippingType(static_cast<juce::AudioParameterChoice *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getClippingType().getId().getParamID()))),
      mOversampleRate(static_cast<juce::AudioParameterChoice *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getOversampleRate().getId().getParamID()))),
      mDryWet(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getDryWet().getId().getParamID()))),
      mAttack(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getAttack().getId().getParamID()))),
      mHold(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getHold().getId().getParamID()))),
      mRelease(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getRelease().getId().getParamID()))),
      mTransientRecovery(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getTransientRecovery().getId().getParamID()))),
      mLookahead(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getLookahead().getId().getParamID()))),
      mDetectorHp(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getDetectorHp().getId().getParamID()))),
      mSaturation(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getSaturation().getId().getParamID()))),
      mTruePeakLimit(static_cast<juce::AudioParameterBool *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getTruePeakLimit().getId().getParamID()))),
      mAdaptiveRelease(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getAdaptiveRelease().getId().getParamID()))),
      mStereoLink(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getStereoLink().getId().getParamID()))),
      mLowProtect(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getLowProtect().getId().getParamID()))),
      mTruePeakMargin(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getTruePeakMargin().getId().getParamID()))),
      mGrLimit(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getGrLimit().getId().getParamID()))),
      mPunchProtect(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getPunchProtect().getId().getParamID()))),
      mReleaseShape(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getReleaseShape().getId().getParamID()))),
      mLimiterStyle(static_cast<juce::AudioParameterChoice *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getLimiterStyle().getId().getParamID()))),
      mHfGuard(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getHfGuard().getId().getParamID()))),
      mDetectorTilt(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getDetectorTilt().getId().getParamID()))),
      mDeltaListen(static_cast<juce::AudioParameterBool *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getDeltaListen().getId().getParamID()))),
      mFinalClip(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getFinalClip().getId().getParamID()))),
      mSaturationMode(static_cast<juce::AudioParameterChoice *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getSaturationMode().getId().getParamID()))),
      mSaturationTone(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getSaturationTone().getId().getParamID()))),
      mSaturationDensity(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getSaturationDensity().getId().getParamID()))),
      mBassSafe(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getBassSafe().getId().getParamID()))),
      mGainMatch(static_cast<juce::AudioParameterBool *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getGainMatch().getId().getParamID()))),
      mDeltaSource(static_cast<juce::AudioParameterChoice *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getDeltaSource().getId().getParamID()))),
      mMeterTargetLufs(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getMeterTargetLufs().getId().getParamID()))),
      mBassRecover(static_cast<juce::AudioParameterFloat *>(
          mParameters->getParameter(pe::params::ParametersProvider::getInstance().getBassRecover().getId().getParamID()))),
      mLevelMeterPostIn(std::make_shared<processor::LevelMeter<float>>()),
      mLevelMeterPostClipper(std::make_shared<processor::LevelMeter<float>>()),
      mLevelMeterPostOut(std::make_shared<processor::LevelMeter<float>>()),
      mDynamicsMeter(std::make_shared<processor::DynamicsMeter>()),
      mOscilloscopeBuffer(std::make_shared<processor::OscilloscopeBuffer>()),
      mPluginSizeState({mPluginSizeConstraints.minWidth, mPluginSizeConstraints.minHeight}) {
}

PeakEaterAudioProcessor::~PeakEaterAudioProcessor() {
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    stopTimer();
#endif
}

#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
void PeakEaterAudioProcessor::timerCallback() {
    auto const delay = mPreparedSchedulerDelay.load(std::memory_order_acquire);
    if (delay <= 0) return;
    auto const high = mOversampleRate->getIndex() >= 2;
    if (high != mSchedulerHighQuality.load(std::memory_order_relaxed)) {
        setLatencySamples(high ? delay : 0);
        mSchedulerHighQuality.store(high, std::memory_order_release);
    }
}

void PeakEaterAudioProcessor::runWetScheduler(juce::AudioBuffer<float>& buffer) {
    auto const margin = *mTruePeakLimit ? juce::jlimit(0.0f, 1.0f, static_cast<float>(*mTruePeakMargin)) : 0.0f;
    mWetScheduler.configure(juce::Decibels::decibelsToGain(std::min(0.0f, static_cast<float>(*mCeiling)) - margin),
                            *mRelease, *mTruePeakLimit, *mAdaptiveRelease);
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        auto const result = mWetScheduler.process({left[i], right ? right[i] : left[i]}, !*mDeltaListen);
        left[i] = result[0];
        if (right) right[i] = result[1];
    }
}

void PeakEaterAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    if (buffer.getNumChannels() == 0) return;
    auto const high = mSchedulerHighQuality.load(std::memory_order_acquire);
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        auto const result = mDryAlignment.process({left[i], right ? right[i] : left[i]}, high);
        left[i] = result[0];
        if (right) right[i] = result[1];
    }
    mWetScheduler.reset();
}
#endif

//==============================================================================
const juce::String PeakEaterAudioProcessor::getName() const { return JucePlugin_Name; }

bool PeakEaterAudioProcessor::acceptsMidi() const {
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool PeakEaterAudioProcessor::producesMidi() const {
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool PeakEaterAudioProcessor::isMidiEffect() const {
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double PeakEaterAudioProcessor::getTailLengthSeconds() const {
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    return 0.005;
#else
    return 0.0;
#endif
}

int PeakEaterAudioProcessor::getNumPrograms() {
    // NB: some hosts don't cope very well if you tell them there are 0
    // programs,
    // so this should be at least 1, even if you're not really implementing
    // programs.
    return 1;
}

int PeakEaterAudioProcessor::getCurrentProgram() { return 1; }

void PeakEaterAudioProcessor::setCurrentProgram(int /* index */) {
    // Since we are not supporting presets
}

const juce::String PeakEaterAudioProcessor::getProgramName(int /* index */) { return "default"; }

void PeakEaterAudioProcessor::changeProgramName(int /* index */, juce::String const & /* newName */) {
    // Since we are not supporting presets
}

void PeakEaterAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::dsp::ProcessSpec const spec{sampleRate, static_cast<juce::uint32>(samplesPerBlock), 2};
    mDryBufferCapacityChannels = 2;
    mDryBufferCapacitySamples = realtimeDryBufferSampleCapacity(samplesPerBlock);
    dryBuffer.setSize(mDryBufferCapacityChannels, mDryBufferCapacitySamples, false, false, true);
    inputGain.prepare(spec);
    for (auto &clipper : clippers) {
        clipper.prepare(spec);
    }
    spectralMaximizer.prepare(spec);
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    mWetScheduler.prepare(sampleRate);
    mDryAlignment.prepare(mWetScheduler.latencySamples());
    mParallelOutputGain.prepare(spec);
    mSchedulerWasPassthrough = false;
    mPreparedSchedulerDelay.store(mWetScheduler.latencySamples(), std::memory_order_release);
    mSchedulerWasHighQuality = mOversampleRate->getIndex() >= 2;
    mSchedulerHighQuality.store(mSchedulerWasHighQuality, std::memory_order_release);
    setLatencySamples(mSchedulerWasHighQuality ? mWetScheduler.latencySamples() : 0);
    startTimer(50);
#endif
    outputGain.prepare(spec);
    mDryWetSmoothed = static_cast<float>(*mDryWet);
    mDryWetCompensationDb = 0.0f;
    mDryWetPeakTrimGain = 1.0f;
    mPostMixPeakTrimGain = 1.0f;
    mPeakMeterUpdateCountdown = 0;
    mLoudnessMeterUpdateCountdown = 0;
    mOscilloscopeUpdateCountdown = 0;
}

void PeakEaterAudioProcessor::releaseResources() {
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    stopTimer();
    mPreparedSchedulerDelay.store(0, std::memory_order_release);
    mWetScheduler.reset();
    mDryAlignment.reset();
    mParallelOutputGain.reset();
#endif
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
    outputGain.reset();
    mGainMatchDb = 0.0f;
    mDryWetSmoothed = 1.0f;
    mDryWetCompensationDb = 0.0f;
    mDryWetPeakTrimGain = 1.0f;
    mPostMixPeakTrimGain = 1.0f;
    mDynamicsMeter->reset();
    mOscilloscopeBuffer->reset();
    spectralMaximizer.reset();
    for (auto &clipper : clippers) {
        clipper.reset();
    }
    inputGain.reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool PeakEaterAudioProcessor::isBusesLayoutSupported(BusesLayout const &layouts) const {
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() &&
        layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) {
        return false;
    }

    // This checks if the input layout matches the output layout
#if !JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet()) {
        return false;
    }
#endif

    return true;
#endif
}
#endif

void PeakEaterAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer & /* midiMessages */) {
    juce::ScopedNoDenormals const noDenormals;
    if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0 || getSampleRate() <= 0.0) {
        return;
    }

    //-----------------------------------------------------------
    // Propagate params to DSP processors
    auto const inputGainDb = static_cast<float>(*mInputGain);
    auto const outputGainDb = *mLinkInOut ? -inputGainDb : static_cast<float>(*mOutputGain);
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    mParallelOutputGain.setGainDecibels(outputGainDb);
#endif
    inputGain.setGainDecibels(inputGainDb);
    spectralMaximizer.setMode(parameterChoiceToSpectralMode(*mClippingType));
    spectralMaximizer.setThreshold(*mThreshold);
    spectralMaximizer.setCeiling(*mCeiling);
    spectralMaximizer.setTone(*mTone);
    spectralMaximizer.setToneStyle(parameterChoiceToToneStyle(*mToneStyle));
    spectralMaximizer.setAttack(*mAttack);
    spectralMaximizer.setHold(*mHold);
    spectralMaximizer.setRelease(*mRelease);
    spectralMaximizer.setTransientRecovery(*mTransientRecovery);
    spectralMaximizer.setLookahead(*mLookahead);
    spectralMaximizer.setBandMultiplier(oversampleChoiceToBandMultiplier(*mOversampleRate));
    spectralMaximizer.setDetectorHp(*mDetectorHp);
    spectralMaximizer.setSaturation(*mSaturation);
    spectralMaximizer.setTruePeakLimit(*mTruePeakLimit);
    spectralMaximizer.setAdaptiveRelease(*mAdaptiveRelease);
    spectralMaximizer.setStereoLink(*mStereoLink);
    spectralMaximizer.setLowProtect(*mLowProtect);
    spectralMaximizer.setTruePeakMargin(*mTruePeakMargin);
    spectralMaximizer.setGrLimit(*mGrLimit);
    spectralMaximizer.setPunchProtect(*mPunchProtect);
    spectralMaximizer.setReleaseShape(*mReleaseShape);
    spectralMaximizer.setLimiterStyle(parameterChoiceToLimiterStyle(*mLimiterStyle));
    spectralMaximizer.setHfGuard(*mHfGuard);
    spectralMaximizer.setDetectorTilt(*mDetectorTilt);
    spectralMaximizer.setDeltaListen(*mDeltaListen);
    spectralMaximizer.setDeltaSource(parameterChoiceToDeltaSource(*mDeltaSource));
    spectralMaximizer.setFinalClip(*mFinalClip);
    spectralMaximizer.setSaturationStyle(parameterChoiceToSaturationStyle(*mSaturationMode));
    spectralMaximizer.setSaturationTone(*mSaturationTone);
    spectralMaximizer.setSaturationDensity(*mSaturationDensity);
    spectralMaximizer.setBassSafe(*mBassSafe);
    spectralMaximizer.setBassRecover(*mBassRecover);
    outputGain.setGainDecibels(outputGainDb);
    //-----------------------------------------------------------
    // Do actual DSP
    juce::dsp::AudioBlock<float> audioBlock(buffer);
    juce::dsp::ProcessContextReplacing const context(audioBlock);
    auto const editorActive = mEditorActive.load(std::memory_order_relaxed);
    auto const shouldUpdateVisuals = editorActive && --mVisualUpdateCountdown <= 0;
    if (shouldUpdateVisuals) {
#if PEAKEATER_SMALL_VARIANT
        mVisualUpdateCountdown = 12;
#else
        mVisualUpdateCountdown = 8;
#endif
    }
    auto const bypassed = static_cast<bool>(*mBypass);
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
    auto const schedulerHigh = mSchedulerHighQuality.load(std::memory_order_acquire);
    if (schedulerHigh != mSchedulerWasHighQuality) {
        mWetScheduler.reset();
        mDryAlignment.reset();
        mSchedulerWasHighQuality = schedulerHigh;
    }
    auto const alignedDryReady = buffer.getNumChannels() <= dryBuffer.getNumChannels()
                                  && buffer.getNumSamples() <= dryBuffer.getNumSamples();
    auto const rawPassthrough = bypassed || static_cast<float>(*mDryWet) <= 0.0005f;
    auto* alignmentLeft = buffer.getWritePointer(0);
    auto* alignmentRight = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    for (int i = 0; schedulerHigh && i < buffer.getNumSamples(); ++i) {
        auto const aligned = mDryAlignment.process({alignmentLeft[i], alignmentRight ? alignmentRight[i] : alignmentLeft[i]}, schedulerHigh);
        if (alignedDryReady) {
            dryBuffer.setSample(0, i, aligned[0]);
            if (alignmentRight) dryBuffer.setSample(1, i, aligned[1]);
        }
        if (rawPassthrough && schedulerHigh) {
            alignmentLeft[i] = aligned[0];
            if (alignmentRight) alignmentRight[i] = aligned[1];
        }
    }
    if (rawPassthrough && !mSchedulerWasPassthrough) mWetScheduler.reset();
    mSchedulerWasPassthrough = rawPassthrough;
#endif
    if (!bypassed) {
        auto const dryWetTarget = juce::jlimit(0.0f, 1.0f, static_cast<float>(*mDryWet));
        auto const dryWetCoefficient =
            std::exp(-static_cast<float>(buffer.getNumSamples()) / (0.028f * static_cast<float>(std::max(1.0, getSampleRate()))));
        mDryWetSmoothed = (dryWetCoefficient * mDryWetSmoothed) + ((1.0f - dryWetCoefficient) * dryWetTarget);
        if (std::abs(mDryWetSmoothed - dryWetTarget) < 0.0005f) {
            mDryWetSmoothed = dryWetTarget;
        }
        if (dryWetTarget <= 0.0005f) {
            mDryWetSmoothed = 0.0f;
        }
        auto const dryWet = juce::jlimit(0.0f, 1.0f, mDryWetSmoothed);
        auto const dryBufferReady = buffer.getNumChannels() <= dryBuffer.getNumChannels() && buffer.getNumSamples() <= dryBuffer.getNumSamples();
        auto const effectiveDryWet = dryWet <= 0.0005f ? 0.0f : (dryBufferReady ? dryWet : 1.0f);
        auto const dryOnly = effectiveDryWet <= 0.0005f;
        auto const needsDryPath = (effectiveDryWet < 0.9995f || (*mGainMatch && !dryOnly)) && dryBufferReady;
        auto const shouldProcessWetPath = !dryOnly && (dryWet > 0.0005f || *mGainMatch || *mDeltaListen);
        if (needsDryPath
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
            && !schedulerHigh
#endif
        ) {
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
                dryBuffer.copyFrom(channel, 0, buffer, channel, 0, buffer.getNumSamples());
            }
        }
        if (dryOnly) {
            spectralMaximizer.reset();
            mGainMatchDb = 0.0f;
            mDryWetCompensationDb = 0.0f;
            mDryWetPeakTrimGain = 1.0f;
            mPostMixPeakTrimGain = 1.0f;
            outputGain.reset();
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
            mParallelOutputGain.reset();
#endif
            inputGain.reset();
            if (shouldUpdateVisuals) {
                mLevelMeterPostIn->updateLevels(context.getOutputBlock());
                mLevelMeterPostClipper->updateLevels(context.getOutputBlock());
                mLevelMeterPostOut->updateLevels(context.getOutputBlock());
            }
        } else {
        inputGain.process(context);
        if (shouldUpdateVisuals) {
            mLevelMeterPostIn->updateLevels(context.getOutputBlock());
        }
        if (shouldProcessWetPath) {
            spectralMaximizer.process(context);
        } else {
            spectralMaximizer.reset();
        }
        if (shouldUpdateVisuals) {
            mLevelMeterPostClipper->updateLevels(context.getOutputBlock());
        }
        auto const parallelMixActive = effectiveDryWet > 0.0005f && effectiveDryWet < 0.9995f;
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
        if (schedulerHigh) {
            outputGain.process(context);
            if (alignedDryReady) {
                juce::dsp::AudioBlock<float> dryBlock(dryBuffer);
                auto drySpan = dryBlock.getSubsetChannelBlock(0, buffer.getNumChannels()).getSubBlock(0, buffer.getNumSamples());
                juce::dsp::ProcessContextReplacing<float> dryContext(drySpan);
                mParallelOutputGain.process(dryContext);
            }
            runWetScheduler(buffer);
        }
#endif
        auto const dryRmsDb = *mGainMatch && needsDryPath ? bufferRmsDb(dryBuffer) : -120.0f;
        auto const wetRmsDb = *mGainMatch && shouldProcessWetPath ? bufferRmsDb(buffer) : -120.0f;
        auto const dryPeakBeforeMix = parallelMixActive && needsDryPath ? bufferPeakGain(dryBuffer) : 0.0f;
        auto const wetPeakBeforeMix = parallelMixActive && shouldProcessWetPath ? bufferPeakGain(buffer) : 0.0f;
        auto dryGainForParallel = 0.0f;
        auto wetGainForParallel = 1.0f;
        if (effectiveDryWet < 0.9995f) {
            auto const [dry, wet] = dryWetParallelGains(effectiveDryWet);
            dryGainForParallel = dry;
            wetGainForParallel = wet;
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
                auto* wetSamples = buffer.getWritePointer(channel);
                auto const* drySamples = dryBuffer.getReadPointer(channel);
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
                    wetSamples[sample] = (drySamples[sample] * dry) + (wetSamples[sample] * wet);
                }
            }
        }
        if (*mGainMatch && effectiveDryWet > 0.0005f && dryBufferReady) {
            auto const targetGainDb = juce::jlimit(-12.0f, 0.0f, dryRmsDb - wetRmsDb);
            auto const coefficient =
                std::exp(-static_cast<float>(buffer.getNumSamples()) / (0.12f * static_cast<float>(std::max(1.0, getSampleRate()))));
            mGainMatchDb = (coefficient * mGainMatchDb) + ((1.0f - coefficient) * targetGainDb);
            buffer.applyGain(juce::Decibels::decibelsToGain(mGainMatchDb));
        } else {
            auto const coefficient =
                std::exp(-static_cast<float>(buffer.getNumSamples()) / (0.08f * static_cast<float>(std::max(1.0, getSampleRate()))));
            mGainMatchDb *= coefficient;
        }
        mDryWetCompensationDb = 0.0f;
        auto peakCeilingDb = std::min(0.0f, static_cast<float>(*mCeiling));
        if (*mTruePeakLimit) {
            peakCeilingDb -= juce::jlimit(0.0f, 1.0f, static_cast<float>(*mTruePeakMargin));
        }
        auto const peakCeilingGain = juce::Decibels::decibelsToGain(peakCeilingDb);
        if (parallelMixActive) {
            auto const overlap = effectiveDryWet * (1.0f - effectiveDryWet) * 4.0f;
            auto const parallelMakeupGain = juce::Decibels::decibelsToGain(1.0f + (juce::jlimit(0.0f, 1.0f, overlap) * 1.0f));
            auto const weightedReferencePeak =
                (dryPeakBeforeMix * dryGainForParallel) + (wetPeakBeforeMix * wetGainForParallel);
            auto const parallelTargetPeak = std::max(0.000001f, weightedReferencePeak * parallelMakeupGain);
            applyParallelMixLevelTrim(buffer, parallelTargetPeak, mDryWetPeakTrimGain, getSampleRate());
            mPostMixPeakTrimGain = 1.0f;
        } else {
            applyPeakSafetyTrim(buffer, peakCeilingGain, mDryWetPeakTrimGain, getSampleRate());
        }
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
        if (!schedulerHigh) outputGain.process(context);
#else
        outputGain.process(context);
#endif
        if (!parallelMixActive) {
            applyPeakSafetyTrim(buffer, peakCeilingGain, mPostMixPeakTrimGain, getSampleRate());
        }
        if (shouldUpdateVisuals) {
            mLevelMeterPostOut->updateLevels(context.getOutputBlock());
        }
        }
    } else if (!mWasBypassed) {
        mDynamicsMeter->reset();
    }
    mWasBypassed = bypassed;
    auto updatePeakMeter = false;
    auto updateLoudnessMeter = false;
    if (editorActive && --mPeakMeterUpdateCountdown <= 0) {
#if PEAKEATER_SMALL_VARIANT
        mPeakMeterUpdateCountdown = 36;
#else
        mPeakMeterUpdateCountdown = 32;
#endif
        updatePeakMeter = true;
    }
    if (editorActive && --mLoudnessMeterUpdateCountdown <= 0) {
#if PEAKEATER_SMALL_VARIANT
        mLoudnessMeterUpdateCountdown = 144;
#else
        mLoudnessMeterUpdateCountdown = 128;
#endif
        updatePeakMeter = true;
        updateLoudnessMeter = true;
    }
    if (updatePeakMeter || updateLoudnessMeter) {
        if (*mBypass) {
            mDynamicsMeter->update(0.0f, 0.0f, buffer, getSampleRate(), updatePeakMeter, updateLoudnessMeter);
        } else {
            mDynamicsMeter->update(spectralMaximizer.getClipAmountDb(), spectralMaximizer.getGainReductionDb(), buffer, getSampleRate(),
                                   updatePeakMeter, updateLoudnessMeter);
        }
    }
    auto const shouldUpdateOscilloscope = editorActive && --mOscilloscopeUpdateCountdown <= 0;
    if (shouldUpdateOscilloscope) {
#if PEAKEATER_SMALL_VARIANT
        mOscilloscopeUpdateCountdown = 10;
#else
        mOscilloscopeUpdateCountdown = 6;
#endif
        mOscilloscopeBuffer->push(buffer);
    }
}

bool PeakEaterAudioProcessor::hasEditor() const {
    // change this to false if you choose to not supply an editor
    return true;
}

juce::AudioProcessorEditor *PeakEaterAudioProcessor::createEditor() {
    return new pe::PeakEaterAudioProcessorEditor(
        *this, mParameters,
        {.inputLevelMeter = mLevelMeterPostIn,
         .clippingLevelMeter = mLevelMeterPostClipper,
         .outputLevelMeter = mLevelMeterPostOut,
         .dynamicsMeter = mDynamicsMeter,
         .oscilloscopeBuffer = mOscilloscopeBuffer});
}

void PeakEaterAudioProcessor::setEditorActive(bool isActive) noexcept {
    mEditorActive.store(isActive, std::memory_order_relaxed);
}

void PeakEaterAudioProcessor::getStateInformation(juce::MemoryBlock &destData) {
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    auto const state = mParameters->copyState();
    std::unique_ptr<juce::XmlElement> const xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void PeakEaterAudioProcessor::setStateInformation(void const *data, int sizeInBytes) {
    // You should use this method to restore your parameters from this memory
    // block, whose contents will have been created by the getStateInformation()
    // call.
    std::unique_ptr<juce::XmlElement> const xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr && isLegacyOrCurrentPeakeaterState(*xmlState)) {
        auto restoredState = juce::ValueTree::fromXml(*xmlState);
        restoredState = withCurrentStateType(restoredState, mParameters->state.getType());
        if (restoredState.isValid()) {
            mParameters->replaceState(restoredState);
        }
    }
}

PeakEaterAudioProcessor::PluginSizeConstraints PeakEaterAudioProcessor::getPluginSizeConstraints() const { return mPluginSizeConstraints; }

void PeakEaterAudioProcessor::setPluginSizeState(PluginSizeState const &&pluginSizeState) { mPluginSizeState = pluginSizeState; }

PeakEaterAudioProcessor::PluginSizeState PeakEaterAudioProcessor::getPluginSizeState() const { return mPluginSizeState; }

}  // namespace pe::processor

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new pe::processor::PeakEaterAudioProcessor(); }

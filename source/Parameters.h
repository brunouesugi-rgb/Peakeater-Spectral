#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

//==============================================================================
/*
 * Helper namespace to keep consistent parameter info
 * across all project. For example, to keep label text similar
 * in the automation section and in the UI.
 */
namespace pe
{
namespace params
{

    class Parameter
    {
    public:
        Parameter (int const hint, juce::String const& id, juce::String const& label)
            : mParameterID (id, hint), mLabel (label)
        {
        }

        [[nodiscard]] juce::ParameterID const& getId() const { return mParameterID; }

        juce::String getLabel() const { return mLabel; }

    private:
        juce::ParameterID const mParameterID;
        juce::String mLabel;
    };

    template <typename T>
    class RangedParameter : public Parameter
    {
    public:
        RangedParameter (int const hint,
                         juce::String const& id,
                         juce::String const& label,
                         juce::NormalisableRange<T> const&& range)
            : Parameter (hint, id, label), mRange (range)
        {
        }

        juce::NormalisableRange<T>& getRange() { return mRange; }

    private:
        juce::NormalisableRange<T> mRange;
    };

    class ChoicingParameter : public Parameter
    {
    public:
        ChoicingParameter (int const hint,
                           juce::String const& id,
                           juce::String const& label,
                           juce::StringArray const&& choices)
            : Parameter (hint, id, label), mChoices (choices)
        {
        }

        juce::StringArray& getChoices() { return mChoices; }

    private:
        juce::StringArray mChoices;
    };

    class ParametersProvider
    {
    public:
        static ParametersProvider& getInstance()
        {
            static ParametersProvider pp;
            return pp;
        }

        juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
        {
            return {
                std::make_unique<juce::AudioParameterFloat> (
                    mInputGain.getId(), mInputGain.getLabel(), mInputGain.getRange(), 0.0f),
                std::make_unique<juce::AudioParameterFloat> (
                    mOutputGain.getId(), mOutputGain.getLabel(), mOutputGain.getRange(), 0.0f),
                std::make_unique<juce::AudioParameterFloat> (
                    mThreshold.getId(), mThreshold.getLabel(), mThreshold.getRange(), -6.0f),
                std::make_unique<juce::AudioParameterFloat> (
                    mCeiling.getId(), mCeiling.getLabel(), mCeiling.getRange(), 0.0f),
                std::make_unique<juce::AudioParameterFloat> (
                    mTone.getId(), mTone.getLabel(), mTone.getRange(), 0.0f),
                std::make_unique<juce::AudioParameterChoice> (
                    mToneStyle.getId(), mToneStyle.getLabel(), mToneStyle.getChoices(), 1),
                std::make_unique<juce::AudioParameterBool> (mLinkInOut.getId(), mLinkInOut.getLabel(), false),
                std::make_unique<juce::AudioParameterBool> (mBypass.getId(), mBypass.getLabel(), false),
                std::make_unique<juce::AudioParameterChoice> (
                    mClippingType.getId(), mClippingType.getLabel(), mClippingType.getChoices(), 0),
                std::make_unique<juce::AudioParameterChoice> (
                    mOversampleRate.getId(), mOversampleRate.getLabel(), mOversampleRate.getChoices(), 0),
                std::make_unique<juce::AudioParameterFloat> (
                    mDryWet.getId(), mDryWet.getLabel(), mDryWet.getRange(), 1.0f),
                std::make_unique<juce::AudioParameterFloat> (
                    mAttack.getId(), mAttack.getLabel(), mAttack.getRange(), 1.0f),
                std::make_unique<juce::AudioParameterFloat> (
                    mHold.getId(), mHold.getLabel(), mHold.getRange(), 0.0f),
                std::make_unique<juce::AudioParameterFloat> (
                    mRelease.getId(), mRelease.getLabel(), mRelease.getRange(), 80.0f),
                std::make_unique<juce::AudioParameterFloat> (
                    mTransientRecovery.getId(), mTransientRecovery.getLabel(), mTransientRecovery.getRange(), 0.0f),
                std::make_unique<juce::AudioParameterFloat> (
                    mLookahead.getId(), mLookahead.getLabel(), mLookahead.getRange(), 0.0f),
                std::make_unique<juce::AudioParameterFloat> (
                    mDetectorHp.getId(), mDetectorHp.getLabel(), mDetectorHp.getRange(), 0.0f),
                std::make_unique<juce::AudioParameterFloat> (
                    mSaturation.getId(), mSaturation.getLabel(), mSaturation.getRange(), 0.0f),
                std::make_unique<juce::AudioParameterBool> (
                    mTruePeakLimit.getId(), mTruePeakLimit.getLabel(), false),
                std::make_unique<juce::AudioParameterFloat> (
                    mAdaptiveRelease.getId(), mAdaptiveRelease.getLabel(), mAdaptiveRelease.getRange(), 0.65f),
                std::make_unique<juce::AudioParameterFloat> (
                    mStereoLink.getId(), mStereoLink.getLabel(), mStereoLink.getRange(), 1.0f),
                std::make_unique<juce::AudioParameterFloat> (
                    mLowProtect.getId(), mLowProtect.getLabel(), mLowProtect.getRange(), 0.5f),
                std::make_unique<juce::AudioParameterFloat> (
                    mTruePeakMargin.getId(), mTruePeakMargin.getLabel(), mTruePeakMargin.getRange(), 0.2f),
                std::make_unique<juce::AudioParameterFloat> (
                    mGrLimit.getId(), mGrLimit.getLabel(), mGrLimit.getRange(), 12.0f),
                std::make_unique<juce::AudioParameterFloat> (
                    mPunchProtect.getId(), mPunchProtect.getLabel(), mPunchProtect.getRange(), 0.25f),
                std::make_unique<juce::AudioParameterFloat> (
                    mReleaseShape.getId(), mReleaseShape.getLabel(), mReleaseShape.getRange(), 0.5f),
                std::make_unique<juce::AudioParameterChoice> (
                    mLimiterStyle.getId(), mLimiterStyle.getLabel(), mLimiterStyle.getChoices(), 0),
                std::make_unique<juce::AudioParameterFloat> (
                    mHfGuard.getId(), mHfGuard.getLabel(), mHfGuard.getRange(), 0.0f),
                std::make_unique<juce::AudioParameterFloat> (
                    mDetectorTilt.getId(), mDetectorTilt.getLabel(), mDetectorTilt.getRange(), 0.0f),
                std::make_unique<juce::AudioParameterBool> (
                    mDeltaListen.getId(), mDeltaListen.getLabel(), false),
                std::make_unique<juce::AudioParameterFloat> (
                    mFinalClip.getId(), mFinalClip.getLabel(), mFinalClip.getRange(), 0.0f),
                std::make_unique<juce::AudioParameterChoice> (
                    mSaturationMode.getId(), mSaturationMode.getLabel(), mSaturationMode.getChoices(), 0),
                std::make_unique<juce::AudioParameterFloat> (
                    mSaturationTone.getId(), mSaturationTone.getLabel(), mSaturationTone.getRange(), 0.0f),
                std::make_unique<juce::AudioParameterFloat> (
                    mSaturationDensity.getId(), mSaturationDensity.getLabel(), mSaturationDensity.getRange(), 0.35f),
                std::make_unique<juce::AudioParameterFloat> (
                    mBassSafe.getId(), mBassSafe.getLabel(), mBassSafe.getRange(), 0.35f),
                std::make_unique<juce::AudioParameterBool> (
                    mGainMatch.getId(), mGainMatch.getLabel(), false),
                std::make_unique<juce::AudioParameterChoice> (
                    mDeltaSource.getId(), mDeltaSource.getLabel(), mDeltaSource.getChoices(), 0),
                std::make_unique<juce::AudioParameterFloat> (
                    mMeterTargetLufs.getId(), mMeterTargetLufs.getLabel(), mMeterTargetLufs.getRange(), -14.0f),
                std::make_unique<juce::AudioParameterFloat> (
                    mBassRecover.getId(), mBassRecover.getLabel(), mBassRecover.getRange(), 0.0f),
            };
        }

        RangedParameter<float>& getInputGain() { return mInputGain; }

        RangedParameter<float>& getOutputGain() { return mOutputGain; }

        RangedParameter<float>& getThreshold() { return mThreshold; }

        RangedParameter<float>& getCeiling() { return mCeiling; }

        RangedParameter<float>& getTone() { return mTone; }

        ChoicingParameter& getToneStyle() { return mToneStyle; }

        Parameter& getBypass() { return mBypass; }

        Parameter& getLinkInOut() { return mLinkInOut; }

        ChoicingParameter& getClippingType() { return mClippingType; }

        ChoicingParameter& getOversampleRate() { return mOversampleRate; }

        RangedParameter<float>& getDryWet() { return mDryWet; }

        RangedParameter<float>& getAttack() { return mAttack; }

        RangedParameter<float>& getHold() { return mHold; }

        RangedParameter<float>& getRelease() { return mRelease; }

        RangedParameter<float>& getTransientRecovery() { return mTransientRecovery; }

        RangedParameter<float>& getLookahead() { return mLookahead; }

        RangedParameter<float>& getDetectorHp() { return mDetectorHp; }

        RangedParameter<float>& getSaturation() { return mSaturation; }

        Parameter& getTruePeakLimit() { return mTruePeakLimit; }

        RangedParameter<float>& getAdaptiveRelease() { return mAdaptiveRelease; }

        RangedParameter<float>& getStereoLink() { return mStereoLink; }

        RangedParameter<float>& getLowProtect() { return mLowProtect; }

        RangedParameter<float>& getTruePeakMargin() { return mTruePeakMargin; }

        RangedParameter<float>& getGrLimit() { return mGrLimit; }

        RangedParameter<float>& getPunchProtect() { return mPunchProtect; }

        RangedParameter<float>& getReleaseShape() { return mReleaseShape; }

        ChoicingParameter& getLimiterStyle() { return mLimiterStyle; }

        RangedParameter<float>& getHfGuard() { return mHfGuard; }

        RangedParameter<float>& getDetectorTilt() { return mDetectorTilt; }

        Parameter& getDeltaListen() { return mDeltaListen; }

        RangedParameter<float>& getFinalClip() { return mFinalClip; }

        ChoicingParameter& getSaturationMode() { return mSaturationMode; }

        RangedParameter<float>& getSaturationTone() { return mSaturationTone; }

        RangedParameter<float>& getSaturationDensity() { return mSaturationDensity; }

        RangedParameter<float>& getBassSafe() { return mBassSafe; }

        Parameter& getGainMatch() { return mGainMatch; }

        ChoicingParameter& getDeltaSource() { return mDeltaSource; }

        RangedParameter<float>& getMeterTargetLufs() { return mMeterTargetLufs; }

        RangedParameter<float>& getBassRecover() { return mBassRecover; }

    private:
        ParametersProvider()
            : mInputGain (1, "InputGain", "INPUT", { -48.0f, 48.0f, 0.1f, 0.5f, true }),
              mOutputGain (2, "OutputGain", "OUTPUT", { -48.0f, 48.0f, 0.1f, 0.5f, true }),
#if defined(PEAKEATER_SPECTRAL_3_VARIANT)
              mThreshold (3, "Threshold", "DRIVE", { -24.0f, 0.0f, 0.1f, 1.4f, false }),
#else
              mThreshold (3, "Threshold", "DRIVE", { -48.0f, 0.0f, 0.1f, 1.4f, false }),
#endif
              mCeiling (4, "Ceiling", "CEILING", { -36.0f, 0.0f, 0.1f, 1.9f, false }),
              mTone (5, "Tone", "TONE", { -1.0f, 1.0f, 0.001f }),
              mToneStyle (16, "ToneStyle", "MODE", { "WARM", "CLEAN", "BRIGHT" }),
              mBypass (6, "Bypass", "BYPASS"),
              mLinkInOut (7, "LinkInOut", "INOUT"),
              mClippingType (8,
                             "ClippingType",
                             "ALGORITHM",
                             { "EDM", "HIP HOP", "DRUMS", "ONE SHOT", "ACOUSTIC", "VOCAL", "BASS", "BRIGHT", "GLUE", "CLEAN",
                               "PERCS", "DUBSTEP", "DRUMNBASS", "HOUSE", "TRAP", "808&KICK", "ONE SHOT CLEAN" }),
              mOversampleRate (9, "OversampleRate", "QUALITY", { "ECO", "LIVE", "HIGH", "MASTER", "ULTRA", "MAX" }),
              mDryWet (10, "DryWet", "DRYWET", { 0.0f, 1.0f, 0.001f }),
              mAttack (11, "Attack", "ATTACK", { 0.01f, 1000.0f, 0.01f, 0.35f, false }),
              mHold (12, "Hold", "HOLD", { 0.0f, 500.0f, 0.1f, 0.45f, false }),
              mRelease (13, "Release", "RELEASE", { 1.0f, 2000.0f, 0.1f, 0.35f, false }),
              mTransientRecovery (14, "TransientRecovery", "TRANSIENT", { 0.0f, 1.0f, 0.001f }),
              mLookahead (15, "Lookahead", "LOOKAHEAD", { 0.0f, 50.0f, 0.01f, 0.6f, false }),
              mDetectorHp (17, "DetectorHP", "DET HP", { 0.0f, 250.0f, 1.0f, 0.45f, false }),
              mSaturation (18, "Saturation", "SAT", { 0.0f, 1.0f, 0.001f }),
              mTruePeakLimit (19, "TruePeakLimit", "TRUE PEAK"),
              mAdaptiveRelease (20, "AdaptiveRelease", "ADAPT REL", { 0.0f, 1.0f, 0.001f }),
              mStereoLink (21, "StereoLink", "ST LINK", { 0.0f, 1.0f, 0.001f }),
              mLowProtect (22, "LowProtect", "LOW PROTECT", { 0.0f, 1.0f, 0.001f }),
              mTruePeakMargin (23, "TruePeakMargin", "TP MARGIN", { 0.0f, 1.0f, 0.01f }),
              mGrLimit (24, "GRLimit", "GR LIMIT", { 0.0f, 12.0f, 0.1f }),
              mPunchProtect (25, "PunchProtect", "PUNCH", { 0.0f, 1.0f, 0.001f }),
              mReleaseShape (26, "ReleaseShape", "REL SHAPE", { 0.0f, 1.0f, 0.001f }),
              mLimiterStyle (27, "LimiterStyle", "STYLE", { "TRANSPARENT", "PUNCH", "GLUE", "SAFE", "LOUD" }),
              mHfGuard (28, "HFGuard", "HF GUARD", { 0.0f, 1.0f, 0.001f }),
              mDetectorTilt (29, "DetectorTilt", "DET TILT", { -1.0f, 1.0f, 0.001f }),
              mDeltaListen (30, "DeltaListen", "DELTA"),
              mFinalClip (31, "FinalClip", "FINAL CLIP", { 0.0f, 1.0f, 0.001f }),
              mSaturationMode (32, "SaturationMode", "SAT MODE", { "CLEAN", "WARM", "TUBE", "TAPE", "XFMR" }),
              mSaturationTone (33, "SaturationTone", "SAT TONE", { -1.0f, 1.0f, 0.001f }),
              mSaturationDensity (34, "SaturationDensity", "DENSITY", { 0.0f, 1.0f, 0.001f }),
              mBassSafe (35, "BassSafe", "BASS SAFE", { 0.0f, 1.0f, 0.001f }),
              mGainMatch (36, "GainMatch", "GAIN MATCH"),
              mDeltaSource (37, "DeltaSource", "DELTA SRC", { "ALL", "LIMITER", "SPECTRAL", "SAT+CLIP", "CEILING" }),
              mMeterTargetLufs (38, "MeterTargetLufs", "TARGET", { -24.0f, -6.0f, 0.1f }),
              mBassRecover (39, "BassRecover", "BASS REC", { 0.0f, 1.0f, 0.001f })
        {
        }
        ~ParametersProvider() {}

        RangedParameter<float> mInputGain;
        RangedParameter<float> mOutputGain;
        RangedParameter<float> mThreshold;
        RangedParameter<float> mCeiling;
        RangedParameter<float> mTone;
        ChoicingParameter mToneStyle;
        Parameter mBypass;
        Parameter mLinkInOut;
        ChoicingParameter mClippingType;
        ChoicingParameter mOversampleRate;
        RangedParameter<float> mDryWet;
        RangedParameter<float> mAttack;
        RangedParameter<float> mHold;
        RangedParameter<float> mRelease;
        RangedParameter<float> mTransientRecovery;
        RangedParameter<float> mLookahead;
        RangedParameter<float> mDetectorHp;
        RangedParameter<float> mSaturation;
        Parameter mTruePeakLimit;
        RangedParameter<float> mAdaptiveRelease;
        RangedParameter<float> mStereoLink;
        RangedParameter<float> mLowProtect;
        RangedParameter<float> mTruePeakMargin;
        RangedParameter<float> mGrLimit;
        RangedParameter<float> mPunchProtect;
        RangedParameter<float> mReleaseShape;
        ChoicingParameter mLimiterStyle;
        RangedParameter<float> mHfGuard;
        RangedParameter<float> mDetectorTilt;
        Parameter mDeltaListen;
        RangedParameter<float> mFinalClip;
        ChoicingParameter mSaturationMode;
        RangedParameter<float> mSaturationTone;
        RangedParameter<float> mSaturationDensity;
        RangedParameter<float> mBassSafe;
        Parameter mGainMatch;
        ChoicingParameter mDeltaSource;
        RangedParameter<float> mMeterTargetLufs;
        RangedParameter<float> mBassRecover;
    };
} // namespace params
} // namespace pe

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "ControlPanel.h"
#include "LevelMetersPack.h"
#include "LinkingPanel.h"
#include "Ticks.h"
#include "analyser/AnalyserComponent.h"
#include "clipmeter/ClipMeter.h"
#include "scaling/ScalingSwitch.h"

namespace pe
{
namespace gui
{
    class AdvancedSettingsPanel : public juce::Component, private juce::Timer
    {
    public:
        AdvancedSettingsPanel (std::shared_ptr<juce::AudioProcessorValueTreeState> parameters,
                               LevelMetersPack const& levelMetersPack);
        ~AdvancedSettingsPanel() override;

        void paint (juce::Graphics& g) override;
        void resized() override;
        void mouseDown (juce::MouseEvent const& event) override;

    private:
        class AdvancedComboLookAndFeel : public juce::LookAndFeel_V4
        {
        public:
            juce::Font getComboBoxFont (juce::ComboBox& box) override;
            juce::Font getPopupMenuFont() override;
            void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override;
        };

        enum class Tab
        {
            Limiter,
            ToneSat,
            Metering
        };

        juce::TextButton mLimiterTab;
        juce::TextButton mToneSatTab;
        juce::TextButton mMeteringTab;
        AdvancedComboLookAndFeel mAdvancedComboLookAndFeel;
        juce::Label mLimiterStyleLabel;
        juce::ComboBox mLimiterStyle;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mLimiterStyleAttachment;
        juce::ToggleButton mTruePeakLimit;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mTruePeakLimitAttachment;
        juce::ToggleButton mDeltaListen;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mDeltaListenAttachment;
        juce::Label mDeltaSourceLabel;
        juce::ComboBox mDeltaSource;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mDeltaSourceAttachment;
        juce::ToggleButton mGainMatch;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mGainMatchAttachment;
        juce::Label mSaturationModeLabel;
        juce::ComboBox mSaturationMode;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mSaturationModeAttachment;
        Dial mGrLimit;
        Dial mPunchProtect;
        Dial mHfGuard;
        Dial mFinalClip;
        Dial mReleaseShape;
        Dial mAdaptiveRelease;
        Dial mDetectorTilt;
        Dial mStereoLink;
        Dial mLowProtect;
        Dial mTruePeakMargin;
        Dial mLookahead;
        Dial mRelease;
        Dial mSaturation;
        Dial mSaturationTone;
        Dial mSaturationDensity;
        Dial mBassSafe;
        Dial mDetectorHp;
        Dial mAttack;
        Dial mHold;
        Dial mTransient;
        Dial mDryWet;
        Dial mMeterTargetLufs;
        std::shared_ptr<processor::LevelMeter<float>> mInputLevelMeter;
        std::shared_ptr<processor::LevelMeter<float>> mOutputLevelMeter;
        std::shared_ptr<processor::DynamicsMeter> mDynamicsMeter;
        Tab mActiveTab = Tab::Limiter;

        juce::Rectangle<int> takeArea (juce::Rectangle<int>& row, int count, int gap) const;
        void configureTabButton (juce::TextButton& button, juce::String const& text);
        void setActiveTab (Tab tab);
        void updateVisibleControls();
        void drawMeterCell (juce::Graphics& g, juce::Rectangle<int> bounds, juce::String const& label, float value,
                            juce::String const& suffix, juce::Colour colour);
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AdvancedSettingsPanel)
    };

    class CentralPanel : public juce::Component
    {
    public:
        CentralPanel (std::shared_ptr<juce::AudioProcessorValueTreeState> parameters,
                      LevelMetersPack const& levelMetersPack,
                      std::shared_ptr<Ticks> ticks,
                      std::function<void (bool)> advancedSettingsChanged,
                      std::function<void (bool)> analyzerFocusChanged);

        void paint (juce::Graphics& g) override;
        void resized() override;

    private:
        enum class ViewMode
        {
            Compact,
            AnalyzerFocus
        };

        ClipMeter mClipMeter;
        ControlPanel mControlPanel;
        LinkingPanel mLinkingPanel;
        AnalyserComponent mAnalyserComponent;
        ScalingSwitch mScalingSwitch;
        AdvancedSettingsPanel mAdvancedSettingsPanel;
        juce::TextButton mAdvancedSettingsButton;
        juce::TextButton mAnalyzerFocusButton;
        std::function<void (bool)> mAdvancedSettingsChanged;
        std::function<void (bool)> mAnalyzerFocusChanged;
        bool mAdvancedSettingsVisible = false;
        ViewMode mViewMode = ViewMode::Compact;

        void setAdvancedSettingsVisible (bool shouldShow);
        void setAnalyzerFocusVisible (bool shouldShow);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CentralPanel)
    };
} // namespace gui
} // namespace pe

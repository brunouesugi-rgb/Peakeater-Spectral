#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "LevelMetersPack.h"
#include "clipmeter/ClipMeter.h"
#include "dial/Dial.h"
#include "dial/DialLookAndFeel.h"
#include "dial/ceilingdial/CeilingDial.h"
#include "dial/gaindial/GainDial.h"

namespace pe::gui {

class DriveDialLookAndFeel : public DialLookAndFeel {
   public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override;
};

class DriveDial : public juce::Component, private juce::Slider::Listener {
   public:
    explicit DriveDial(std::shared_ptr<juce::AudioProcessorValueTreeState> parameters);
    ~DriveDial() override;

    void resized() override;

   private:
    juce::RangedAudioParameter& mParameter;
    juce::ParameterAttachment mAttachment;
    DriveDialLookAndFeel mLookAndFeel;
    juce::Slider mSlider;
    juce::Label mLabel;
    juce::Label mValueLabel;
    bool mUpdatingFromParameter = false;
    bool mDragging = false;

    void setDriveFromThreshold(float thresholdDb);
    void updateValueLabel(float driveDb);
    void sliderValueChanged(juce::Slider* slider) override;
    void sliderDragStarted(juce::Slider* slider) override;
    void sliderDragEnded(juce::Slider* slider) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DriveDial)
};

class ChoiceBlockLookAndFeel : public juce::LookAndFeel_V4 {
   public:
    juce::Font getComboBoxFont(juce::ComboBox& box) override;
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox& box, juce::Label& label) override;
};

class ChoiceBlock : public juce::Component {
   public:
    ChoiceBlock(juce::String label, std::shared_ptr<juce::AudioProcessorValueTreeState> parameters, juce::String parameterId,
                std::initializer_list<juce::String> choices, juce::String footer = {});
    ~ChoiceBlock() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

   private:
    ChoiceBlockLookAndFeel mLookAndFeel;
    juce::Label mLabel;
    juce::ComboBox mComboBox;
    juce::Label mFooter;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChoiceBlock)
};

class DecisionMeterStrip : public juce::Component, private juce::Timer {
   public:
    explicit DecisionMeterStrip(LevelMetersPack const& levelMetersPack);

    void paint(juce::Graphics& g) override;

   private:
    std::shared_ptr<processor::LevelMeter<float>> mInputLevelMeter;
    std::shared_ptr<processor::LevelMeter<float>> mOutputLevelMeter;
    std::shared_ptr<processor::DynamicsMeter> mDynamicsMeter;
    float mHeldTruePeak = -120.0f;
    float mHeldClip = 0.0f;

    void timerCallback() override;
    void drawCell(juce::Graphics& g, juce::Rectangle<int> bounds, juce::String const& label, float value, juce::String const& suffix,
                  juce::Colour colour, bool emphasize = false);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecisionMeterStrip)
};

class ControlPanel : public juce::Component {
   public:
    ControlPanel(std::shared_ptr<juce::AudioProcessorValueTreeState> parameters, LevelMetersPack const& levelMetersPack);

    void setAnalyzerFocusMode(bool shouldUse);
    void paint(juce::Graphics& g) override;
    void resized() override;

   private:
    GainDial inputGain;
    ChoiceBlock algorithmChoice;
    DriveDial drive;
    CeilingDial ceiling;
    Dial tone;
    Dial bassRecover;
    juce::Component toneStylePanel;
    ChoiceBlock toneStyleChoice;
    ChoiceBlock qualityChoice;
    Dial dryWet;
    GainDial outputGain;
    Dial attack;
    Dial hold;
    Dial release;
    Dial transientRecovery;
    Dial lookahead;
    Dial detectorHp;
    Dial saturation;
    DecisionMeterStrip decisionMeters;
    juce::Rectangle<int> loudnessSectionBounds;
    juce::Rectangle<int> toneSectionBounds;
    juce::Rectangle<int> utilitySectionBounds;
    juce::Rectangle<int> detailSectionBounds;
    juce::Rectangle<int> meterSectionBounds;
    bool mAnalyzerFocusMode = false;

    juce::Rectangle<int> takeWeightedArea(juce::Rectangle<int>& row, float weight, float& remainingWeight, int gap) const;
    void drawSection(juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour colour, float alpha) const;
    void drawFocusFrame(juce::Graphics& g, juce::Rectangle<int> bounds, juce::String const& title, juce::Colour colour) const;
    void resizedAnalyzerFocus(juce::Rectangle<int> bounds);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlPanel)
};
}  // namespace pe::gui

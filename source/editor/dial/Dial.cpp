#include "Dial.h"

#include "../Utils.h"

#include <juce_graphics/juce_graphics.h>

namespace pe
{
namespace gui
{
    Dial::Dial (std::string const& labelText,
                std::shared_ptr<juce::AudioProcessorValueTreeState> parameters,
                std::string const& parameterId)
        : mDialValue (parameters, parameterId), mSliderAttachment (*parameters, parameterId, mSlider)
    {
        mSlider.setSliderStyle (juce::Slider::SliderStyle::RotaryVerticalDrag);
        mSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 80, 20);
        mSlider.setLookAndFeel (&mLookAndFeel);
        mSlider.setVelocityModeParameters (1.0, 1, 0.02, true, juce::ModifierKeys::shiftModifier);
        mSlider.setTextBoxIsEditable (false);
        addAndMakeVisible (mSlider);
        addAndMakeVisible (mDialValue);
        mLabel.setFont (11.0f);
        mLabel.setText (juce::String (labelText).toUpperCase(), juce::dontSendNotification);
        mLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (mLabel);
        updateFontSize();
    }

    Dial::~Dial()
    {
        mSlider.setLookAndFeel (nullptr);
        mLabel.setLookAndFeel (nullptr);
        setLookAndFeel (nullptr);
        mSlider.removeMouseListener (this);
    }

    void Dial::resized()
    {
        juce::Grid grid;
        using Track = juce::Grid::TrackInfo;
        using Fr = juce::Grid::Fr;
        using Item = juce::GridItem;
        auto const multilineLabel = mLabel.getText().containsChar ('\n');
        if (multilineLabel)
            grid.templateRows = { Track (Fr (3)), Track (Fr (5)), Track (Fr (1)) };
        else
            grid.templateRows = { Track (Fr (2)), Track (Fr (5)), Track (Fr (1)) };
        grid.templateColumns = { Track (Fr (1)) };
        grid.items = { Item (mLabel), Item (mSlider), Item (mDialValue) };
        grid.performLayout (getLocalBounds());
        updateFontSize();
    }

    void Dial::updateFontSize()
    {
        auto const topLevel = getTopLevelComponent();
        if (topLevel == nullptr)
        {
            mLabel.setFont (mLabel.getText().containsChar ('\n') ? 8.0f : 11.0f);
            return;
        }

        auto const topLevelBounds = topLevel->getBounds();
        auto const baseSize = static_cast<float> (calculatePrimaryTextSize (topLevelBounds.getWidth(), topLevelBounds.getHeight()));
        auto const multilineLabel = mLabel.getText().containsChar ('\n');
        auto const heightScale = multilineLabel ? 0.115f : 0.18f;
        auto const maxSize = multilineLabel ? 9.0f : 14.0f;
        auto const componentLimitedSize = getHeight() > 0 ? static_cast<float> (getHeight()) * heightScale : baseSize;
        mLabel.setFont (juce::jlimit (7.0f, maxSize, juce::jmin (baseSize, componentLimitedSize)));
        mLabel.setMinimumHorizontalScale (0.72f);
    }
} // namespace gui
} // namespace pe

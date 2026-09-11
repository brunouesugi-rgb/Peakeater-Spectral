#include "ScalingSwitch.h"

#include "../ColourScheme.h"
#include "../Utils.h"

namespace pe
{
namespace gui
{
    ScalingSwitch::ScalingSwitch (std::shared_ptr<Ticks> ticks)
        : mTicks (ticks)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    ScalingSwitch::~ScalingSwitch()
    {
        setLookAndFeel (nullptr);
    }

    juce::String ScalingSwitch::getTooltip()
    {
        return "Scaling";
    }

    void ScalingSwitch::paint (juce::Graphics& g)
    {
        auto const bounds = getLocalBounds().toFloat().reduced (1.0f);
        auto const borderRadius = juce::jmin (8.0f, bounds.getHeight() * 0.35f);
        auto const borderThickness = 1.0f;
        auto const activeText = mTicks->isLinear() ? "1x" : "2x";
        auto const inactiveText = mTicks->isLinear() ? "2x" : "1x";
        juce::Colour color = colourscheme::BackgroundTertiary.withAlpha (0.78f);
        if (isMouseOver())
        {
            color = color.withAlpha (1.0f);
        }

        g.setColour (colourscheme::BackgroundPrimary.withAlpha (0.72f));
        g.fillRoundedRectangle (bounds, borderRadius);
        g.setColour (color);
        g.drawRoundedRectangle (bounds, borderRadius, borderThickness);

        auto inner = bounds.reduced (3.0f, 3.0f);
        auto activeBounds = inner.removeFromLeft (inner.getWidth() * 0.5f);
        auto inactiveBounds = inner;
        g.setColour (colourscheme::ForegroundTertiary.withAlpha (0.22f));
        g.fillRoundedRectangle (activeBounds, juce::jmax (4.0f, borderRadius - 2.0f));

        g.setFont (juce::Font (juce::jlimit (8.0f, 12.0f, bounds.getHeight() * 0.36f), juce::Font::bold));
        g.setColour (color);
        g.drawText (activeText, activeBounds.toNearestInt(), juce::Justification::centred, true);
        g.setColour (colourscheme::TextFocusLevel3);
        g.drawText (inactiveText, inactiveBounds.toNearestInt(), juce::Justification::centred, true);
    }

    void ScalingSwitch::mouseDown (juce::MouseEvent const&)
    {
        mTicks->setIsLinear (! mTicks->isLinear());
    }

    void ScalingSwitch::mouseEnter (juce::MouseEvent const&)
    {
        if (isEnabled())
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        else
        {
            setMouseCursor (juce::MouseCursor::NormalCursor);
        }
    }

    void ScalingSwitch::mouseExit (juce::MouseEvent const&)
    {
        if (isEnabled())
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        else
        {
            setMouseCursor (juce::MouseCursor::NormalCursor);
        }
    }
} // namespace gui
} // namespace pe

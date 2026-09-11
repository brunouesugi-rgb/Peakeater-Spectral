#include "DynamicsStatsComponent.h"

#include "../ColourScheme.h"
#include "../Utils.h"

namespace pe::gui {

DynamicsStatsComponent::DynamicsStatsComponent(LevelMetersPack const& levelMetersPack)
    : juce::Component(),
      mTimer(std::bind(&DynamicsStatsComponent::onTimerTick, this)),
      mOutputLevelMeter(levelMetersPack.outputLevelMeter),
      mDynamicsMeter(levelMetersPack.dynamicsMeter) {
    mTimer.startTimerHz(12);
}

void DynamicsStatsComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().reduced(6, 4);
    auto const rowHeight = juce::jmax(9, bounds.getHeight() / 7);

    drawStat(g, bounds.removeFromTop(rowHeight), "PK", mOutputLevelMeter->getPeakDBFS().load(), "dB");
    drawStat(g, bounds.removeFromTop(rowHeight), "TP", mDynamicsMeter->getTruePeakDb(), "dB");
    drawStat(g, bounds.removeFromTop(rowHeight), "LUFS-S", mDynamicsMeter->getShortTermLufs(), "");
    drawStat(g, bounds.removeFromTop(rowHeight), "RMS", mOutputLevelMeter->getRmsDBFS().load(), "dB");
    drawStat(g, bounds.removeFromTop(rowHeight), "CRST", mOutputLevelMeter->getCrestFactorDB().load(), "dB");
    drawStat(g, bounds.removeFromTop(rowHeight), "CLIP", mDynamicsMeter->getClipAmountDb(), "dB");
    drawStat(g, bounds.removeFromTop(rowHeight), "GR", mDynamicsMeter->getGainReductionDb(), "dB");
}

void DynamicsStatsComponent::drawStat(juce::Graphics& g, juce::Rectangle<int> bounds, juce::String const& label, float value,
                                      juce::String const& suffix) {
    auto const fontSize = juce::jlimit(8.0f, 13.0f, static_cast<float>(bounds.getHeight()) * 0.62f);
    auto labelBounds = bounds.removeFromLeft(juce::jmax(30, getWidth() / 3));

    g.setFont(juce::Font(fontSize, juce::Font::bold));
    g.setColour(colourscheme::TextFocusLevel2);
    g.drawText(label, labelBounds, juce::Justification::centredLeft, true);

    g.setFont(juce::Font(fontSize, juce::Font::plain));
    g.setColour(value > 0.05f ? colourscheme::ForegroundPrimary : colourscheme::TextFocusLevel1);
    auto const text = value <= -119.0f ? juce::String("-inf") : juce::String(value, 1) + suffix;
    g.drawText(text, bounds, juce::Justification::centredRight, true);
}

void DynamicsStatsComponent::onTimerTick() {
    repaint();
}

}  // namespace pe::gui

#include "WorkingPanel.h"

namespace pe::gui
{
WorkingPanel::WorkingPanel (std::shared_ptr<juce::AudioProcessorValueTreeState> parameters,
                            LevelMetersPack const& levelMetersPack,
                            std::function<void (bool)> advancedSettingsChanged,
                            std::function<void (bool)> analyzerFocusChanged)
    : juce::Component(), mTicks (std::make_shared<Ticks>()), mLeftPanel (levelMetersPack, mTicks),
      mCentralPanel (parameters, levelMetersPack, mTicks, std::move (advancedSettingsChanged), std::move (analyzerFocusChanged)),
      mRightPanel (levelMetersPack, mTicks)
{
    addAndMakeVisible (mLeftPanel);
    addAndMakeVisible (mCentralPanel);
    addAndMakeVisible (mRightPanel);
}

void WorkingPanel::resized()
{
    juce::Grid grid;
    using Track = juce::Grid::TrackInfo;
    using Fr = juce::Grid::Fr;
    using Item = juce::GridItem;
    grid.templateRows = { Track (Fr (1)) };
    grid.templateColumns = { Track (Fr (1)), Track (Fr (14)), Track (Fr (1)) };
    grid.items = { Item (mLeftPanel), Item (mCentralPanel), Item (mRightPanel) };
    grid.performLayout (getLocalBounds());
}
} // namespace pe::gui

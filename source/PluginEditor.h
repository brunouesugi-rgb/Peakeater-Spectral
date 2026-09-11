#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "editor/LevelMetersPack.h"
#include "editor/MainComponent.h"

namespace pe {
class PeakEaterAudioProcessorEditor : public juce::AudioProcessorEditor {
   public:
    PeakEaterAudioProcessorEditor(processor::PeakEaterAudioProcessor& audioProcessor,
                                  std::shared_ptr<juce::AudioProcessorValueTreeState> parameters,
                                  gui::LevelMetersPack const&& levelMetersPack);
    ~PeakEaterAudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

   private:
    void setAdvancedSettingsExpanded(bool shouldExpand);
    void setAnalyzerFocusExpanded(bool shouldExpand);
    void updateEditorSize();

    pe::gui::MainComponent mMainComponent;
    processor::PeakEaterAudioProcessor& mAudioProcessor;
    bool mAdvancedSettingsExpanded = false;
    bool mAnalyzerFocusExpanded = false;
    bool mRestoreCompactSizeAfterAnalyzer = false;
    int mCompactWidthBeforeAnalyzer = 0;
    int mCompactHeightBeforeAnalyzer = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PeakEaterAudioProcessorEditor)
};
}  // namespace pe

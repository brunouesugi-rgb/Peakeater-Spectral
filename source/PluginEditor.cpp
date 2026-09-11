#include "PluginEditor.h"

namespace pe {
//==============================================================================
PeakEaterAudioProcessorEditor::PeakEaterAudioProcessorEditor(processor::PeakEaterAudioProcessor& audioProcessor,
                                                             std::shared_ptr<juce::AudioProcessorValueTreeState> parameters,
                                                             gui::LevelMetersPack const&& levelMetersPack)
    : juce::AudioProcessorEditor(audioProcessor),
      mMainComponent(parameters,
                     levelMetersPack,
                     [this](bool shouldExpand) { setAdvancedSettingsExpanded(shouldExpand); },
                     [this](bool shouldExpand) { setAnalyzerFocusExpanded(shouldExpand); }),
      mAudioProcessor(audioProcessor) {
    auto const size = mAudioProcessor.getPluginSizeState();
    auto const constraints = mAudioProcessor.getPluginSizeConstraints();

    addAndMakeVisible(mMainComponent);

    setSize(juce::jmax(size.width, constraints.minWidth), juce::jmax(size.height, constraints.minHeight));
    setResizable(true, true);

    setResizeLimits(constraints.minWidth, constraints.minHeight, constraints.maxWidth, constraints.maxHeight);
    mAudioProcessor.setEditorActive(true);
}

PeakEaterAudioProcessorEditor::~PeakEaterAudioProcessorEditor() {
    mAudioProcessor.setEditorActive(false);
    // Call to destructor indicates plugin window has been closed
    // We are saving it's state to keep resized with/height
    mAudioProcessor.setPluginSizeState({getWidth(), getHeight()});
}

//==============================================================================
void PeakEaterAudioProcessorEditor::paint(juce::Graphics& g) { g.fillAll(); }

void PeakEaterAudioProcessorEditor::resized() { mMainComponent.setBounds(getLocalBounds()); }

void PeakEaterAudioProcessorEditor::setAdvancedSettingsExpanded(bool shouldExpand) {
    mAdvancedSettingsExpanded = shouldExpand;
    if (shouldExpand) {
        mAnalyzerFocusExpanded = false;
    }
    updateEditorSize();
}

void PeakEaterAudioProcessorEditor::setAnalyzerFocusExpanded(bool shouldExpand) {
    if (shouldExpand && !mAnalyzerFocusExpanded) {
        mCompactWidthBeforeAnalyzer = getWidth();
        mCompactHeightBeforeAnalyzer = getHeight();
    } else if (!shouldExpand && mAnalyzerFocusExpanded) {
        mRestoreCompactSizeAfterAnalyzer = true;
    }

    mAnalyzerFocusExpanded = shouldExpand;
    if (shouldExpand) {
        mAdvancedSettingsExpanded = false;
    }
    updateEditorSize();
}

void PeakEaterAudioProcessorEditor::updateEditorSize() {
    auto const constraints = mAudioProcessor.getPluginSizeConstraints();
    auto targetWidth = constraints.minWidth;
    auto targetHeight = constraints.minHeight;

    if (mAnalyzerFocusExpanded) {
        targetWidth = juce::jmax(getWidth(), 1180);
        targetHeight = 940;
    } else if (mRestoreCompactSizeAfterAnalyzer) {
        targetWidth = juce::jmax(mCompactWidthBeforeAnalyzer, constraints.minWidth);
        targetHeight = juce::jmax(mCompactHeightBeforeAnalyzer, constraints.minHeight);
        mRestoreCompactSizeAfterAnalyzer = false;
    } else {
        targetWidth = juce::jmax(getWidth(), constraints.minWidth);
        targetHeight = mAdvancedSettingsExpanded ? 760 : constraints.minHeight;
    }

    setSize(targetWidth, targetHeight);
}
}  // namespace pe

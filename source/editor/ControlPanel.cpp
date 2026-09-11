#include "ControlPanel.h"

#include "ColourScheme.h"
#include "Utils.h"

#include <cmath>

namespace pe::gui {
namespace {
int constexpr gBorderWith = 1;
int constexpr gBorderRadius = 8;
int constexpr gInnerGap = 6;

juce::String signedDb(float value) {
    auto const prefix = value >= 0.0f ? "+" : "";
    return prefix + juce::String(value, 1) + " dB";
}
}  // namespace

void DriveDialLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                            float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider) {
    DialLookAndFeel::drawRotarySlider(g, x, y, width, height, sliderPos, rotaryStartAngle, rotaryEndAngle, slider);

    auto const bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width),
                                              static_cast<float>(height))
                            .reduced(2.0f);
    auto const radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto const lineW = radius * 0.115f;
    auto const arcRadius = radius - lineW * 1.6f;
    auto const toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    juce::Path driveArc;
    driveArc.addCentredArc(bounds.getCentreX(), bounds.getCentreY(), arcRadius, arcRadius, 0.0f, rotaryStartAngle, toAngle, true);
    g.setColour(colourscheme::ForegroundSecondary.brighter(0.18f));
    g.strokePath(driveArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    DialLookAndFeel::drawStick(g, x, y, width, height, sliderPos, rotaryStartAngle, rotaryEndAngle, slider);
}

DriveDial::DriveDial(std::shared_ptr<juce::AudioProcessorValueTreeState> parameters)
    : mParameter(*parameters->getParameter("Threshold")),
      mAttachment(mParameter, [this](float thresholdDb) { setDriveFromThreshold(thresholdDb); }, nullptr) {
    mSlider.setSliderStyle(juce::Slider::SliderStyle::RotaryVerticalDrag);
    mSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 80, 20);
    mSlider.setRange(0.0, -mParameter.getNormalisableRange().start, 0.1);
    mSlider.setLookAndFeel(&mLookAndFeel);
    mSlider.setVelocityModeParameters(1.0, 1, 0.02, true, juce::ModifierKeys::shiftModifier);
    mSlider.addListener(this);
    addAndMakeVisible(mSlider);

    mLabel.setText("DRIVE", juce::dontSendNotification);
    mLabel.setJustificationType(juce::Justification::centred);
    mLabel.setColour(juce::Label::textColourId, colourscheme::TextFocusLevel0);
    addAndMakeVisible(mLabel);

    mValueLabel.setJustificationType(juce::Justification::centred);
    mValueLabel.setColour(juce::Label::textColourId, colourscheme::ForegroundSecondary.brighter(0.2f));
    addAndMakeVisible(mValueLabel);

    mAttachment.sendInitialUpdate();
}

DriveDial::~DriveDial() {
    mSlider.removeListener(this);
    mSlider.setLookAndFeel(nullptr);
}

void DriveDial::resized() {
    juce::Grid grid;
    using Track = juce::Grid::TrackInfo;
    using Fr = juce::Grid::Fr;
    using Item = juce::GridItem;
    grid.templateRows = {Track(Fr(2)), Track(Fr(5)), Track(Fr(1))};
    grid.templateColumns = {Track(Fr(1))};
    grid.items = {Item(mLabel), Item(mSlider), Item(mValueLabel)};
    grid.performLayout(getLocalBounds());

    auto const topLevel = getTopLevelComponent();
    auto const baseSize = topLevel != nullptr ? calculatePrimaryTextSize(topLevel->getWidth(), topLevel->getHeight()) : 11;
    auto const labelSize = juce::jlimit(9.0f, 15.0f, juce::jmin(static_cast<float>(baseSize), static_cast<float>(getHeight()) * 0.16f));
    auto const valueSize = juce::jlimit(8.0f, 13.0f, juce::jmin(static_cast<float>(baseSize), static_cast<float>(getHeight()) * 0.14f));
    mLabel.setFont(juce::Font(labelSize, juce::Font::bold));
    mValueLabel.setFont(juce::Font(valueSize, juce::Font::bold));
}

void DriveDial::setDriveFromThreshold(float thresholdDb) {
    auto const driveDb = juce::jlimit(0.0f, -mParameter.getNormalisableRange().start, -thresholdDb);
    mUpdatingFromParameter = true;
    mSlider.setValue(driveDb, juce::dontSendNotification);
    mUpdatingFromParameter = false;
    updateValueLabel(driveDb);
}

void DriveDial::updateValueLabel(float driveDb) {
    mValueLabel.setText(signedDb(driveDb), juce::dontSendNotification);
}

void DriveDial::sliderValueChanged(juce::Slider* slider) {
    if (slider != &mSlider || mUpdatingFromParameter) {
        return;
    }

    auto const driveDb = static_cast<float>(mSlider.getValue());
    auto const thresholdDb = -driveDb;
    if (mDragging) {
        mAttachment.setValueAsPartOfGesture(thresholdDb);
    } else {
        mAttachment.setValueAsCompleteGesture(thresholdDb);
    }
    updateValueLabel(driveDb);
}

void DriveDial::sliderDragStarted(juce::Slider* slider) {
    if (slider == &mSlider) {
        mDragging = true;
        mAttachment.beginGesture();
    }
}

void DriveDial::sliderDragEnded(juce::Slider* slider) {
    if (slider == &mSlider) {
        mDragging = false;
        mAttachment.endGesture();
    }
}

juce::Font ChoiceBlockLookAndFeel::getComboBoxFont(juce::ComboBox& box) {
    auto const size = juce::jlimit(9.0f, 11.0f, static_cast<float>(box.getHeight()) * 0.34f);
    return juce::Font(size, juce::Font::bold);
}

void ChoiceBlockLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    auto const arrowWidth = juce::jmin(18, box.getHeight());
    label.setBounds(4, 1, juce::jmax(1, box.getWidth() - arrowWidth - 5), box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centredLeft);
}

juce::PopupMenu::Options ChoiceBlockLookAndFeel::getOptionsForComboBoxPopupMenu(juce::ComboBox& box, juce::Label& label) {
    auto const compactItemHeight = juce::jlimit(24, 28, juce::roundToInt(static_cast<float>(label.getHeight()) * 0.62f));
    auto const compactWidth = juce::jmax(box.getWidth(), 196);
    return juce::PopupMenu::Options()
        .withTargetComponent(&box)
        .withItemThatMustBeVisible(box.getSelectedId())
        .withInitiallySelectedItem(box.getSelectedId())
        .withMinimumWidth(compactWidth)
        .withMaximumNumColumns(2)
        .withStandardItemHeight(compactItemHeight);
}

ChoiceBlock::ChoiceBlock(juce::String label, std::shared_ptr<juce::AudioProcessorValueTreeState> parameters, juce::String parameterId,
                         std::initializer_list<juce::String> choices, juce::String footer) {
    mLabel.setText(label.toUpperCase(), juce::dontSendNotification);
    mLabel.setJustificationType(juce::Justification::centred);
    mLabel.setColour(juce::Label::textColourId, colourscheme::TextFocusLevel1);
    addAndMakeVisible(mLabel);

    int itemId = 1;
    for (auto const& choice : choices) {
        mComboBox.addItem(choice, itemId++);
    }
    mComboBox.setJustificationType(juce::Justification::centred);
    mComboBox.setColour(juce::ComboBox::backgroundColourId, colourscheme::BackgroundPrimary.withAlpha(0.75f));
    mComboBox.setColour(juce::ComboBox::outlineColourId, colourscheme::BackgroundTertiary.withAlpha(0.45f));
    mComboBox.setColour(juce::ComboBox::textColourId, colourscheme::TextFocusLevel0);
    mComboBox.setColour(juce::ComboBox::arrowColourId, colourscheme::ForegroundTertiary);
    mComboBox.setWantsKeyboardFocus(false);
    mComboBox.setLookAndFeel(&mLookAndFeel);
    addAndMakeVisible(mComboBox);

    mFooter.setText(footer.toUpperCase(), juce::dontSendNotification);
    mFooter.setJustificationType(juce::Justification::centred);
    mFooter.setColour(juce::Label::textColourId, colourscheme::TextFocusLevel3);
    addAndMakeVisible(mFooter);

    mAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(*parameters, parameterId, mComboBox);
}

ChoiceBlock::~ChoiceBlock() {
    mComboBox.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void ChoiceBlock::paint(juce::Graphics& g) {
    auto const bounds = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(colourscheme::BackgroundPrimary.withAlpha(0.36f));
    g.fillRoundedRectangle(bounds, 7.0f);
    g.setColour(colourscheme::BackgroundTertiary.withAlpha(0.24f));
    g.drawRoundedRectangle(bounds, 7.0f, 1.0f);
}

void ChoiceBlock::resized() {
    auto bounds = getLocalBounds().reduced(4, 3);
    auto const topLevel = getTopLevelComponent();
    auto const baseSize = topLevel != nullptr ? calculateTextSize(topLevel->getWidth(), topLevel->getHeight()) : 10;
    auto const labelSize = juce::jlimit(8.0f, 12.0f, juce::jmin(static_cast<float>(baseSize), static_cast<float>(getHeight()) * 0.16f));
    mLabel.setFont(juce::Font(labelSize, juce::Font::bold));
    mFooter.setFont(juce::Font(juce::jmax(7.0f, labelSize - 2.0f), juce::Font::plain));

    mLabel.setBounds(bounds.removeFromTop(juce::jmax(12, bounds.proportionOfHeight(0.25f))));
    if (mFooter.getText().isNotEmpty()) {
        mFooter.setBounds(bounds.removeFromBottom(juce::jmax(10, bounds.proportionOfHeight(0.22f))));
    } else {
        mFooter.setVisible(false);
    }
    mComboBox.setBounds(bounds.withTrimmedTop(2).withTrimmedBottom(2));
}

DecisionMeterStrip::DecisionMeterStrip(LevelMetersPack const& levelMetersPack)
    : mInputLevelMeter(levelMetersPack.inputLevelMeter),
      mOutputLevelMeter(levelMetersPack.outputLevelMeter),
      mDynamicsMeter(levelMetersPack.dynamicsMeter) {
    startTimerHz(20);
}

void DecisionMeterStrip::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().reduced(1);
    g.setColour(colourscheme::BackgroundPrimary.withAlpha(0.45f));
    g.fillRoundedRectangle(bounds.toFloat(), 7.0f);
    g.setColour(colourscheme::BackgroundTertiary.withAlpha(0.28f));
    g.drawRoundedRectangle(bounds.toFloat(), 7.0f, 1.0f);

    bounds.reduce(6, 4);
    auto topRow = bounds.removeFromTop(bounds.getHeight() / 2);
    auto bottomRow = bounds;
    auto const topCellWidth = topRow.getWidth() / 4;
    auto const bottomCellWidth = bottomRow.getWidth() / 4;
    auto const inputPeak = mInputLevelMeter->getPeakDBFS().load();
    auto const peak = mOutputLevelMeter->getPeakDBFS().load();
    auto const rms = mOutputLevelMeter->getRmsDBFS().load();
    auto const crest = mOutputLevelMeter->getCrestFactorDB().load();
    auto const clip = mDynamicsMeter->getClipAmountDb();
    auto const gr = mDynamicsMeter->getGainReductionDb();
    auto const truePeak = mDynamicsMeter->getTruePeakDb();
    auto const shortTermLufs = mDynamicsMeter->getShortTermLufs();
    mHeldTruePeak = juce::jmax(truePeak, mHeldTruePeak - 0.08f);
    mHeldClip = juce::jmax(clip, mHeldClip - 0.08f);

    drawCell(g, topRow.removeFromLeft(topCellWidth), "IN", inputPeak, "dB", inputPeak > -0.3f ? colourscheme::Warning : colourscheme::TextFocusLevel1);
    drawCell(g, topRow.removeFromLeft(topCellWidth), "OUT", peak, "dB", peak > -0.3f ? colourscheme::Warning : colourscheme::TextFocusLevel1);
    drawCell(g, topRow.removeFromLeft(topCellWidth), "TRUE PEAK", mHeldTruePeak, "dB",
             mHeldTruePeak > -0.3f ? colourscheme::Warning : colourscheme::TextFocusLevel1, true);
    drawCell(g, topRow, "LUFS-S", shortTermLufs, "", colourscheme::ForegroundSecondary.brighter(0.15f), true);

    drawCell(g, bottomRow.removeFromLeft(bottomCellWidth), "RMS", rms, "dB", colourscheme::TextFocusLevel1);
    drawCell(g, bottomRow.removeFromLeft(bottomCellWidth), "CREST", crest, "dB", colourscheme::TextFocusLevel1);
    drawCell(g, bottomRow.removeFromLeft(bottomCellWidth), "GR", gr, "dB", gr > 0.1f ? colourscheme::ForegroundTertiary : colourscheme::TextFocusLevel1);
    drawCell(g, bottomRow, "CLIP", mHeldClip, "dB", mHeldClip > 0.1f ? colourscheme::ForegroundSecondary : colourscheme::TextFocusLevel1);
}

void DecisionMeterStrip::timerCallback() {
    repaint();
}

void DecisionMeterStrip::drawCell(juce::Graphics& g, juce::Rectangle<int> bounds, juce::String const& label, float value,
                                  juce::String const& suffix, juce::Colour colour, bool emphasize) {
    bounds.reduce(2, 1);
    if (emphasize) {
        g.setColour(colour.withAlpha(0.07f));
        g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
    }

    auto const labelHeight = juce::jmax(9, bounds.proportionOfHeight(0.43f));
    auto labelBounds = bounds.removeFromTop(labelHeight);
    g.setFont(juce::Font(juce::jlimit(6.5f, 9.5f, static_cast<float>(labelBounds.getHeight()) * 0.76f), juce::Font::bold));
    g.setColour(colourscheme::TextFocusLevel3);
    g.drawText(label, labelBounds, juce::Justification::centred, true);

    auto const valueText = value <= -119.0f ? juce::String("-inf") : juce::String(value, 1) + suffix;
    g.setFont(juce::Font(juce::jlimit(7.0f, 10.5f, static_cast<float>(bounds.getHeight()) * 0.62f), juce::Font::bold));
    g.setColour(colour);
    g.drawText(valueText, bounds, juce::Justification::centred, true);
}

ControlPanel::ControlPanel(std::shared_ptr<juce::AudioProcessorValueTreeState> parameters, LevelMetersPack const& levelMetersPack)
    : juce::Component(),
      inputGain("Input", parameters, "InputGain"),
      algorithmChoice("Type", parameters, "ClippingType",
                      {"EDM", "HIP HOP", "DRUMS", "ONE SHOT", "ACOUSTIC", "VOCAL", "BASS", "BRIGHT", "GLUE", "CLEAN",
                       "PERCS", "DUBSTEP", "DRUMNBASS", "HOUSE", "TRAP", "808&KICK", "ONE SHOT CLEAN"},
                      "Algorithm"),
      drive(parameters),
      ceiling("Ceiling", parameters, "Ceiling", levelMetersPack.inputLevelMeter, levelMetersPack.clippingLevelMeter),
      tone("Tone", parameters, "Tone"),
      bassRecover("Bass\nRecovery", parameters, "BassRecover"),
      toneStyleChoice("Mode", parameters, "ToneStyle", {"Warm", "Clean", "Bright"}),
      qualityChoice("Quality", parameters, "OversampleRate", {"Eco", "Live", "High", "Master", "Ultra", "Max"}, "Realtime safe"),
      dryWet("Dry/Wet", parameters, "DryWet"),
      outputGain("Output", parameters, "OutputGain"),
      attack("Attack", parameters, "Attack"),
      hold("Hold", parameters, "Hold"),
      release("Release", parameters, "Release"),
      transientRecovery("Transient", parameters, "TransientRecovery"),
      lookahead("Lookahead", parameters, "Lookahead"),
      detectorHp("Det HP", parameters, "DetectorHP"),
      saturation("Sat", parameters, "Saturation"),
      decisionMeters(levelMetersPack) {
    addAndMakeVisible(inputGain);
    addAndMakeVisible(algorithmChoice);
    addAndMakeVisible(drive);
    addAndMakeVisible(ceiling);
    addAndMakeVisible(toneStylePanel);
    toneStylePanel.addAndMakeVisible(tone);
    toneStylePanel.addAndMakeVisible(bassRecover);
    toneStylePanel.addAndMakeVisible(toneStyleChoice);
    addAndMakeVisible(qualityChoice);
    addAndMakeVisible(dryWet);
    addAndMakeVisible(outputGain);
    addAndMakeVisible(attack);
    addAndMakeVisible(hold);
    addAndMakeVisible(release);
    addAndMakeVisible(transientRecovery);
    addAndMakeVisible(lookahead);
    addAndMakeVisible(detectorHp);
    addAndMakeVisible(saturation);
    addAndMakeVisible(decisionMeters);
}

void ControlPanel::setAnalyzerFocusMode(bool shouldUse) {
    if (mAnalyzerFocusMode == shouldUse) {
        return;
    }

    mAnalyzerFocusMode = shouldUse;
    resized();
    repaint();
}

void ControlPanel::paint(juce::Graphics& g) {
    if (mAnalyzerFocusMode) {
        auto const bounds = getLocalBounds().toFloat().reduced(gBorderWith);
        g.setColour(colourscheme::BackgroundPrimary.withAlpha(0.94f));
        g.fillRoundedRectangle(bounds, gBorderRadius);

        drawFocusFrame(g, loudnessSectionBounds, "MAIN CONTROLS", colourscheme::ForegroundTertiary);
        drawFocusFrame(g, detailSectionBounds, "DETAIL CONTROLS", colourscheme::BackgroundTertiary);
        drawFocusFrame(g, meterSectionBounds, "METERING", colourscheme::ForegroundPrimary);

        g.setColour(colourscheme::BackgroundTertiary.withAlpha(0.34f));
        g.drawRoundedRectangle(bounds, gBorderRadius, 1.1f);
        return;
    }

    g.setColour(colourscheme::BackgroundSecondary.withAlpha(0.9f));
    auto const bounds = getLocalBounds().toFloat().reduced(gBorderWith);
    g.fillRoundedRectangle(bounds, gBorderRadius);
    drawSection(g, loudnessSectionBounds, colourscheme::ForegroundSecondary, 0.1f);
    drawSection(g, toneSectionBounds, colourscheme::ForegroundPrimary, 0.08f);
    drawSection(g, utilitySectionBounds, colourscheme::ForegroundTertiary, 0.055f);
    drawSection(g, detailSectionBounds, colourscheme::BackgroundTertiary, 0.045f);
    g.setColour(colourscheme::BackgroundTertiary.withAlpha(0.5f));
    g.drawRoundedRectangle(bounds, gBorderRadius, 1);
}

void ControlPanel::resized() {
    auto bounds = getLocalBounds().reduced(7, 6);
    if (mAnalyzerFocusMode) {
        resizedAnalyzerFocus(bounds);
        return;
    }

    auto topRow = bounds.removeFromTop(juce::jmax(82, bounds.proportionOfHeight(0.58f)));
    bounds.removeFromTop(gInnerGap);
    auto bottomRow = bounds;

    auto topRemaining = 7.0f;
    inputGain.setBounds(takeWeightedArea(topRow, 0.72f, topRemaining, gInnerGap));
    algorithmChoice.setBounds(takeWeightedArea(topRow, 0.9f, topRemaining, gInnerGap));
    drive.setBounds(takeWeightedArea(topRow, 1.20f, topRemaining, gInnerGap));
    ceiling.setBounds(takeWeightedArea(topRow, 0.98f, topRemaining, gInnerGap));
    toneStylePanel.setBounds(takeWeightedArea(topRow, 2.26f, topRemaining, gInnerGap));
    outputGain.setBounds(takeWeightedArea(topRow, 0.94f, topRemaining, 0));

    auto toneBounds = toneStylePanel.getLocalBounds().reduced(4, 3);
    auto toneCharacterBounds = toneBounds.removeFromRight(juce::jmax(84, toneBounds.proportionOfWidth(0.36f)));
    toneBounds.removeFromRight(4);
    auto bassRecoverBounds = toneBounds.removeFromRight(juce::jmax(62, toneBounds.proportionOfWidth(0.45f)));
    toneBounds.removeFromRight(4);
    tone.setBounds(toneBounds);
    bassRecover.setBounds(bassRecoverBounds);
    toneStyleChoice.setBounds(toneCharacterBounds);

    auto bottomRemaining = 9.25f;
    qualityChoice.setBounds(takeWeightedArea(bottomRow, 1.26f, bottomRemaining, gInnerGap));
    dryWet.setBounds(takeWeightedArea(bottomRow, 0.68f, bottomRemaining, gInnerGap));
    detectorHp.setBounds(takeWeightedArea(bottomRow, 0.74f, bottomRemaining, gInnerGap));
    saturation.setBounds(takeWeightedArea(bottomRow, 0.68f, bottomRemaining, gInnerGap));
    attack.setBounds(takeWeightedArea(bottomRow, 0.62f, bottomRemaining, gInnerGap));
    hold.setBounds(takeWeightedArea(bottomRow, 0.62f, bottomRemaining, gInnerGap));
    release.setBounds(takeWeightedArea(bottomRow, 0.62f, bottomRemaining, gInnerGap));
    transientRecovery.setBounds(takeWeightedArea(bottomRow, 0.72f, bottomRemaining, gInnerGap));
    lookahead.setBounds(takeWeightedArea(bottomRow, 0.72f, bottomRemaining, gInnerGap));
    decisionMeters.setBounds(takeWeightedArea(bottomRow, 2.59f, bottomRemaining, 0));

    loudnessSectionBounds = algorithmChoice.getBounds().getUnion(drive.getBounds()).getUnion(ceiling.getBounds()).expanded(3, 2);
    toneSectionBounds = toneStylePanel.getBounds().expanded(3, 2);
    utilitySectionBounds = inputGain.getBounds()
                               .getUnion(outputGain.getBounds())
                               .getUnion(qualityChoice.getBounds())
                               .getUnion(dryWet.getBounds())
                               .expanded(3, 2);
    detailSectionBounds = attack.getBounds()
                              .getUnion(hold.getBounds())
                              .getUnion(release.getBounds())
                              .getUnion(transientRecovery.getBounds())
                              .getUnion(lookahead.getBounds())
                              .getUnion(detectorHp.getBounds())
                              .getUnion(saturation.getBounds())
                              .getUnion(decisionMeters.getBounds())
                              .expanded(3, 2);
    meterSectionBounds = decisionMeters.getBounds().expanded(3, 2);
}

juce::Rectangle<int> ControlPanel::takeWeightedArea(juce::Rectangle<int>& row, float weight, float& remainingWeight, int gap) const {
    auto const width = juce::jmax(1, static_cast<int>(std::round(static_cast<float>(row.getWidth()) * weight / remainingWeight)));
    remainingWeight -= weight;
    auto area = row.removeFromLeft(width);
    if (gap > 0 && row.getWidth() > gap) {
        row.removeFromLeft(gap);
    }
    return area.reduced(2);
}

void ControlPanel::drawSection(juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour colour, float alpha) const {
    if (bounds.isEmpty()) {
        return;
    }

    auto const section = bounds.toFloat().reduced(1.0f);
    g.setColour(colour.withAlpha(alpha));
    g.fillRoundedRectangle(section, 8.0f);
    g.setColour(colour.withAlpha(alpha + 0.08f));
    g.drawRoundedRectangle(section, 8.0f, 1.0f);
}

void ControlPanel::drawFocusFrame(juce::Graphics& g, juce::Rectangle<int> bounds, juce::String const& title, juce::Colour colour) const {
    if (bounds.isEmpty()) {
        return;
    }

    auto const section = bounds.toFloat().reduced(0.5f);
    g.setColour(colourscheme::BackgroundSecondary.withAlpha(0.22f));
    g.fillRoundedRectangle(section, 8.0f);
    g.setColour(colour.withAlpha(0.26f));
    g.drawRoundedRectangle(section, 8.0f, 1.0f);

    auto titleBounds = bounds.reduced(10, 4).removeFromTop(16);
    g.setColour(colour.withAlpha(0.72f));
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawText(title, titleBounds, juce::Justification::centredLeft, true);
}

void ControlPanel::resizedAnalyzerFocus(juce::Rectangle<int> bounds) {
    auto content = bounds.reduced(2, 1);
    auto mainRow = content.removeFromTop(juce::jlimit(148, 220, content.proportionOfHeight(0.58f)));
    content.removeFromTop(10);
    auto detailRow = content;

    auto mainArea = mainRow.reduced(8, 8);
    auto mainRemaining = 7.7f;
    inputGain.setBounds(takeWeightedArea(mainArea, 0.85f, mainRemaining, 8));

    auto typeColumn = takeWeightedArea(mainArea, 1.08f, mainRemaining, 8);
    auto typeTop = typeColumn.removeFromTop(typeColumn.getHeight() / 2);
    algorithmChoice.setBounds(typeTop.reduced(2));
    qualityChoice.setBounds(typeColumn.reduced(2));

    drive.setBounds(takeWeightedArea(mainArea, 1.28f, mainRemaining, 8));
    ceiling.setBounds(takeWeightedArea(mainArea, 1.08f, mainRemaining, 8));
    toneStylePanel.setBounds(takeWeightedArea(mainArea, 2.18f, mainRemaining, 8));
    outputGain.setBounds(takeWeightedArea(mainArea, 1.23f, mainRemaining, 0));

    auto toneBounds = toneStylePanel.getLocalBounds().reduced(6, 6);
    auto modeColumn = toneBounds.removeFromRight(juce::jmax(102, toneBounds.proportionOfWidth(0.37f)));
    toneBounds.removeFromRight(8);
    auto bassColumn = toneBounds.removeFromRight(toneBounds.getWidth() / 2);
    toneBounds.removeFromRight(6);
    tone.setBounds(toneBounds);
    bassRecover.setBounds(bassColumn);
    toneStyleChoice.setBounds(modeColumn.reduced(2));

    auto detailArea = detailRow.reduced(8, 8);
    auto meterWidth = juce::jlimit(300, 430, detailArea.proportionOfWidth(0.34f));
    auto meterArea = detailArea.removeFromRight(meterWidth);
    detailArea.removeFromRight(10);

    auto detailRemaining = 8.0f;
    dryWet.setBounds(takeWeightedArea(detailArea, 1.0f, detailRemaining, 8));
    detectorHp.setBounds(takeWeightedArea(detailArea, 1.0f, detailRemaining, 8));
    saturation.setBounds(takeWeightedArea(detailArea, 1.0f, detailRemaining, 8));
    attack.setBounds(takeWeightedArea(detailArea, 1.0f, detailRemaining, 8));
    hold.setBounds(takeWeightedArea(detailArea, 1.0f, detailRemaining, 8));
    release.setBounds(takeWeightedArea(detailArea, 1.0f, detailRemaining, 8));
    transientRecovery.setBounds(takeWeightedArea(detailArea, 1.0f, detailRemaining, 8));
    lookahead.setBounds(takeWeightedArea(detailArea, 1.0f, detailRemaining, 0));
    decisionMeters.setBounds(meterArea.reduced(2));

    loudnessSectionBounds = mainRow.reduced(1);
    toneSectionBounds = toneStylePanel.getBounds().expanded(3, 2);
    utilitySectionBounds = inputGain.getBounds()
                               .getUnion(outputGain.getBounds())
                               .getUnion(algorithmChoice.getBounds())
                               .getUnion(qualityChoice.getBounds())
                               .expanded(3, 2);
    detailSectionBounds = detailRow.withRight(decisionMeters.getX() - 5).reduced(1);
    meterSectionBounds = decisionMeters.getBounds().expanded(3, 2);
}
}  // namespace pe::gui

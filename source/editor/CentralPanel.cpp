#include "CentralPanel.h"

#include "ColourScheme.h"
#include "LevelMetersPack.h"

namespace pe::gui {

juce::Font AdvancedSettingsPanel::AdvancedComboLookAndFeel::getComboBoxFont(juce::ComboBox& box) {
    auto const height = static_cast<float>(box.getHeight());
    return juce::Font(juce::jlimit(15.0f, 18.0f, height * 0.52f), juce::Font::bold);
}

juce::Font AdvancedSettingsPanel::AdvancedComboLookAndFeel::getPopupMenuFont() {
    return juce::Font(16.5f, juce::Font::bold);
}

void AdvancedSettingsPanel::AdvancedComboLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(10, 0, box.getWidth() - 34, box.getHeight());
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centredLeft);
}

AdvancedSettingsPanel::AdvancedSettingsPanel(std::shared_ptr<juce::AudioProcessorValueTreeState> parameters,
                                             LevelMetersPack const& levelMetersPack)
    : mGrLimit("GR Limit", parameters, "GRLimit"),
      mPunchProtect("Punch", parameters, "PunchProtect"),
      mHfGuard("HF Guard", parameters, "HFGuard"),
      mFinalClip("Final Clip", parameters, "FinalClip"),
      mReleaseShape("Rel Shape", parameters, "ReleaseShape"),
      mAdaptiveRelease("Adapt Rel", parameters, "AdaptiveRelease"),
      mDetectorTilt("Det Tilt", parameters, "DetectorTilt"),
      mStereoLink("Stereo Link", parameters, "StereoLink"),
      mLowProtect("Low Protect", parameters, "LowProtect"),
      mTruePeakMargin("TP Margin", parameters, "TruePeakMargin"),
      mLookahead("Lookahead", parameters, "Lookahead"),
      mRelease("Release", parameters, "Release"),
      mSaturation("Saturation", parameters, "Saturation"),
      mSaturationTone("Sat Tone", parameters, "SaturationTone"),
      mSaturationDensity("Density", parameters, "SaturationDensity"),
      mBassSafe("Bass Safe", parameters, "BassSafe"),
      mDetectorHp("Detector HP", parameters, "DetectorHP"),
      mAttack("Attack", parameters, "Attack"),
      mHold("Hold", parameters, "Hold"),
      mTransient("Transient", parameters, "TransientRecovery"),
      mDryWet("Dry/Wet", parameters, "DryWet"),
      mMeterTargetLufs("Target", parameters, "MeterTargetLufs"),
      mInputLevelMeter(levelMetersPack.inputLevelMeter),
      mOutputLevelMeter(levelMetersPack.outputLevelMeter),
      mDynamicsMeter(levelMetersPack.dynamicsMeter) {
    configureTabButton(mLimiterTab, "LIMITER");
    configureTabButton(mToneSatTab, "TONE & SAT");
    configureTabButton(mMeteringTab, "METERING");
    addAndMakeVisible(mLimiterTab);
    addAndMakeVisible(mToneSatTab);
    addAndMakeVisible(mMeteringTab);

    mLimiterTab.onClick = [this] { setActiveTab(Tab::Limiter); };
    mToneSatTab.onClick = [this] { setActiveTab(Tab::ToneSat); };
    mMeteringTab.onClick = [this] { setActiveTab(Tab::Metering); };

    mLimiterStyleLabel.setText("STYLE", juce::dontSendNotification);
    mLimiterStyleLabel.setJustificationType(juce::Justification::centredLeft);
    mLimiterStyleLabel.setFont(juce::Font(10.5f, juce::Font::bold));
    mLimiterStyleLabel.setColour(juce::Label::textColourId, colourscheme::TextFocusLevel2);
    addAndMakeVisible(mLimiterStyleLabel);

    int styleItemId = 1;
    for (auto const& styleName : {juce::String("Transparent"), juce::String("Punch"), juce::String("Glue"), juce::String("Safe"),
                                  juce::String("Loud")}) {
        mLimiterStyle.addItem(styleName, styleItemId++);
    }
    mLimiterStyle.setJustificationType(juce::Justification::centredLeft);
    mLimiterStyle.setColour(juce::ComboBox::backgroundColourId, colourscheme::BackgroundPrimary.withAlpha(0.72f));
    mLimiterStyle.setColour(juce::ComboBox::outlineColourId, colourscheme::ForegroundPrimary.withAlpha(0.45f));
    mLimiterStyle.setColour(juce::ComboBox::textColourId, colourscheme::TextFocusLevel0);
    mLimiterStyle.setColour(juce::ComboBox::arrowColourId, colourscheme::ForegroundTertiary);
    mLimiterStyle.setLookAndFeel(&mAdvancedComboLookAndFeel);
    mLimiterStyleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(*parameters, "LimiterStyle",
                                                                                                        mLimiterStyle);
    addAndMakeVisible(mLimiterStyle);

    mSaturationModeLabel.setText("SAT MODE", juce::dontSendNotification);
    mSaturationModeLabel.setJustificationType(juce::Justification::centredLeft);
    mSaturationModeLabel.setFont(juce::Font(10.5f, juce::Font::bold));
    mSaturationModeLabel.setColour(juce::Label::textColourId, colourscheme::TextFocusLevel2);
    addAndMakeVisible(mSaturationModeLabel);

    int saturationItemId = 1;
    for (auto const& modeName : {juce::String("Clean"), juce::String("Warm"), juce::String("Tube"), juce::String("Tape"),
                                 juce::String("XFMR")}) {
        mSaturationMode.addItem(modeName, saturationItemId++);
    }
    mSaturationMode.setJustificationType(juce::Justification::centredLeft);
    mSaturationMode.setColour(juce::ComboBox::backgroundColourId, colourscheme::BackgroundPrimary.withAlpha(0.72f));
    mSaturationMode.setColour(juce::ComboBox::outlineColourId, colourscheme::ForegroundPrimary.withAlpha(0.45f));
    mSaturationMode.setColour(juce::ComboBox::textColourId, colourscheme::TextFocusLevel0);
    mSaturationMode.setColour(juce::ComboBox::arrowColourId, colourscheme::ForegroundTertiary);
    mSaturationMode.setLookAndFeel(&mAdvancedComboLookAndFeel);
    mSaturationModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(*parameters, "SaturationMode",
                                                                                                          mSaturationMode);
    addAndMakeVisible(mSaturationMode);

    mTruePeakLimit.setButtonText("TRUE PEAK");
    mTruePeakLimit.setColour(juce::ToggleButton::textColourId, colourscheme::TextFocusLevel1);
    mTruePeakLimitAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(*parameters, "TruePeakLimit",
                                                                                                       mTruePeakLimit);
    mDeltaListen.setButtonText("DELTA LISTEN");
    mDeltaListen.setColour(juce::ToggleButton::textColourId, colourscheme::TextFocusLevel1);
    mDeltaListenAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(*parameters, "DeltaListen",
                                                                                                    mDeltaListen);
    mDeltaSourceLabel.setText("DELTA SRC", juce::dontSendNotification);
    mDeltaSourceLabel.setJustificationType(juce::Justification::centredLeft);
    mDeltaSourceLabel.setFont(juce::Font(10.5f, juce::Font::bold));
    mDeltaSourceLabel.setColour(juce::Label::textColourId, colourscheme::TextFocusLevel2);
    int deltaSourceItemId = 1;
    for (auto const& sourceName : {juce::String("All"), juce::String("Limiter"), juce::String("Spectral"),
                                   juce::String("Sat+Clip"), juce::String("Ceiling")}) {
        mDeltaSource.addItem(sourceName, deltaSourceItemId++);
    }
    mDeltaSource.setJustificationType(juce::Justification::centredLeft);
    mDeltaSource.setColour(juce::ComboBox::backgroundColourId, colourscheme::BackgroundPrimary.withAlpha(0.72f));
    mDeltaSource.setColour(juce::ComboBox::outlineColourId, colourscheme::ForegroundPrimary.withAlpha(0.45f));
    mDeltaSource.setColour(juce::ComboBox::textColourId, colourscheme::TextFocusLevel0);
    mDeltaSource.setColour(juce::ComboBox::arrowColourId, colourscheme::ForegroundTertiary);
    mDeltaSource.setLookAndFeel(&mAdvancedComboLookAndFeel);
    mDeltaSourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(*parameters, "DeltaSource",
                                                                                                      mDeltaSource);
    mGainMatch.setButtonText("GAIN MATCH");
    mGainMatch.setColour(juce::ToggleButton::textColourId, colourscheme::TextFocusLevel1);
    mGainMatchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(*parameters, "GainMatch",
                                                                                                  mGainMatch);
    addAndMakeVisible(mTruePeakLimit);
    addAndMakeVisible(mDeltaListen);
    addAndMakeVisible(mDeltaSourceLabel);
    addAndMakeVisible(mDeltaSource);
    addAndMakeVisible(mGainMatch);
    addAndMakeVisible(mGrLimit);
    addAndMakeVisible(mPunchProtect);
    addAndMakeVisible(mHfGuard);
    addAndMakeVisible(mFinalClip);
    addAndMakeVisible(mReleaseShape);
    addAndMakeVisible(mAdaptiveRelease);
    addAndMakeVisible(mDetectorTilt);
    addAndMakeVisible(mStereoLink);
    addAndMakeVisible(mLowProtect);
    addAndMakeVisible(mTruePeakMargin);
    addAndMakeVisible(mLookahead);
    addAndMakeVisible(mRelease);
    addAndMakeVisible(mSaturation);
    addAndMakeVisible(mSaturationTone);
    addAndMakeVisible(mSaturationDensity);
    addAndMakeVisible(mBassSafe);
    addAndMakeVisible(mDetectorHp);
    addAndMakeVisible(mAttack);
    addAndMakeVisible(mHold);
    addAndMakeVisible(mTransient);
    addAndMakeVisible(mDryWet);
    addAndMakeVisible(mMeterTargetLufs);
    updateVisibleControls();
    startTimerHz(20);
}

AdvancedSettingsPanel::~AdvancedSettingsPanel() {
    mLimiterStyle.setLookAndFeel(nullptr);
    mSaturationMode.setLookAndFeel(nullptr);
    mDeltaSource.setLookAndFeel(nullptr);
}

void AdvancedSettingsPanel::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat().reduced(4.0f);
    g.setColour(colourscheme::BackgroundSecondary.withAlpha(0.92f));
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(colourscheme::ForegroundPrimary.withAlpha(0.38f));
    g.drawRoundedRectangle(bounds, 8.0f, 1.3f);

    auto titleBounds = getLocalBounds().reduced(12, 7).removeFromTop(18);
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.setColour(colourscheme::TextFocusLevel1);
    g.drawText("ADVANCED SETTINGS", titleBounds, juce::Justification::centredLeft, true);

    if (mActiveTab == Tab::Metering) {
        auto meterBounds = getLocalBounds().reduced(14, 42);
        meterBounds.removeFromLeft(juce::jlimit(150, 190, meterBounds.proportionOfWidth(0.22f)));
        auto topRow = meterBounds.removeFromTop(meterBounds.getHeight() / 2).reduced(0, 2);
        auto bottomRow = meterBounds.reduced(0, 2);
        auto const topWidth = topRow.getWidth() / 5;
        auto const bottomWidth = bottomRow.getWidth() / 6;
        drawMeterCell(g, topRow.removeFromLeft(topWidth), "INPUT PEAK", mInputLevelMeter->getPeakDBFS().load(), "dB",
                      colourscheme::TextFocusLevel1);
        drawMeterCell(g, topRow.removeFromLeft(topWidth), "OUTPUT", mDynamicsMeter->getOutputPeakDb(), "dB",
                      colourscheme::TextFocusLevel1);
        drawMeterCell(g, topRow.removeFromLeft(topWidth), "OUT MAX", mDynamicsMeter->getOutputPeakMaxDb(), "dB",
                      colourscheme::TextFocusLevel1);
        drawMeterCell(g, topRow.removeFromLeft(topWidth), "TRUE PEAK", mDynamicsMeter->getTruePeakDb(), "dB",
                      mDynamicsMeter->getTruePeakDb() > -0.3f ? colourscheme::Warning : colourscheme::TextFocusLevel1);
        drawMeterCell(g, topRow, "TP MAX", mDynamicsMeter->getTruePeakMaxDb(), "dB",
                      mDynamicsMeter->getTruePeakMaxDb() > -0.3f ? colourscheme::Warning : colourscheme::ForegroundSecondary.brighter(0.12f));
        drawMeterCell(g, bottomRow.removeFromLeft(bottomWidth), "MOMENTARY", mDynamicsMeter->getMomentaryLufs(), "",
                      colourscheme::TextFocusLevel1);
        drawMeterCell(g, bottomRow.removeFromLeft(bottomWidth), "LUFS-S", mDynamicsMeter->getShortTermLufs(), "",
                      colourscheme::ForegroundSecondary.brighter(0.12f));
        drawMeterCell(g, bottomRow.removeFromLeft(bottomWidth), "LUFS-I", mDynamicsMeter->getIntegratedLufs(), "",
                      colourscheme::ForegroundTertiary);
        drawMeterCell(g, bottomRow.removeFromLeft(bottomWidth), "CREST MAX", mDynamicsMeter->getCrestMaxDb(), "dB",
                      colourscheme::TextFocusLevel1);
        drawMeterCell(g, bottomRow.removeFromLeft(bottomWidth), "GR MAX", mDynamicsMeter->getGainReductionMaxDb(), "dB",
                      colourscheme::ForegroundTertiary);
        drawMeterCell(g, bottomRow, "CLIP MAX", mDynamicsMeter->getClipAmountMaxDb(), "dB", colourscheme::ForegroundSecondary);
    }
}

void AdvancedSettingsPanel::resized() {
    auto bounds = getLocalBounds().reduced(10, 8);
    auto header = bounds.removeFromTop(24);
    header.removeFromLeft(170);
    auto const tabWidth = juce::jmax(74, header.getWidth() / 3);
    mLimiterTab.setBounds(header.removeFromLeft(tabWidth).reduced(2));
    mToneSatTab.setBounds(header.removeFromLeft(tabWidth).reduced(2));
    mMeteringTab.setBounds(header.reduced(2));

    bounds.removeFromTop(8);
    auto row = bounds.reduced(6, 4);
    auto constexpr gap = 6;
    if (mActiveTab == Tab::Limiter) {
        auto topRow = row.removeFromTop(juce::jlimit(132, 158, row.proportionOfHeight(0.52f)));
        row.removeFromTop(8);
        auto middleRow = row.removeFromTop(row.getHeight() / 2).reduced(0, 1);
        row.removeFromTop(6);
        auto bottomRow = row.reduced(0, 1);

        auto styleColumn = topRow.removeFromLeft(juce::jlimit(232, 280, topRow.proportionOfWidth(0.3f))).reduced(4, 2);
        mLimiterStyleLabel.setBounds(styleColumn.removeFromTop(16));
        mLimiterStyle.setBounds(styleColumn.removeFromTop(40).reduced(0, 1));
        mTruePeakLimit.setBounds(styleColumn.removeFromTop(24).reduced(0, 2));
        mDeltaListen.setBounds(styleColumn.removeFromTop(24).reduced(0, 2));
        mDeltaSourceLabel.setBounds(styleColumn.removeFromTop(16));
        mDeltaSource.setBounds(styleColumn.removeFromTop(42).reduced(0, 1));
        auto mainCount = 4;
        mGrLimit.setBounds(takeArea(topRow, mainCount--, gap));
        mPunchProtect.setBounds(takeArea(topRow, mainCount--, gap));
        mHfGuard.setBounds(takeArea(topRow, mainCount--, gap));
        mFinalClip.setBounds(takeArea(topRow, mainCount, 0));

        auto middleCount = 5;
        mReleaseShape.setBounds(takeArea(middleRow, middleCount--, gap));
        mAdaptiveRelease.setBounds(takeArea(middleRow, middleCount--, gap));
        mDetectorTilt.setBounds(takeArea(middleRow, middleCount--, gap));
        mStereoLink.setBounds(takeArea(middleRow, middleCount--, gap));
        mLowProtect.setBounds(takeArea(middleRow, middleCount, 0));

        auto bottomCount = 3;
        mTruePeakMargin.setBounds(takeArea(bottomRow, bottomCount--, gap));
        mLookahead.setBounds(takeArea(bottomRow, bottomCount--, gap));
        mRelease.setBounds(takeArea(bottomRow, bottomCount, 0));
    } else if (mActiveTab == Tab::ToneSat) {
        auto topRow = row.removeFromTop(juce::jlimit(120, 148, row.proportionOfHeight(0.48f)));
        row.removeFromTop(8);
        auto middleRow = row.removeFromTop(row.getHeight() / 2).reduced(0, 1);
        row.removeFromTop(6);
        auto bottomRow = row.reduced(0, 1);

        auto styleColumn = topRow.removeFromLeft(juce::jlimit(164, 196, topRow.proportionOfWidth(0.22f))).reduced(4, 2);
        mSaturationModeLabel.setBounds(styleColumn.removeFromTop(18));
        mSaturationMode.setBounds(styleColumn.removeFromTop(40).reduced(0, 2));
        auto mainCount = 4;
        mSaturation.setBounds(takeArea(topRow, mainCount--, gap));
        mSaturationDensity.setBounds(takeArea(topRow, mainCount--, gap));
        mSaturationTone.setBounds(takeArea(topRow, mainCount--, gap));
        mBassSafe.setBounds(takeArea(topRow, mainCount, 0));

        auto middleCount = 5;
        mDetectorHp.setBounds(takeArea(middleRow, middleCount--, gap));
        mAttack.setBounds(takeArea(middleRow, middleCount--, gap));
        mHold.setBounds(takeArea(middleRow, middleCount--, gap));
        mTransient.setBounds(takeArea(middleRow, middleCount--, gap));
        mDryWet.setBounds(takeArea(middleRow, middleCount, 0));

        mRelease.setBounds(bottomRow.removeFromLeft(juce::jmax(140, bottomRow.getWidth() / 3)).reduced(2));
    } else if (mActiveTab == Tab::Metering) {
        auto controlColumn = row.removeFromLeft(juce::jlimit(150, 190, row.proportionOfWidth(0.22f))).reduced(4, 8);
        mGainMatch.setBounds(controlColumn.removeFromTop(34).reduced(0, 3));
        controlColumn.removeFromTop(8);
        mMeterTargetLufs.setBounds(controlColumn.removeFromTop(118).reduced(2));
    }
}

void AdvancedSettingsPanel::mouseDown(juce::MouseEvent const& event) {
    if (mActiveTab == Tab::Metering && event.mods.isLeftButtonDown()) {
        mDynamicsMeter->resetMaxima();
        repaint();
    }
}

juce::Rectangle<int> AdvancedSettingsPanel::takeArea(juce::Rectangle<int>& row, int count, int gap) const {
    auto area = row.removeFromLeft(row.getWidth() / juce::jmax(1, count));
    if (gap > 0 && row.getWidth() > gap) {
        row.removeFromLeft(gap);
    }
    return area.reduced(2);
}

void AdvancedSettingsPanel::configureTabButton(juce::TextButton& button, juce::String const& text) {
    button.setButtonText(text);
    button.setClickingTogglesState(false);
    button.setColour(juce::TextButton::buttonColourId, colourscheme::BackgroundPrimary.withAlpha(0.55f));
    button.setColour(juce::TextButton::buttonOnColourId, colourscheme::ForegroundPrimary.withAlpha(0.75f));
    button.setColour(juce::TextButton::textColourOffId, colourscheme::TextFocusLevel2);
    button.setColour(juce::TextButton::textColourOnId, colourscheme::TextFocusLevel0);
}

void AdvancedSettingsPanel::setActiveTab(Tab tab) {
    mActiveTab = tab;
    updateVisibleControls();
    resized();
    repaint();
}

void AdvancedSettingsPanel::updateVisibleControls() {
    auto const limiter = mActiveTab == Tab::Limiter;
    auto const toneSat = mActiveTab == Tab::ToneSat;
    auto const metering = mActiveTab == Tab::Metering;
    mLimiterTab.setToggleState(limiter, juce::dontSendNotification);
    mToneSatTab.setToggleState(toneSat, juce::dontSendNotification);
    mMeteringTab.setToggleState(metering, juce::dontSendNotification);

    mLimiterStyleLabel.setVisible(limiter);
    mLimiterStyle.setVisible(limiter);
    mSaturationModeLabel.setVisible(toneSat);
    mSaturationMode.setVisible(toneSat);
    mTruePeakLimit.setVisible(limiter);
    mDeltaListen.setVisible(limiter);
    mDeltaSourceLabel.setVisible(limiter);
    mDeltaSource.setVisible(limiter);
    mGainMatch.setVisible(metering);
    mGrLimit.setVisible(limiter);
    mPunchProtect.setVisible(limiter);
    mHfGuard.setVisible(limiter);
    mFinalClip.setVisible(limiter);
    mReleaseShape.setVisible(limiter);
    mAdaptiveRelease.setVisible(limiter);
    mDetectorTilt.setVisible(limiter);
    mStereoLink.setVisible(limiter);
    mLowProtect.setVisible(limiter);
    mTruePeakMargin.setVisible(limiter);
    mLookahead.setVisible(limiter);
    mRelease.setVisible(limiter || toneSat);

    mSaturation.setVisible(toneSat);
    mSaturationTone.setVisible(toneSat);
    mSaturationDensity.setVisible(toneSat);
    mBassSafe.setVisible(toneSat);
    mDetectorHp.setVisible(toneSat);
    mAttack.setVisible(toneSat);
    mHold.setVisible(toneSat);
    mTransient.setVisible(toneSat);
    mDryWet.setVisible(toneSat);
    mMeterTargetLufs.setVisible(metering);
}

void AdvancedSettingsPanel::drawMeterCell(juce::Graphics& g, juce::Rectangle<int> bounds, juce::String const& label, float value,
                                          juce::String const& suffix, juce::Colour colour) {
    auto cell = bounds.reduced(4, 3).toFloat();
    g.setColour(colourscheme::BackgroundPrimary.withAlpha(0.42f));
    g.fillRoundedRectangle(cell, 5.0f);
    g.setColour(colourscheme::BackgroundTertiary.withAlpha(0.18f));
    g.drawRoundedRectangle(cell, 5.0f, 1.0f);

    auto textBounds = bounds.reduced(7, 4);
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.setColour(colourscheme::TextFocusLevel3);
    g.drawText(label, textBounds.removeFromTop(15), juce::Justification::centred, true);
    auto const valueText = value <= -119.0f ? juce::String("-inf") : juce::String(value, 1) + suffix;
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.setColour(colour);
    g.drawText(valueText, textBounds, juce::Justification::centred, true);
}

void AdvancedSettingsPanel::timerCallback() {
    if (mActiveTab == Tab::Metering) {
        repaint();
    }
}

CentralPanel::CentralPanel(std::shared_ptr<juce::AudioProcessorValueTreeState> parameters, LevelMetersPack const& levelMetersPack,
                           std::shared_ptr<Ticks> ticks, std::function<void(bool)> advancedSettingsChanged,
                           std::function<void(bool)> analyzerFocusChanged)
    : juce::Component(),
      mClipMeter(parameters, levelMetersPack, ticks),
      mControlPanel(parameters, levelMetersPack),
      mLinkingPanel(parameters),
      mAnalyserComponent(parameters, levelMetersPack),
      mScalingSwitch(ticks),
      mAdvancedSettingsPanel(parameters, levelMetersPack),
      mAdvancedSettingsChanged(std::move(advancedSettingsChanged)),
      mAnalyzerFocusChanged(std::move(analyzerFocusChanged)) {
    addAndMakeVisible(mClipMeter);
    addAndMakeVisible(mControlPanel);
    addAndMakeVisible(mLinkingPanel);
    addAndMakeVisible(mAnalyserComponent);
    addAndMakeVisible(mScalingSwitch);
    addAndMakeVisible(mAdvancedSettingsPanel);
    addAndMakeVisible(mAdvancedSettingsButton);
    addAndMakeVisible(mAnalyzerFocusButton);

    mAdvancedSettingsPanel.setVisible(false);
    mAdvancedSettingsButton.setButtonText("Advanced Settings");
    mAdvancedSettingsButton.setColour(juce::TextButton::buttonColourId, colourscheme::BackgroundPrimary.withAlpha(0.78f));
    mAdvancedSettingsButton.setColour(juce::TextButton::buttonOnColourId, colourscheme::ForegroundPrimary.withAlpha(0.65f));
    mAdvancedSettingsButton.setColour(juce::TextButton::textColourOffId, colourscheme::TextFocusLevel0);
    mAdvancedSettingsButton.setColour(juce::TextButton::textColourOnId, colourscheme::TextFocusLevel0);
    mAdvancedSettingsButton.onClick = [this] { setAdvancedSettingsVisible(!mAdvancedSettingsVisible); };

    mAnalyzerFocusButton.setButtonText("Compact View");
    mAnalyzerFocusButton.setColour(juce::TextButton::buttonColourId, colourscheme::BackgroundPrimary.withAlpha(0.78f));
    mAnalyzerFocusButton.setColour(juce::TextButton::buttonOnColourId, colourscheme::ForegroundPrimary.withAlpha(0.65f));
    mAnalyzerFocusButton.setColour(juce::TextButton::textColourOffId, colourscheme::TextFocusLevel0);
    mAnalyzerFocusButton.setColour(juce::TextButton::textColourOnId, colourscheme::TextFocusLevel0);
    mAnalyzerFocusButton.onClick = [this] { setAnalyzerFocusVisible(false); };
    mAnalyzerFocusButton.setVisible(false);

    mAnalyserComponent.setScopeClickCallback([this] { setAnalyzerFocusVisible(mViewMode != ViewMode::AnalyzerFocus); });
    mClipMeter.setClickCallback([this] { setAnalyzerFocusVisible(mViewMode != ViewMode::AnalyzerFocus); });
}

void CentralPanel::paint(juce::Graphics& g) {
    if (mViewMode != ViewMode::AnalyzerFocus) {
        return;
    }

    auto bounds = getLocalBounds().toFloat();
    juce::ColourGradient background(colourscheme::BackgroundPrimary.darker(0.55f), bounds.getTopLeft(),
                                    colourscheme::BackgroundPrimary.brighter(0.05f), bounds.getBottomRight(), false);
    g.setGradientFill(background);
    g.fillRoundedRectangle(bounds.reduced(2.0f), 9.0f);

    auto header = getLocalBounds().reduced(getLocalBounds().proportionOfWidth(0.02f), getLocalBounds().proportionOfHeight(0.025f))
                      .removeFromTop(86);
    auto headerFloat = header.toFloat().reduced(1.0f);
    g.setColour(colourscheme::BackgroundPrimary.withAlpha(0.66f));
    g.fillRoundedRectangle(headerFloat, 8.0f);
    g.setColour(colourscheme::BackgroundTertiary.withAlpha(0.16f));
    g.drawRoundedRectangle(headerFloat, 8.0f, 1.0f);

    auto titleArea = header.reduced(18, 10).removeFromLeft(header.proportionOfWidth(0.24f));
    g.setColour(colourscheme::TextFocusLevel0);
    g.setFont(juce::Font(24.0f, juce::Font::plain));
    g.drawText("Peakeater", titleArea.removeFromTop(34), juce::Justification::centredLeft, true);
    g.setColour(colourscheme::TextFocusLevel2);
    g.setFont(juce::Font(12.0f, juce::Font::plain));
    g.drawText("Spectral Dynamics Processor", titleArea, juce::Justification::centredLeft, true);

    auto statusArea = header.reduced(18, 14)
                          .withTrimmedLeft(header.proportionOfWidth(0.38f))
                          .withTrimmedRight(190);
    auto drawStatusCell = [&g](juce::Rectangle<int> cell, juce::String const& label, juce::String const& value, juce::Colour valueColour) {
        cell.reduce(4, 0);
        auto labelArea = cell.removeFromTop(cell.getHeight() / 2);
        g.setFont(juce::Font(10.5f, juce::Font::bold));
        g.setColour(colourscheme::TextFocusLevel3);
        g.drawText(label.toUpperCase(), labelArea, juce::Justification::centredLeft, true);
        g.setFont(juce::Font(12.5f, juce::Font::bold));
        g.setColour(valueColour);
        g.drawText(value, cell, juce::Justification::centredLeft, true);
    };

    auto const cellWidth = statusArea.getWidth() / 6;
    drawStatusCell(statusArea.removeFromLeft(cellWidth), "Status", "READY", colourscheme::ForegroundTertiary);
    drawStatusCell(statusArea.removeFromLeft(cellWidth), "View", "FOCUS", colourscheme::TextFocusLevel1);
    drawStatusCell(statusArea.removeFromLeft(cellWidth), "Scope", "5 BAND", colourscheme::TextFocusLevel1);
    drawStatusCell(statusArea.removeFromLeft(cellWidth), "Rate", "60 Hz", colourscheme::TextFocusLevel1);
    drawStatusCell(statusArea.removeFromLeft(cellWidth), "Audio", "DSP SAFE", colourscheme::TextFocusLevel1);
    drawStatusCell(statusArea, "Click", "COMPACT", colourscheme::ForegroundPrimary.brighter(0.2f));
}

void CentralPanel::resized() {
    auto const localBounds = getLocalBounds();
    auto const analyzerFocus = mViewMode == ViewMode::AnalyzerFocus;

    if (analyzerFocus) {
        auto bounds = localBounds.reduced(localBounds.proportionOfWidth(0.02f), localBounds.proportionOfHeight(0.025f));
        auto const headerHeight = juce::jlimit(76, 96, bounds.proportionOfHeight(0.09f));
        bounds.removeFromTop(headerHeight);
        bounds.removeFromTop(12);

        auto const scopeHeight = juce::jlimit(360, 500, bounds.proportionOfHeight(0.52f));
        auto scopeBounds = bounds.removeFromTop(scopeHeight);
        bounds.removeFromTop(12);

        mClipMeter.setBounds(scopeBounds);

        auto const analyserWidth = juce::jlimit(360, 560, scopeBounds.proportionOfWidth(0.42f));
        auto const analyserHeight = juce::jlimit(120, 170, scopeBounds.proportionOfHeight(0.36f));
        mAnalyserComponent.setBounds(scopeBounds.withTrimmedLeft(scopeBounds.proportionOfWidth(0.06f))
                                         .withTrimmedTop(scopeBounds.proportionOfHeight(0.10f))
                                         .withWidth(analyserWidth)
                                         .withHeight(analyserHeight));

        auto const scalingWidth = juce::jlimit(64, 120, scopeBounds.proportionOfWidth(0.10f));
        auto const scalingHeight = juce::jlimit(28, 44, scopeBounds.proportionOfHeight(0.11f));
        mScalingSwitch.setBounds(scopeBounds.withWidth(scalingWidth)
                                     .withHeight(scalingHeight)
                                     .withX(scopeBounds.getRight() - scopeBounds.proportionOfWidth(0.15f))
                                     .withY(scopeBounds.getY() + scopeBounds.proportionOfHeight(0.12f)));

        auto controlsBounds = bounds;
        auto const linkHeight = juce::jlimit(36, 58, controlsBounds.proportionOfHeight(0.12f));
        auto linkingBounds = controlsBounds.removeFromBottom(linkHeight);
        controlsBounds.removeFromBottom(8);
        mControlPanel.setBounds(controlsBounds);
        mLinkingPanel.setBounds(linkingBounds.withTrimmedLeft(linkingBounds.proportionOfWidth(0.08f))
                                            .withTrimmedRight(linkingBounds.proportionOfWidth(0.08f)));

        mAdvancedSettingsPanel.setVisible(false);
        mAdvancedSettingsPanel.setBounds({});
        mAdvancedSettingsButton.setVisible(true);
        mAnalyzerFocusButton.setVisible(true);
        auto const buttonWidth = juce::jlimit(128, 184, localBounds.proportionOfWidth(0.16f));
        auto const buttonHeight = juce::jlimit(24, 34, localBounds.proportionOfHeight(0.042f));
        mAnalyzerFocusButton.setBounds(localBounds.getRight() - buttonWidth - 34, localBounds.proportionOfHeight(0.055f), buttonWidth, buttonHeight);
        mAdvancedSettingsButton.setBounds(localBounds.getRight() - buttonWidth - 34,
                                          linkingBounds.getY() + (linkingBounds.getHeight() - buttonHeight) / 2,
                                          buttonWidth,
                                          buttonHeight);
        return;
    }

    mAnalyzerFocusButton.setVisible(false);
    mAdvancedSettingsPanel.setVisible(mAdvancedSettingsVisible);
    mAdvancedSettingsButton.setVisible(true);
    auto contentBounds = localBounds;
    auto advancedBounds = juce::Rectangle<int>{};
    if (mAdvancedSettingsVisible) {
        auto const advancedHeight = juce::jlimit(300, 340, localBounds.proportionOfHeight(0.44f));
        advancedBounds = contentBounds.removeFromBottom(advancedHeight).reduced(localBounds.proportionOfWidth(0.025f), 5);
    }

    juce::Grid grid;
    using Track = juce::Grid::TrackInfo;
    using Fr = juce::Grid::Fr;
    using Item = juce::GridItem;
    grid.templateRows = {Track(Fr(5)), Track(Fr(1))};
    grid.templateColumns = {Track(Fr(1))};
    auto const linkingRightMargin = static_cast<float>(contentBounds.proportionOfWidth(0.13f));
    grid.items = {Item(mControlPanel), Item(mLinkingPanel).withMargin(Item::Margin(0.0f, linkingRightMargin, 0.0f, 0.0f))};
    auto const toRemoveFromTop = contentBounds.proportionOfHeight(mAdvancedSettingsVisible ? 0.58f : 0.64f);
    auto const toRemoveFromSides = contentBounds.proportionOfWidth(0.025f);
    auto const scopeBounds = contentBounds.withBottom(juce::jmax(contentBounds.getY(), contentBounds.getY() + toRemoveFromTop - 8));
    mClipMeter.setBounds(scopeBounds);

    grid.performLayout(contentBounds.withTrimmedTop(toRemoveFromTop).withTrimmedLeft(toRemoveFromSides).withTrimmedRight(toRemoveFromSides));

    auto const analyserHeight = juce::jlimit(82, 116, scopeBounds.proportionOfHeight(0.30f));
    mAnalyserComponent.setBounds(scopeBounds.withWidth(scopeBounds.proportionOfWidth(0.46f))
                                     .withHeight(analyserHeight)
                                     .withX(scopeBounds.proportionOfWidth(0.08f))
                                     .withY(scopeBounds.proportionOfHeight(0.09f)));

    auto const scalingWidth = juce::jmax(44, scopeBounds.proportionOfWidth(0.075f));
    auto const scalingHeight = juce::jlimit(28, 48, scopeBounds.proportionOfHeight(0.13f));
    mScalingSwitch.setBounds(scopeBounds.withWidth(scalingWidth)
                                 .withHeight(scalingHeight)
                                 .withX(scopeBounds.proportionOfWidth(0.78f))
                                 .withY(scopeBounds.proportionOfHeight(0.22f)));

    auto const buttonWidth = juce::jlimit(132, 190, localBounds.proportionOfWidth(0.18f));
    auto const buttonHeight = juce::jlimit(22, 32, localBounds.proportionOfHeight(0.058f));
    auto const buttonX = localBounds.proportionOfWidth(0.74f);
    auto const buttonY = mAdvancedSettingsVisible ? contentBounds.getBottom() - buttonHeight - 6 : localBounds.proportionOfHeight(0.91f);
    mAdvancedSettingsButton.setBounds(buttonX, buttonY, buttonWidth, buttonHeight);
    mAdvancedSettingsPanel.setBounds(advancedBounds);
}

void CentralPanel::setAdvancedSettingsVisible(bool shouldShow) {
    if (mViewMode == ViewMode::AnalyzerFocus) {
        setAnalyzerFocusVisible(false);
    }
    mAdvancedSettingsVisible = shouldShow;
    mAdvancedSettingsPanel.setVisible(shouldShow);
    mAdvancedSettingsButton.setToggleState(shouldShow, juce::dontSendNotification);
    if (mAdvancedSettingsChanged) {
        mAdvancedSettingsChanged(shouldShow);
    }
    resized();
    repaint();
}

void CentralPanel::setAnalyzerFocusVisible(bool shouldShow) {
    mViewMode = shouldShow ? ViewMode::AnalyzerFocus : ViewMode::Compact;
    mControlPanel.setAnalyzerFocusMode(shouldShow);
    if (shouldShow) {
        mAdvancedSettingsVisible = false;
        mAdvancedSettingsPanel.setVisible(false);
        mAdvancedSettingsButton.setToggleState(false, juce::dontSendNotification);
    }
    mAnalyzerFocusButton.setVisible(shouldShow);
    if (mAnalyzerFocusChanged) {
        mAnalyzerFocusChanged(shouldShow);
    }
    resized();
    repaint();
}
}  // namespace pe::gui

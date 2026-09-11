#include "ClipTypeComponent.h"

#include <JuceHeader.h>

#include <algorithm>
#include <cmath>
#include <utility>

#include "../../ColourScheme.h"

namespace pe::gui {
namespace {
int constexpr gBorderWidth = 1;
int constexpr gBorderRadius = 10;
float constexpr gWaveThickness = 1.2f;
int constexpr gScopeRefreshHz = 60;
size_t constexpr gVisibleSamples = 384;

float calculatePeak(std::vector<float> const& samples) {
    auto peak = 0.0f;
    for (auto sample : samples) {
        peak = juce::jmax(peak, std::abs(sample));
    }
    return peak;
}
}  // namespace

ClipTypeComponent::ClipTypeComponent(LevelMetersPack const& levelMetersPack) : mOscilloscopeBuffer(levelMetersPack.oscilloscopeBuffer) {
    mSnapshot.resize(processor::OscilloscopeBuffer::bufferSize, 0.0f);
    startTimerHz(gScopeRefreshHz);
}

ClipTypeComponent::~ClipTypeComponent() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void ClipTypeComponent::setClickCallback(std::function<void()> callback) {
    mClickCallback = std::move(callback);
}

void ClipTypeComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat().reduced(gBorderWidth);
    auto scopeBounds = bounds.reduced(6.0f, 6.0f);

    g.setColour(colourscheme::BackgroundPrimary.withAlpha(0.35f));
    g.fillRoundedRectangle(bounds, gBorderRadius);

    g.setColour(colourscheme::BackgroundTertiary.withAlpha(0.12f));
    for (int i = 1; i < 4; ++i) {
        auto const y = scopeBounds.getY() + scopeBounds.getHeight() * static_cast<float>(i) / 4.0f;
        g.drawHorizontalLine(static_cast<int>(std::round(y)), scopeBounds.getX(), scopeBounds.getRight());
    }
    for (int i = 1; i < 4; ++i) {
        auto const x = scopeBounds.getX() + scopeBounds.getWidth() * static_cast<float>(i) / 4.0f;
        g.drawVerticalLine(static_cast<int>(std::round(x)), scopeBounds.getY(), scopeBounds.getBottom());
    }

    auto const centreY = scopeBounds.getCentreY();
    g.setColour(colourscheme::TextFocusLevel3);
    g.drawHorizontalLine(static_cast<int>(std::round(centreY)), scopeBounds.getX(), scopeBounds.getRight());

    auto const peak = mSnapshotPeak;
    if (peak > 0.0005f) {
        auto const visibleSamples = juce::jmin<size_t>(mSnapshot.size(), gVisibleSamples);
        auto const start = mSnapshot.size() - visibleSamples;
        auto const gain = 0.9f / juce::jlimit(0.2f, 1.0f, peak);
        juce::Path waveform;

        for (size_t i = 0; i < visibleSamples; ++i) {
            auto const x = scopeBounds.getX() + (static_cast<float>(i) / static_cast<float>(visibleSamples - 1)) * scopeBounds.getWidth();
            auto const sample = juce::jlimit(-1.0f, 1.0f, mSnapshot[start + i] * gain);
            auto const y = centreY - (sample * scopeBounds.getHeight() * 0.46f);
            if (i == 0) {
                waveform.startNewSubPath(x, y);
            } else {
                waveform.lineTo(x, y);
            }
        }

        g.setColour(colourscheme::ForegroundTertiary);
        g.strokePath(waveform, juce::PathStrokeType(gWaveThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        auto const peakText = juce::String(juce::Decibels::gainToDecibels(peak, -60.0f), 0) + "dB";
        g.setFont(juce::Font(8.0f, juce::Font::bold));
        g.setColour(peak > 0.98f ? colourscheme::Warning : colourscheme::TextFocusLevel2);
        g.drawText(peakText, scopeBounds.toNearestInt().removeFromBottom(10), juce::Justification::bottomRight, true);
    } else {
        g.setFont(juce::Font(8.0f, juce::Font::bold));
        g.setColour(colourscheme::TextFocusLevel3);
        g.drawText("SCOPE", scopeBounds.toNearestInt(), juce::Justification::centred, true);
    }

    g.setColour(colourscheme::BackgroundTertiary.withAlpha(0.55f));
    g.drawRoundedRectangle(bounds, gBorderRadius, gBorderWidth);
}

void ClipTypeComponent::mouseDown(juce::MouseEvent const& event) {
    if (event.getNumberOfClicks() == 4) {
        std::string const lModalBoxTitle = std::string(ProjectInfo::projectName) + " by " + std::string(ProjectInfo::companyName);
        std::string const lModalBoxText = "Version: " + std::string(ProjectInfo::versionString);
        juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::InfoIcon, lModalBoxTitle, lModalBoxText, nullptr, nullptr);
        return;
    }

    if (event.mods.isLeftButtonDown() && mClickCallback) {
        mClickCallback();
    }
}

void ClipTypeComponent::timerCallback() {
    if (mOscilloscopeBuffer == nullptr) {
        return;
    }

    auto const sequence = mOscilloscopeBuffer->getSequence();
    if (sequence == mLastScopeSequence) {
        return;
    }

    mLastScopeSequence = sequence;
    mOscilloscopeBuffer->copySnapshot(mSnapshot);
    mSnapshotPeak = calculatePeak(mSnapshot);
    repaint();
}

}  // namespace pe::gui

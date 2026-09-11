#include "ClipMeter.h"

#include <BinaryData.h>
#include <cmath>
#include <cstdint>
#include <juce_graphics/juce_graphics.h>
#include <limits>
#include <utility>

#include "../../Parameters.h"
#include "../ColourScheme.h"
#include "../Utils.h"

namespace pe::gui
{
namespace
{
float calculateScopePeak (std::vector<float> const& samples)
{
    auto peak = 0.0f;
    for (size_t i = 0; i < samples.size(); i += 2)
    {
        auto const sample = samples[i];
        peak = juce::jmax (peak, std::abs (sample));
    }

    return peak;
}

size_t constexpr gVisibleScopeSamples = 384;
int constexpr gScopeRefreshHz = 60;
int constexpr gScopeSnapshotDivider = 2;
}

ClipMeter::ClipMeter (std::shared_ptr<juce::AudioProcessorValueTreeState> parameters,
                      LevelMetersPack const& levelMetersPack,
                      std::shared_ptr<Ticks> ticks)
    : juce::Component(), mTimer (std::bind (&ClipMeter::onTimerTick, this)), mParameters (parameters),
      mInputLevelMeter (levelMetersPack.inputLevelMeter), mClippingLevelMeter (levelMetersPack.clippingLevelMeter),
      mOutputLevelMeter (levelMetersPack.outputLevelMeter), mOscilloscopeBuffer (levelMetersPack.oscilloscopeBuffer),
      mBufferMaxSize (400), mTicks (ticks)
{
    for (int x = 0; x < mBufferMaxSize; x++)
    {
        mInputBuffer.push_back (-std::numeric_limits<float>::infinity());
        mClippingBuffer.push_back (-std::numeric_limits<float>::infinity());
        mOutputBuffer.push_back (-std::numeric_limits<float>::infinity());
    }
    mScopeSnapshot.resize (processor::OscilloscopeBuffer::bufferSize, 0.0f);
    mSubScopeSnapshot.resize (processor::OscilloscopeBuffer::bufferSize, 0.0f);
    mLowScopeSnapshot.resize (processor::OscilloscopeBuffer::bufferSize, 0.0f);
    mMidScopeSnapshot.resize (processor::OscilloscopeBuffer::bufferSize, 0.0f);
    mHighScopeSnapshot.resize (processor::OscilloscopeBuffer::bufferSize, 0.0f);
    mAirScopeSnapshot.resize (processor::OscilloscopeBuffer::bufferSize, 0.0f);
    mTimer.startTimerHz (gScopeRefreshHz);
}

ClipMeter::~ClipMeter() { setLookAndFeel (nullptr); }

void ClipMeter::setClickCallback(std::function<void()> callback)
{
    mClickCallback = std::move(callback);
}

void ClipMeter::paint (juce::Graphics& g)
{
    g.fillAll (colourscheme::BackgroundPrimary);
    drawTicks (mTicks->getTicksList(), colourscheme::TextFocusLevel3, g);
    drawScrollingOscilloscope (g);
    drawDbLine (*static_cast<juce::AudioParameterFloat*> (mParameters->getParameter (
                    pe::params::ParametersProvider::getInstance().getCeiling().getId().getParamID())),
                colourscheme::TextFocusLevel0,
                g);
    drawTicksTexts (mTicks->getTicksList(), colourscheme::TextFocusLevel3, g);
}

void ClipMeter::pushLevelHistory()
{
    mInputBuffer.pop_front();
    mInputBuffer.push_back (mInputLevelMeter->getDBFS());

    mClippingBuffer.pop_front();
    mClippingBuffer.push_back (mClippingLevelMeter->getDBFS());
}

void ClipMeter::updateScopeSnapshot()
{
    if (mOscilloscopeBuffer == nullptr)
        return;

    mOscilloscopeBuffer->copySnapshot (mScopeSnapshot);
    mOscilloscopeBuffer->copyBandSnapshots (mSubScopeSnapshot, mLowScopeSnapshot, mMidScopeSnapshot, mHighScopeSnapshot, mAirScopeSnapshot);
    mScopePeak = calculateScopePeak (mScopeSnapshot);
    mSubScopePeak = calculateScopePeak (mSubScopeSnapshot);
    mLowScopePeak = calculateScopePeak (mLowScopeSnapshot);
    mMidScopePeak = calculateScopePeak (mMidScopeSnapshot);
    mHighScopePeak = calculateScopePeak (mHighScopeSnapshot);
    mAirScopePeak = calculateScopePeak (mAirScopeSnapshot);
}

void ClipMeter::drawScrollingOscilloscope (juce::Graphics& g)
{
    auto const bounds = getLocalBounds().toFloat();
    auto const scopeBounds = bounds.reduced (2.0f, 1.0f);
    auto bandBounds = scopeBounds;
    auto const bandHeight = bandBounds.getHeight() / 5.0f;

    g.setColour (colourscheme::BackgroundTertiary.withAlpha (0.18f));
    for (int band = 1; band < 5; ++band)
        g.drawHorizontalLine (static_cast<int> (std::round (scopeBounds.getY() + bandHeight * static_cast<float> (band))),
                              scopeBounds.getX(), scopeBounds.getRight());

    if (mScopeSnapshot.empty() || mScopePeak <= 0.0005f)
        return;

    drawBandScope (g, bandBounds.removeFromTop (bandHeight).reduced (0.0f, 1.5f), mSubScopeSnapshot, mSubScopePeak,
                   juce::Colour::fromRGB (42, 112, 255), "SUB");
    drawBandScope (g, bandBounds.removeFromTop (bandHeight).reduced (0.0f, 1.5f), mLowScopeSnapshot, mLowScopePeak,
                   juce::Colour::fromRGB (42, 198, 255), "LOW");
    drawBandScope (g, bandBounds.removeFromTop (bandHeight).reduced (0.0f, 2.0f), mMidScopeSnapshot, mMidScopePeak,
                   juce::Colour::fromRGB (255, 150, 48), "MID");
    drawBandScope (g, bandBounds.removeFromTop (bandHeight).reduced (0.0f, 1.5f), mHighScopeSnapshot, mHighScopePeak,
                   juce::Colour::fromRGB (255, 55, 70), "HIGH");
    drawBandScope (g, bandBounds.reduced (0.0f, 1.5f), mAirScopeSnapshot, mAirScopePeak,
                   juce::Colour::fromRGB (255, 76, 214), "AIR");
}

void ClipMeter::drawBandScope (juce::Graphics& g, juce::Rectangle<float> bounds, std::vector<float> const& snapshot, float peak,
                               juce::Colour colour, juce::String const& label)
{
    auto const centreY = bounds.getCentreY();
    g.setColour (colour.withAlpha (0.12f));
    g.drawHorizontalLine (static_cast<int> (std::round (centreY)), bounds.getX(), bounds.getRight());

    g.setFont (juce::Font (8.5f, juce::Font::bold));
    g.setColour (colour.withAlpha (0.56f));
    g.drawText (label, bounds.toNearestInt().withTrimmedLeft (4).removeFromTop (14), juce::Justification::centredLeft, true);

    if (snapshot.empty() || peak <= 0.0005f)
        return;

    auto const visibleSamples = juce::jmin<size_t> (snapshot.size(), gVisibleScopeSamples);
    auto const start = snapshot.size() - visibleSamples;
    auto const gain = 0.86f / juce::jlimit (0.18f, 1.1f, peak);
    juce::Path waveform;
    juce::Path glow;
    auto const pixelStep = juce::jmax<size_t> (1, static_cast<size_t> (std::ceil (static_cast<float> (visibleSamples) /
                                                                                juce::jmax (24.0f, bounds.getWidth()))));

    for (size_t i = 0; i < visibleSamples; i += pixelStep)
    {
        auto const x = bounds.getX() + (static_cast<float> (i) / static_cast<float> (visibleSamples - 1)) * bounds.getWidth();
        auto const sample = juce::jlimit (-1.0f, 1.0f, snapshot[start + i] * gain);
        auto const y = centreY - (sample * bounds.getHeight() * 0.38f);
        if (i == 0)
        {
            waveform.startNewSubPath (x, y);
            glow.startNewSubPath (x, y);
        }
        else
        {
            waveform.lineTo (x, y);
            glow.lineTo (x, y);
        }
    }

    g.setColour (colour.withAlpha (0.20f));
    g.strokePath (glow, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (colour.withAlpha (0.95f));
    g.strokePath (waveform, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void ClipMeter::drawBuffer (std::deque<float>& buffer, juce::Colour const&& colour, juce::Graphics& g)
{
    drawBuffer (buffer, colour, g);
}

void ClipMeter::drawBuffer (std::deque<float>& buffer, juce::Colour const& colour, juce::Graphics& g)
{
    auto const bounds = getBounds();
    auto const width = static_cast<float> (bounds.getWidth());
    auto const height = static_cast<float> (bounds.getHeight());

    juce::Path p;

    g.setColour (colour);
    float offset = 0.0f;
    float offsetCoef = width / static_cast<float> (mBufferMaxSize);
    juce::Point<float> currentPoint { offset, height };
    p.startNewSubPath (currentPoint);
    for (auto& db : buffer)
    {
        juce::Point<float> nextPoint (offset, gDbToYPos (db, height, mTicks->isLinear()));
        p.lineTo (nextPoint);
        currentPoint = nextPoint;
        offset = offset + offsetCoef;
    }
    juce::Point<float> lastPoint (width, height);
    p.lineTo (lastPoint);
    p.closeSubPath();
    g.fillPath (p);
}

void ClipMeter::drawDbLine (float const& dB, juce::Colour const& colour, juce::Graphics& g)
{
    auto const bounds = getBounds();
    auto const width = static_cast<float> (bounds.getWidth());
    auto const height = static_cast<float> (bounds.getHeight());
    auto const yPos = gDbToYPos (dB, height, mTicks->isLinear());
    juce::Point<float> start (0.0f, yPos);
    juce::Point<float> end (width, yPos);
    juce::Line<float> line (start, end);
    g.setColour (colour);
    g.drawLine (line, 1.0f);
}

void ClipMeter::drawTicks (std::vector<float> const& ticksLevels, juce::Colour const&& colour, juce::Graphics& g)
{
    drawTicks (ticksLevels, colour, g);
}

void ClipMeter::drawTicks (std::vector<float> const& ticksLevels, juce::Colour const& colour, juce::Graphics& g)
{
    auto const bounds = getBounds();
    auto const height = static_cast<float> (bounds.getHeight());
    auto const tickWidth = static_cast<float> (bounds.getWidth());
    for (auto const& tickLevel : ticksLevels)
    {
        if (tickLevel == 0.0f)
        {
            continue; // Small hack to not draw first line and avoid akward artifact
        }
        auto const yPos = gDbToYPos (tickLevel, height, mTicks->isLinear());
        juce::Point<float> start (0.0f, yPos);
        juce::Point<float> end (tickWidth, yPos);
        juce::Line<float> line (start, end);
        g.setColour (colour);
        g.drawLine (line, 0.5f);
    }
}

void ClipMeter::drawTicksTexts (std::vector<float> const& ticksLevels, juce::Colour const& colour, juce::Graphics& g)
{
    auto const bounds = getBounds();
    auto const height = static_cast<float> (bounds.getHeight());
    auto const tickWidth = static_cast<float> (bounds.getWidth());
    juce::Colour textColor (colour);
    if (! isEnabled())
    {
        textColor = textColor.withAlpha (0.2f);
    }
    for (auto const& tickLevel : ticksLevels)
    {
        auto const yPos = static_cast<int> (gDbToYPos (tickLevel, height, mTicks->isLinear())) + 4;
        auto const fontSize = calculateTextSize (getTopLevelComponent()->getBounds().getWidth(),
                                                 getTopLevelComponent()->getBounds().getHeight());
        auto const textWidth = fontSize * 3;
        auto const textHeight = fontSize;
        g.setFont (juce::Font (static_cast<float> (fontSize)));
        g.setColour (textColor);
        std::string const dbStr = std::to_string (static_cast<int> (tickLevel)) + "dB";
        g.drawText (dbStr, 0, yPos, textWidth, textHeight, juce::Justification::left, true);
        g.drawText (dbStr,
                    static_cast<int> (tickWidth - textWidth),
                    yPos,
                    textWidth,
                    textHeight,
                    juce::Justification::right,
                    true);
    }
}

void ClipMeter::resized() {}

void ClipMeter::onTimerTick()
{
    pushLevelHistory();
    if (--mScopeSnapshotCountdown <= 0)
    {
        mScopeSnapshotCountdown = gScopeSnapshotDivider;
        updateScopeSnapshot();
    }
    repaint();
}

void ClipMeter::mouseDown (juce::MouseEvent const& event)
{
    if (event.mods.isRightButtonDown())
    {
        mTicks->switchToNextTicksList();
        return;
    }

    if (event.mods.isLeftButtonDown() && mClickCallback)
    {
        mClickCallback();
    }
}
} // namespace pe::gui

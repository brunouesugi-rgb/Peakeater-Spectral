#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <deque>
#include <functional>
#include <vector>

#include "../LevelMetersPack.h"
#include "../Ticks.h"

namespace pe::gui {
class ClipMeter : public juce::Component {
    class ClipMeterTimer : public juce::Timer {
       public:
        ClipMeterTimer(std::function<void()> callback) : mCallback(callback) {}

        void timerCallback() override {
            if (mCallback) {
                mCallback();
            }
        }

       private:
        std::function<void()> mCallback;
    };

   public:
    ClipMeter(std::shared_ptr<juce::AudioProcessorValueTreeState> parameters, LevelMetersPack const& levelMetersPack,
              std::shared_ptr<Ticks> ticks);
    ~ClipMeter() override;

    void setClickCallback(std::function<void()> callback);
    void paint(juce::Graphics& g) override;
    void resized() override;

   private:
    ClipMeterTimer mTimer;
    std::shared_ptr<juce::AudioProcessorValueTreeState> mParameters;
    std::shared_ptr<processor::LevelMeter<float>> mInputLevelMeter;
    std::shared_ptr<processor::LevelMeter<float>> mClippingLevelMeter;
    std::shared_ptr<processor::LevelMeter<float>> mOutputLevelMeter;
    std::shared_ptr<processor::OscilloscopeBuffer> mOscilloscopeBuffer;
    std::deque<float> mInputBuffer;
    std::deque<float> mClippingBuffer;
    std::deque<float> mOutputBuffer;
    std::function<void()> mClickCallback;
    std::vector<float> mScopeSnapshot;
    std::vector<float> mSubScopeSnapshot;
    std::vector<float> mLowScopeSnapshot;
    std::vector<float> mMidScopeSnapshot;
    std::vector<float> mHighScopeSnapshot;
    std::vector<float> mAirScopeSnapshot;
    float mScopePeak = 0.0f;
    float mSubScopePeak = 0.0f;
    float mLowScopePeak = 0.0f;
    float mMidScopePeak = 0.0f;
    float mHighScopePeak = 0.0f;
    float mAirScopePeak = 0.0f;
    int mScopeSnapshotCountdown = 0;
    int mBufferMaxSize;
    std::shared_ptr<Ticks> mTicks;

    void onTimerTick();

    void pushLevelHistory();
    void updateScopeSnapshot();
    void drawScrollingOscilloscope(juce::Graphics& g);
    void drawBandScope(juce::Graphics& g, juce::Rectangle<float> bounds, std::vector<float> const& snapshot, float peak,
                       juce::Colour colour, juce::String const& label);
    void drawBuffer(std::deque<float>& buffer, juce::Colour const& colour, juce::Graphics& g);
    void drawBuffer(std::deque<float>& buffer, juce::Colour const&& colour, juce::Graphics& g);
    void drawDbLine(float const& dB, juce::Colour const& colour, juce::Graphics& g);

    void drawTicks(std::vector<float> const& ticksLevels, juce::Colour const& colour, juce::Graphics& g);
    void drawTicks(std::vector<float> const& ticksLevels, juce::Colour const&& colour, juce::Graphics& g);
    void drawTicksTexts(std::vector<float> const& ticksLevels, juce::Colour const& colour, juce::Graphics& g);

    void mouseDown(juce::MouseEvent const& event) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipMeter)
};

}  // namespace pe::gui

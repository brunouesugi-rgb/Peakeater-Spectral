#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <utility>

#include "../LevelMetersPack.h"

namespace pe::gui {

class DynamicsStatsComponent : public juce::Component {
    class StatsTimer : public juce::Timer {
       public:
        explicit StatsTimer(std::function<void()> callback) : mCallback(std::move(callback)) {}

        void timerCallback() override {
            if (mCallback) {
                mCallback();
            }
        }

       private:
        std::function<void()> mCallback;
    };

   public:
    explicit DynamicsStatsComponent(LevelMetersPack const& levelMetersPack);

    void paint(juce::Graphics& g) override;

   private:
    StatsTimer mTimer;
    std::shared_ptr<processor::LevelMeter<float>> mOutputLevelMeter;
    std::shared_ptr<processor::DynamicsMeter> mDynamicsMeter;

    void onTimerTick();
    void drawStat(juce::Graphics& g, juce::Rectangle<int> bounds, juce::String const& label, float value, juce::String const& suffix);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynamicsStatsComponent)
};

}  // namespace pe::gui

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>
#include <functional>

#include "../../LevelMetersPack.h"

namespace pe
{
namespace gui
{
    class ClipTypeComponent
        : public juce::Component, private juce::Timer
    {
    public:
        explicit ClipTypeComponent (LevelMetersPack const& levelMetersPack);
        ~ClipTypeComponent() override;

        void setClickCallback (std::function<void()> callback);
        void paint (juce::Graphics& g) override;
        void mouseDown (juce::MouseEvent const& event) override;

    private:
        std::shared_ptr<processor::OscilloscopeBuffer> mOscilloscopeBuffer;
        std::vector<float> mSnapshot;
        std::function<void()> mClickCallback;
        float mSnapshotPeak = 0.0f;
        std::uint64_t mLastScopeSequence = 0;

        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipTypeComponent)
    };
} // namespace gui
} // namespace pe

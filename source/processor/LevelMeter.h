#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <tuple>

namespace pe::processor {

template <typename T>
class LevelMeter {
   public:
    LevelMeter();

    void updateLevels(juce::dsp::AudioBlock<T> const& audioBlock);

    std::atomic<T>& getDBFS();
    std::atomic<T>& getPeakDBFS();
    std::atomic<T>& getRmsDBFS();
    std::atomic<T>& getCrestFactorDB();

   private:
    std::atomic<T> dbfs;
    std::atomic<T> peakDbfs;
    std::atomic<T> rmsDbfs;
    std::atomic<T> crestFactorDb;
};

}  // namespace pe::processor

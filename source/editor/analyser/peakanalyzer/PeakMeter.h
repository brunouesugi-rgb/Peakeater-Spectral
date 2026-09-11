#pragma once

#include <vector>

namespace pe::gui {
class PeakMeter {
   public:
    PeakMeter(int const& sampleRate, float const& minValue);
    void push(float const& nextValue);
    float getMaxPeak();
    void reset();

   private:
    float mMinValue;
    int const mBufferSize;
    std::vector<float> mBuffer;
    int mWriteIndex = 0;
    float mMaxPeak = 0.0f;
};
}  // namespace pe::gui

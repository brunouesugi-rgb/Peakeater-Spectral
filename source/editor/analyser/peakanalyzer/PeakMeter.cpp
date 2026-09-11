#include "PeakMeter.h"

#include <algorithm>

namespace pe::gui {

namespace {
auto constexpr gLookBehindSeconds = 3;
}

PeakMeter::PeakMeter(int const& sampleRate, float const& minValue)
    : mMinValue(minValue),
      // 4 seconds of look behind
      mBufferSize(sampleRate * gLookBehindSeconds) {
    reset();
}

void PeakMeter::push(float const& nextValue) {
    if (mBuffer.empty()) {
        return;
    }

    auto const replacedValue = mBuffer[static_cast<size_t>(mWriteIndex)];
    mBuffer[static_cast<size_t>(mWriteIndex)] = nextValue;
    mWriteIndex = (mWriteIndex + 1) % mBufferSize;

    if (nextValue >= mMaxPeak) {
        mMaxPeak = nextValue;
    } else if (replacedValue >= mMaxPeak) {
        mMaxPeak = *std::max_element(mBuffer.begin(), mBuffer.end());
    }
}

float PeakMeter::getMaxPeak() { return mMaxPeak; }

void PeakMeter::reset() {
    mBuffer.assign(static_cast<size_t>(mBufferSize), mMinValue);
    mWriteIndex = 0;
    mMaxPeak = mMinValue;
}
}  // namespace pe::gui

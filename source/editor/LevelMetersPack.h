#pragma once

#include <memory>

#include "processor/DynamicsMeter.h"
#include "processor/LevelMeter.h"
#include "processor/OscilloscopeBuffer.h"

namespace pe::gui {
struct LevelMetersPack {
    std::shared_ptr<processor::LevelMeter<float>> inputLevelMeter;
    std::shared_ptr<processor::LevelMeter<float>> clippingLevelMeter;
    std::shared_ptr<processor::LevelMeter<float>> outputLevelMeter;
    std::shared_ptr<processor::DynamicsMeter> dynamicsMeter;
    std::shared_ptr<processor::OscilloscopeBuffer> oscilloscopeBuffer;
};
}  // namespace pe::gui

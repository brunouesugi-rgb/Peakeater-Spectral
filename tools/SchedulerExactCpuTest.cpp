#define LookaheadGainScheduler BaselineScheduler
#define ParallelAlignmentDelay BaselineAlignment
#include "../validation/cpu5-LookaheadGainScheduler.h.baseline"
#undef LookaheadGainScheduler
#undef ParallelAlignmentDelay
#include "../source/processor/LookaheadGainScheduler.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

int main() {
    using namespace pe::processor;
    std::vector<LookaheadGainScheduler::Frame> input(48000), expected(48000), actual(48000);
    uint32_t seed = 17;
    for (auto& frame : input) {
        for (auto& sample : frame) {
            seed = seed * 1664525u + 1013904223u;
            sample = static_cast<float>(static_cast<int32_t>(seed)) / 1073741824.0f;
        }
    }
    double oldTime = 0.0, newTime = 0.0;
    auto run = [&](auto& dsp, auto& output) {
        auto start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < input.size(); ++i) output[i] = dsp.process(input[i]);
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };
    for (auto rate : {8000.0, 44100.0, 48000.0, 96000.0, 192000.0}) {
        for (bool truePeak : {false, true}) {
            BaselineScheduler oldDsp;
            LookaheadGainScheduler newDsp;
            oldDsp.prepare(rate);
            newDsp.prepare(rate);
            oldDsp.configure(0.8f, 33.0f, truePeak, 0.7f);
            newDsp.configure(0.8f, 33.0f, truePeak, 0.7f);
            for (int repeat = 0; repeat < 4; ++repeat) {
                if (repeat % 2 == 0) {
                    oldTime += run(oldDsp, expected);
                    newTime += run(newDsp, actual);
                } else {
                    newTime += run(newDsp, actual);
                    oldTime += run(oldDsp, expected);
                }
                if (actual != expected) throw std::runtime_error("scheduler output mismatch");
            }
        }
    }
    std::cout << "exact_frames=1920000 baseline_seconds=" << oldTime
              << " candidate_seconds=" << newTime
              << " reduction_percent=" << (1.0 - newTime / oldTime) * 100.0 << '\n';
}

#define LookaheadGainScheduler BaselineScheduler
#define ParallelAlignmentDelay BaselineAlignment
#include "../validation/recovery-v1-scheduler.baseline.h"
#undef LookaheadGainScheduler
#undef ParallelAlignmentDelay
#include "../source/processor/LookaheadGainScheduler.h"
#include <iostream>
#include <stdexcept>

void check(bool value, char const* message) {
    if (!value) throw std::runtime_error(message);
}

double reconstructedPeak(std::vector<float> const& audio, int begin, int end) {
    double peak = 0.0;
    for (int i = begin; i < end; ++i) {
        for (int phase = 0; phase < 16; ++phase) {
            double sum = 0.0, sample = 0.0;
            for (int j = -64; j <= 64; ++j) {
                auto x = j - phase / 16.0;
                auto tap = (std::abs(x) < 1e-12 ? 1.0 : std::sin(3.141592653589793 * x) / (3.141592653589793 * x))
                    * (0.5 + 0.5 * std::cos(3.141592653589793 * x / 65.0));
                sum += tap;
                sample += audio[static_cast<size_t>(i + j)] * tap;
            }
            peak = std::max(peak, std::abs(sample / sum));
        }
    }
    return peak;
}

int main() {
    using namespace pe::processor;
    double recovery = 0.0;
    for (auto rate : {44100.0, 48000.0, 96000.0}) {
        for (int fixture = 0; fixture < 3; ++fixture) {
            for (float adaptive : {0.0f, 1.0f}) {
                BaselineScheduler baseline;
                LookaheadGainScheduler candidate;
                baseline.prepare(rate);
                candidate.prepare(rate);
                baseline.configure(0.8f, 80.0f, true, adaptive);
                candidate.configure(0.8f, 80.0f, true, adaptive);
                int count = static_cast<int>(rate * 0.25);
                std::vector<float> oldAudio(count), newAudio(count);
                double oldEnergy = 0.0, newEnergy = 0.0;
                for (int i = 0; i < count; ++i) {
                    double t = i / rate;
                    float input = fixture == 0 ? static_cast<float>((t >= 0.02 && t < 0.022 ? 1.6 : 0.25) * std::sin(6.283185307179586 * 997.0 * t))
                        : fixture == 1 ? static_cast<float>(1.5 * std::sin(6.283185307179586 * 50.0 * t))
                        : static_cast<float>(1.5 * std::sin(6.283185307179586 * 0.45 * i + 0.37));
                    auto oldFrame = baseline.process({input, -input});
                    auto newFrame = candidate.process({input, -input});
                    check(std::isfinite(newFrame[0]) && std::abs(newFrame[0]) <= 0.80001f, "sample ceiling");
                    check(newFrame[0] == -newFrame[1], "stereo link");
                    if (adaptive == 0.0f) check(oldFrame == newFrame, "adaptive off changed");
                    oldAudio[i] = oldFrame[0];
                    newAudio[i] = newFrame[0];
                    if (t > 0.035 && t < 0.10) {
                        oldEnergy += oldFrame[0] * oldFrame[0];
                        newEnergy += newFrame[0] * newFrame[0];
                    }
                }
                auto delta = 10.0 * std::log10(newEnergy / oldEnergy);
                auto peak = reconstructedPeak(newAudio, static_cast<int>(rate * 0.018), static_cast<int>(rate * 0.045));
                std::cout << "rate=" << rate << " fixture=" << fixture << " adaptive=" << adaptive
                          << " rms_delta_db=" << delta << " reconstructed_peak=" << peak << std::endl;
                check(peak <= 0.802, "reconstructed ceiling");
                if (fixture == 0 && adaptive == 1.0f) recovery += delta;
                if (fixture == 1) check(std::abs(delta) < 0.1, "low band changed excessively");
            }
        }
    }
    check(recovery / 3.0 > 0.1, "short peak recovery missing");
}

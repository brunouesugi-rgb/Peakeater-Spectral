# Bounded Lookahead Recovery v1 results

## Change

Spectral 3 High/Master/Ultra/Max: add a faster release candidate to the existing final scheduler, capped at 0.5 dB above its slow release and never above its sliding minimum. Existing average, detector, cycle-aware release and fixed latency remain. Low-band energy share suppresses recovery; existing Adaptive Release scales it and zero restores the old scheduler output. No upward compression, saturation, clipping, APVTS, preset, UI or dry-mix changes. Eco/Live bypass this scheduler as before.

Production edit: source/processor/LookaheadGainScheduler.h. Test registration: CMakeLists.txt. Paired test: tools/LookaheadRecoveryTest.cpp and frozen validation/recovery-v1-scheduler.baseline.h.

## Tests

- Before edit: paired test produced zero RMS recovery and failed its greater-than-0.1-dB short-peak recovery gate (recovery-v1-red.log).
- After edit: 44.1/48/96 kHz, short 997 Hz burst followed by a quieter tone, RMS measured 35-100 ms: +0.463524 / +0.463818 / +0.463663 dB versus baseline, Adaptive=1.
- 50 Hz fixture: less than 0.000725 dB RMS difference.
- Independent 16-phase, 129-tap output reconstruction during the tested event interval: maximum 0.797644 against ceiling 0.8. This is fixture evidence, not an unconditional True Peak guarantee.
- Adaptive=0: exact frame equality against the frozen scheduler in all paired fixtures.
- CTest LookaheadScheduler, LookaheadRecovery, ProcessorScheduler: 3/3 passed. Existing tests cover latency, dry, bypass, parallel mix and steady-tone reconstructed peaks.
- Release build passed: recovery-v1-build.log.
- pluginval 1.0.4, strictness 10, in-process, seed 12345: SUCCESS (recovery-v1-pluginval.log).

## Integrated processor and CPU

48 kHz, stereo, 128 samples, default Type, Input +12 dB, GUI closed: all six quality output captures equal the baseline. The final scheduler does not offer additional RMS recovery on this fixture. Do not advertise the isolated +0.46 dB as an overall music/master loudness improvement.

Two alternating baseline/candidate processor timing runs, affinity mask 4: median timings remain close (High approximately 0.585-0.589 ms, Master/Ultra/Max approximately 1.29-1.32 ms per block). No reliable whole-plugin CPU change is established. Raw measurements: recovery-v1-baseline*.csv and recovery-v1-candidate*.csv. Added arithmetic and an exponential every 16 scheduler samples have nonzero cost.

## Artifact and limitations

VST3 SHA256: ec8b92f2e43b92d1e355578cdd09359331d58856b4b758fac7ead41f8eb20740.

Built bundle: build/Release/peakeater_spectral_3_artefacts/Release/VST3/Peakeater Spectral 3.vst3.

Installed bundle unchanged. No Ableton listening, LUFS certification, real-session CPU measurement or ten-instance test performed. Faster recovery can alter modulation during repeated transients, despite the shallow cap and low-band guard. Future acceptance on real music needs level-matched listening and independent loudness/peak measurements. Earlier SchedulerExactCpuTest is a historical optimization comparison, not an equality gate for this intentional DSP change.

# Spectral 3 behavior-preserving CPU optimization

## Accepted changes

- Lookahead FIR samples are stored in a mirrored contiguous history. Each phase retains its original accumulation order.
- Audio history uses power-of-two capacity and masked indexing, with unchanged logical delay.
- Queue, gain history and parallel dry delay use bounded wrap instead of integer remainder.
- No filter taps, audio algorithms, quality levels, parameter IDs, presets, UI cadence or latency were changed.

## Five-area disposition

1. FIR history sharing: accepted.
2. Ring-buffer indexing: accepted.
3. Ceiling envelope branch calculation: tested, reverted because whole-processor timing did not establish benefit.
4. Ceiling safety redundant calculations: tested, reverted; both safety stages remain intact.
5. Spectral/limiter crossover loop separation: tested, reverted because whole-processor timing did not establish benefit.

## Evidence

- Release build: passed, cpu5-final-build.log.
- CTest LookaheadScheduler and ProcessorScheduler: 2/2 passed.
- Final processor fixture: cpu5-final.raw equals cpu5-baseline.raw byte for byte across all six qualities; 48 kHz stereo, 128 samples, input gain +12 dB, default Type.
- Paired scheduler driver: tools/SchedulerExactCpuTest.cpp. 1,920,000 stereo frames compared exactly across 8/44.1/48/96/192 kHz with True Peak on/off.
- Isolated scheduler timing: baseline 0.262024 s, candidate 0.0960569 s, 63.3404% reduction. This is NOT the whole plugin CPU reduction.
- Whole-processor repeated timing remains within noise; 20% overall reduction is NOT achieved or claimed. Intermediate cpu5-fixed-* files include the subsequently rejected crossover/ceiling experiments.
- pluginval 1.0.4 strictness 10, in-process, seed 12345: SUCCESS, cpu5-pluginval.log.
- Final VST3 binary SHA256: 94def03a32fef8f8667bc0a170fd831e9455d4846278ea35c6efd6004cae9546.

## Limitations

No Ableton session, listening test, ten-instance host CPU measurement or installed-VST3 replacement was performed. Exact-output fixtures are strong regression evidence, not exhaustive proof for all possible signals and automation. Baseline executable and source snapshots remain under build/Release and validation for reproducibility.

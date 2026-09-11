# Lookahead Gain Scheduler + Cycle-Aware Release v1

Implemented for Spectral 3 only. APVTS IDs, Type order, state serialization and
presets were not edited. No installation was performed.

## DSP

- High/Master/Ultra/Max use rounded 5 ms latency: 221 samples at 44.1 kHz,
  240 at 48 kHz, 480 at 96 kHz. Eco/Live bypass this new processing.
- Stereo-linked gain scheduling uses a sliding minimum followed by a moving
  average. Buffers are allocated in prepare, not in the audio callback.
- True Peak ON enables three fractional FIR phases plus the original samples
  (4x detector). Audio itself is not oversampled. Detector interpolation is
  33-tap windowed sinc, with a 0.05 dB internal allowance in addition to the
  user's True Peak margin.
- Cycle estimation uses per-channel lowpass energy and stable positive-going
  crossings in the 20-250 Hz range. The stronger low-band channel controls the
  shared release, including anti-phase stereo signals. Low confidence returns
  toward the base Release; Adaptive Release scales the correction.
- The period floor is two cycles, bounded to four times the selected Release.
  This chiefly affects short Release settings on sustained bass.
- HQ Output gain is applied before the scheduler; aligned Dry has its own
  corresponding output gain stage before mixing. Fully Dry remains unprocessed
  apart from the approved alignment delay. No new limiter is after parallel mix.
- Existing upstream clip/contour processing remains. This stage cannot undo
  distortion already produced there.

## Host Integration

Host latency is set during prepare. A 50 ms message-thread timer observes Quality
changes, reports latency, and publishes the selected mode to the audio thread.
No host notification is made directly by the new audio processing loop.
Quality transitions reset scheduler history; seamless live PDC switching is not
claimed. Select Quality before playback for critical renders.

## Verified

- Release VST3 build: passed.
- CTest LookaheadScheduler: passed at 44.1/48/96 kHz.
- Impulse delay and tail, stereo-linked gain, cycle confidence, release floor,
  reset, and Dry/Wet alignment: passed.
- Independent 16-phase, 129-tap reconstruction on steady overloaded tones:
  peak <= 0.795408 for ceiling 0.8 across tested rates and frequencies
  (50 Hz, 997 Hz, Fs/4, 0.45 Fs).
- CTest ProcessorScheduler uses the product shared-code library: passed all
  six Quality settings, Dry integrity, host bypass delay, finite output,
  and linear 50% mix. Maximum mix error was zero on this fixture.
- 64/128 sample block-size comparison on the integration fixture: zero difference.
- Final built VST3: pluginval 1.0.4 strictness 10, seed 12345, SUCCESS.
- Log: tools/pluginval-lookahead-cycle-v1.log.
- SHA256: 20b6bc24905bc9fbac987805600262107d87178149c56419c54ef6ab398e0333.

## Not Claimed

The finite detector and synthetic tests are not BS.1770 certification or a
universal continuous-time peak guarantee. Real music, abrupt gain automation,
Quality/PDC transitions in Ableton, long-duration sessions, CPU A/B, and
level-matched listening remain to be evaluated. No measured RMS/LUFS improvement
or CPU percentage claim is made. Existing GR telemetry does not separately
display this new scheduler's gain reduction.

## Files

- source/processor/LookaheadGainScheduler.h
- source/PluginProcessor.h
- source/PluginProcessor.cpp
- tools/LookaheadSchedulerTest.cpp
- tools/ProcessorSchedulerTest.cpp
- CMakeLists.txt

The initial host-test target hit Windows path-length limits. Renaming only that
target to pe_host_test resolved the build issue.

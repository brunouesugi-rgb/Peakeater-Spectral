# Peakeater Spectral 3 release notes

## Development release

- Spectral 3 Type-specific loudness distribution and peak budget processing.
- Drive range capped at +24 dB for the Spectral 3 build.
- Ceiling contour recovery for additional RMS density without increasing the
  configured final clip amount.
- True Peak and lookahead safety paths retained.
- GPL-3.0 source distribution derived from PeakEater.

## Validation

- Windows Release build completed.
- LookaheadScheduler, LookaheadRecovery, and ProcessorScheduler tests passed.
- Windows VST3 passed `pluginval --strictness-level 10`.
- Installed VST3 bundle hash matched the built bundle during local validation.

Listening tests with full user projects, Ableton Live, and REAPER remain part of
the release checklist for each tagged build.

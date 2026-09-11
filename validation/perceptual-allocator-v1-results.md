# Perceptual Constrained Gain Allocator v1

Scope: Spectral 3 experimental implementation only. No APVTS IDs, Type order,
preset serialization, or Dry/Wet changes. Not installed.

## Verification

- Release VST3 build: passed.
- Built VST3 pluginval 1.0.4 strictness 10, seed 12345: SUCCESS.
- Log: tools/pluginval-perceptual-allocator-v1.log.
- VST3 binary SHA256: 09baea46ff9849a465572b1750c335f04847402f81f92a6b0a0dc5ec2e9e1a13.
- Benchmark rebuilt with both Spectral 2 and Spectral 3 product defines.
- Allocator A/B, adaptive descriptors, shared stereo loudness, and Tone controls
  regression scripts passed with the matching benchmark.

## Synthetic A/B

Multiplier 16, default benchmark signal, allocator enabled versus disabled:

- RMS gain: +0.025617 dB.
- Sample peak change: +0.026224 dB.
- Candidate sample peak: -1.508378 dBFS.
- Internal average clip metric change: -0.094783 dB.
- Descriptor-cadence allocator updates: 65291.

These are not perceptual listening results or LUFS measurements. The clip metric
is internal DSP telemetry, not an independently measured distortion score.
The small crest increase is within the test tolerance, not a demonstrated crest
improvement. The earlier shared-stereo failure used mismatched benchmark defines;
the product-matching test passed without changing its threshold.

## Remaining Gates

No CPU A/B, long-duration DAW test, real-material listening test, or independent
True Peak compliance measurement was completed. The benchmark has True Peak
limiting disabled. Existing shared stereo recovery is after the ceiling and
sample-peak bounded; its additional gain must not be represented as a proven
True Peak-safe improvement. Validate reconstructed output peaks before deployment.

The allocator is a bounded heuristic using existing envelope descriptors, not
a general psychoacoustic optimization solver. Eco bypasses the added allocator;
no measured percentage CPU claim is made. Installation remains pending.

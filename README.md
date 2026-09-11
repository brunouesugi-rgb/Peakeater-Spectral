# Peakeater Spectral

Peakeater Spectral is a GPL-3.0 spectral limiter/maximizer by AXLRTR Audio
Lab. It is a modified derivative of the open-source PeakEater project. The
original project remains the upstream foundation; this repository contains
additional spectral processing, Type-specific loudness control, transient and
low-band protection, True Peak safety, and performance work.

This is an independent community modification and is not affiliated with the
original PeakEater authors unless explicitly stated in the contributor history.

The current public-alpha direction is simple: keep the immediacy of PeakEater,
then add the judgment tools expected from a modern maximizer.

## Features

- Drive-first limiter workflow with the original `Threshold` parameter ID kept
  for project compatibility.
- Type-aware processing for EDM, Hip Hop, Drums, One Shot, One Shot Clean,
  Acoustic, Vocal, Bass, Bright, Glue, Clean, Percs, Dubstep, DrumNBass,
  House, Trap, and 808&Kick.
- 12-band limiter stage plus 32-bin spectral control bank, scaled internally by
  the Quality setting while keeping realtime CPU under control.
- Final Ceiling protection, Detector HP, Saturation, Tone, Tone Mode, Attack,
  Hold, Release, Transient Recovery, Lookahead, Output, and Dry/Wet.
- Output judgment panel with Input Peak, Output Peak, True Peak estimate,
  LUFS-S estimate, RMS, Crest Factor, Gain Reduction, and Clip Amount.
- Small realtime oscilloscope for checking waveform shape after processing.
- Windows VST3 build. Other formats remain available where the
  selected CMake configuration supports them.

## Status

Peakeater Spectral is published as an open-source development release. It has passed a
Release build, project regression tests, and `pluginval` strictness 10 for the
Windows VST3 artifact. Ableton and REAPER listening tests should still be done
with the exact release material before production use.

Release archives are published from the GitHub Releases page for tagged builds.

## Installation

### Windows VST3

Copy the VST3 bundle to:

```text
C:\Program Files\Common Files\VST3
```

The release build produces:

```text
build/Release/peakeater_spectral_3_artefacts/Release/VST3/Peakeater Spectral.vst3
```

### macOS

Install the format you need into the standard user or system audio plugin
folder:

```text
/Library/Audio/Plug-Ins/VST3
/Library/Audio/Plug-Ins/Components
/Library/Audio/Plug-Ins/CLAP
/Library/Audio/Plug-Ins/LV2
```

### Linux

Copy the desired format to one of the standard plugin folders:

```text
~/.vst3
~/.clap
~/.lv2
```

## Building

This project uses CMake, Conan, JUCE, and the original PeakEater build layout.
On Windows, the local profile is recommended:

```powershell
$env:Path = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;' + $env:Path
.\.venv\Scripts\conan.exe build . -pr:h config/conan/windows-local.jinja -pr:b config/conan/windows-local.jinja
```

Validate the VST3 with pluginval:

```powershell
..\tools\pluginval\v1.0.4\pluginval.exe --strictness-level 10 --verbose --validate-in-process "build\Release\peakeater_spectral_artefacts\Release\VST3\Peakeater Spectral Beta.vst3"
```

## Design Notes

The main DSP chain is:

```text
Drive -> 12-band limiter -> 32-bin spectral bank -> Final Ceiling
```

Quality modes increase analysis depth and tone behavior, but public builds must
stay practical on midrange CPUs. The current reference machine is an i5-13400.

The UI goal is not to imitate any commercial plugin directly. The goal is a
clear mastering workflow: push loudness with Drive, protect the output with
Ceiling, choose a Type for material, shape tone deliberately, then confirm the
result with meters.

## Upstream, modifications, and license

Peakeater Spectral is distributed under the GPL-3.0 License. See
`LICENSE.md` for details.

This project derives from the open-source PeakEater project. Preserve upstream
notices and clearly mark modified versions when redistributing them. Do not imply
that PeakEater authors endorse this modification.

When distributing binaries, include this source repository or an equivalent
offer for the corresponding GPL-3.0 source.

## Download

Windows VST3 release archives are provided on the GitHub Releases page when a
tagged release is published. Source code remains available in this repository.

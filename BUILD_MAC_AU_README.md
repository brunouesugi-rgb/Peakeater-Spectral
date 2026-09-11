# Peakeater Spectral macOS AU Build Package

This package is prepared on Windows for building Peakeater Spectral as a macOS Audio Unit on a Mac.

## Important

- Windows cannot produce a real macOS `.component` AU binary.
- Build this package on macOS with Xcode command line tools installed.
- The copied `CMakeLists.txt` changes only `peakeater_spectral_2` to `FORMATS AU VST3 Standalone`.
- APVTS IDs, Type order, and source files are otherwise kept as-is from the current working copy.

## Build on Mac

```bash
cd Peakeater_Spectral_2_macOS_AU_source_YYYYMMDD-HHMMSS
cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release
cmake --build build-mac --config Release --target peakeater_spectral_2_AU
```

Expected AU output is a `.component` bundle under the build artifacts folder, commonly similar to:

```text
build-mac/peakeater_spectral_2_artefacts/Release/AU/Peakeater Spectral.component
```

## Install on Mac

Copy the built component to:

```text
~/Library/Audio/Plug-Ins/Components/
```

Then rescan plugins in the DAW.

## Validation

If pluginval is available on Mac:

```bash
pluginval --strictness-level 10 --verbose --validate-in-process "path/to/Peakeater Spectral.component"
```

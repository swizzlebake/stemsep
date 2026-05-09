# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bat
rem Configure (first time or after CMakeLists.txt changes)
cmake -B Builds -G "Visual Studio 18 2026" -A x64

rem Build
cmake --build Builds --config Release
```

Output: `Builds\StemSep_artefacts\Release\VST3\StemSep.vst3`

**Deploy to DAW** — copy the artifact to the system VST3 folder (requires admin):
```bat
robocopy "Builds\StemSep_artefacts\Release\VST3\StemSep.vst3" "C:\Program Files\Common Files\VST3\StemSep.vst3" /E /IS /IT
```

Then rescan plugins in Ableton Live 12 Suite.

## Tests

```bat
rem Build tests
cmake --build Builds --config Release --target StemSepTests

rem Run all tests
Builds\Release\StemSepTests.exe

rem Run by tag
Builds\Release\StemSepTests.exe [dsp]
Builds\Release\StemSepTests.exe [processor]

rem Run a single test by name
Builds\Release\StemSepTests.exe "BandFilter: BPF passes centre frequency at unity"
```

Tags: `[dsp]` for `BandFilterTests.cpp`, `[processor]` for `PluginProcessorTests.cpp`.

## Architecture

StemSep is a single-input 5-way bandpass splitter. One stereo input is split into 5 overlapping frequency bands (Drums/Bass/Guitar/Vocals/Other), each processed by a BPF and individually levelled, then summed back to the stereo output.

### Signal flow

```
Stereo In → inputCopy_ (saved before output is cleared)
           → for each enabled stem:
               BandFilter.prepareCoefficients(freq, Q, gainDB)
               BandFilter.processBlock(inL, inR, outL, outR)   // accumulates +=
           → Stereo Out (sum of all enabled stems)
```

`inputCopy_` exists because JUCE shares input/output buffers in-place — the input must be saved before `outputBuf.clear()`.

### Parameter layout

`NUM_STEMS = 5` drives the entire parameter set. IDs are indexed: `freq0`–`freq4`, `q0`–`q4`, `gain0`–`gain4`, `enable0`–`enable4`. Cached as `std::atomic<float>*` arrays for lock-free audio-thread reads.

### DSP — `BandFilter`

Implements the Audio EQ Cookbook constant-0dB-peak bandpass biquad (Direct Form I). Each call to `prepareCoefficients()` writes *target* coefficients (`tb0`, `tb1`, `tb2`, `ta1`, `ta2`, `tGainLin_`); `processBlock()` linearly interpolates the live coefficients toward the targets sample-by-sample to prevent clicks. `processBlock()` **accumulates** (`+=`) into the caller's output buffers — the processor owns clearing.

`getMagnitudeForFrequency()` evaluates the transfer function using the *target* (not current) coefficients, so the `FrequencyDisplay` previews changes instantly before they're audible.

### UI

`FrequencyDisplay` polls `getMagnitudeResponses()` at 30 Hz (JUCE Timer), drawing 512 log-spaced curves (20 Hz–20 kHz) per stem plus a summed white curve. Each `StemStrip` owns APVTS `SliderAttachment`/`ButtonAttachment` objects that keep the knobs in sync with parameters automatically.

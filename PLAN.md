# StimSep — VST3 Multi-Stem Band Mixer Plugin

## Context
StimSep is a VST3 plugin that acts as a spectral stem mixer. A single plugin instance accepts up to 4 stereo audio stems via VST3 auxiliary input buses. Each stem has an independent parametric bell-curve filter (peaking EQ biquad), letting users boost the frequency range of interest and attenuate everything else. All filtered stems sum to one stereo output, giving control over "how much of each track contributes to the overall sound picture" at a given frequency band.

---

## Tech Stack
- **Framework**: JUCE 8 (via CMake `FetchContent`)
- **Build**: CMake 3.22+, Visual Studio 2022 (x64)
- **Format**: VST3 only, Windows
- **Language**: C++17
- **Testing DAW**: Ableton Live 12 Suite

---

## File Structure
```
StimSep/
├── CMakeLists.txt
├── PLAN.md
├── Source/
│   ├── PluginProcessor.h / .cpp
│   ├── PluginEditor.h / .cpp
│   ├── DSP/
│   │   ├── BandFilter.h / .cpp
│   └── UI/
│       ├── FrequencyDisplay.h / .cpp
│       └── StemStrip.h / .cpp
```

---

## Architecture

### Plugin Bus Layout (PluginProcessor constructor)
```cpp
AudioProcessor(BusesProperties()
    .withInput ("Stem 1", stereo, true)   // main input, always enabled
    .withInput ("Stem 2", stereo, false)  // aux, optional
    .withInput ("Stem 3", stereo, false)
    .withInput ("Stem 4", stereo, false)
    .withOutput("Output",  stereo, true))
```
`isBusesLayoutSupported` accepts any combination where enabled buses are stereo.

### Parameters (via APVTS)
For each stem `i` in 0–3:
- `freq_i`: 20–20,000 Hz, log skew, default 1000 Hz
- `q_i`: 0.1–10, default 1.0
- `gain_i`: −48 to +24 dB, default 0 dB
- `enable_i`: bool, default true

Raw `std::atomic<float>*` pointers cached in constructor for lock-free reads in `processBlock`.

### processBlock Flow
```
zero outL/outR
for each stem:
  if disabled → skip
  call BandFilter::prepareCoefficients(freq, Q, gain)
  getBusBuffer(buffer, true, stemIdx) → inL/inR
  BandFilter::processBlock(inL, inR, outL, outR, numSamples)  // accumulates
```

---

## DSP: BandFilter (Audio EQ Cookbook Peaking EQ)

**Coefficient formula** (normalised by a0):
```
A      = pow(10, dBgain / 40)
w0     = 2π × f0 / Fs
alpha  = sin(w0) / (2 × Q)
a0_inv = 1 / (1 + alpha/A)

b0 = (1 + alpha×A) × a0_inv
b1 = (−2×cos(w0))  × a0_inv
b2 = (1 − alpha×A) × a0_inv
a1 = (−2×cos(w0))  × a0_inv
a2 = (1 − alpha/A) × a0_inv
```
Use `double` for coefficient computation; clamp `freqHz` to `[20, Fs/2−1]`.

**Direct Form I per sample:**
```
y[n] = b0·x[n] + b1·x[n-1] + b2·x[n-2] − a1·y[n-1] − a2·y[n-2]
outL[n] += y   // accumulate, not overwrite
```

**Smooth parameter changes** — linear interpolation from current → target coefficients across the block length. Eliminates zipper noise at no meaningful CPU cost.

**Frequency response for visualiser:**
```cpp
w = 2π·f/Fs
H = (b0 + b1·cos(w) + b2·cos(2w) − j(b1·sin(w) + b2·sin(2w)))
  / (1  + a1·cos(w) + a2·cos(2w) − j(a1·sin(w) + a2·sin(2w)))
magnitude = |H|   // return as float
```

---

## UI Components

### FrequencyDisplay
- Repaints at 30 Hz via `juce::Timer`
- 512 log-spaced frequency points (20–20 kHz)
- Draws each stem's response curve in its own colour
- Combined response = sum of enabled stem magnitudes (linear), converted to dB
- `freqToX`: log scale mapping; `dBToY`: linear dB mapping (range −48 to +24)

### StemStrip (one per stem)
- Freq knob, Q knob, gain slider, enable toggle — all wired via APVTS `SliderAttachment` / `ButtonAttachment`
- Coloured label indicator matching the curve colour in FrequencyDisplay

### PluginEditor Layout
```
┌────────────────────────────────────┐
│         FrequencyDisplay           │  ~300px tall
├────────────────────────────────────┤
│  Stem 1 strip  (freq | Q | gain |✓)│
│  Stem 2 strip                      │
│  Stem 3 strip                      │
│  Stem 4 strip                      │
└────────────────────────────────────┘
  Total: 800 × 600 px
```

---

## Building

```bat
cmake -B Builds -G "Visual Studio 17 2022" -A x64
cmake --build Builds --config Release
```

Output: `Builds\StimSep_artefacts\Release\VST3\StimSep.vst3`

Install to `C:\Program Files\Common Files\VST3\`, then re-scan in Ableton Live.

---

## Ableton Live 12 Routing (4 stems → plugin)

1. Create 4 audio tracks, load one stem per track
2. Create a 5th track — insert StimSep as an audio effect
3. Stem 1 (main bus): route track 1's output to track 5's input
4. Stems 2–4 (aux buses): expand the plugin device and set each sidechain input source to the corresponding track
5. The plugin output is track 5's audio

> VST3 aux buses appear as sidechain inputs in Live 12's device panel. Live 12 Suite supports multiple sidechain inputs per VST3 device.

---

## Functional Checks
- Disable a stem → disappears from output
- Gain −48 dB → inaudible
- Sweep frequency knob → audible sweep
- Q 0.1 vs 10 → audible bandwidth difference
- Rapid knob movement → no clicks (smooth interpolation)
- Save/reopen project → parameters restored (state serialisation)

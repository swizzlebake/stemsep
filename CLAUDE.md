# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

### Windows (Ableton Live)

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

**Python runtime for Separate / Tab generation** — install Python 3.10+ from python.org or the Microsoft Store and tick *Add python.exe to PATH* during install. Then from a regular `cmd.exe`:

```bat
py -m pip install --upgrade pip
py -m pip install demucs soundfile julius
py -m pip install librosa             rem monophonic Tab generation
py -m pip install basic-pitch         rem polyphonic Tab generation (~500MB incl. TF Lite)
```

The plugin spawns `python` from the DAW's `PATH`. If Ableton was launched before Python was installed, restart Ableton so it picks up the updated `PATH`. There is no per-project venv on Windows — the `py` launcher resolves the registered interpreter.

**System-audio capture on Windows** — works out of the box via WASAPI loopback on the *default render endpoint*. No driver install, no Stereo Mix, no ffmpeg. The recorded WAV is written as 32-bit float at the device's mix rate (typically 48 kHz) to `%USERPROFILE%\Music\StemSep_captures\`. To change which device is captured, change the default playback device in Sound Settings before clicking Capture.

### Linux (Reaper)

Prerequisites (Ubuntu/Debian):

Build dependencies:
```bash
sudo apt install build-essential pkg-config cmake ninja-build python3-venv \
    libasound2-dev libjack-jackd2-dev \
    libfreetype-dev libfontconfig1-dev \
    libx11-dev libxext-dev libxrender-dev libxcomposite-dev \
    libxcursor-dev libxinerama-dev libxrandr-dev \
    libglu1-mesa-dev mesa-common-dev \
    ffmpeg pulseaudio-utils
```

Demucs runtime — install into a venv (Ubuntu 24.04+ blocks `pip install --user` system-wide due to PEP 668):
```bash
python3 -m venv ~/.venvs/stemsep
~/.venvs/stemsep/bin/pip install --upgrade pip
~/.venvs/stemsep/bin/pip install demucs soundfile julius
```

For the **Tab generation** feature (per-stem ASCII tab from Bass / Guitar):

```bash
~/.venvs/stemsep/bin/pip install librosa             # monophonic tab (pYIN)
~/.venvs/stemsep/bin/pip install basic-pitch         # polyphonic tab — optional, ~500MB incl. TF Lite
```

If `basic-pitch` is missing, the plugin surfaces a clear error pointing back to this command when the user picks the polyphonic mode. Monophonic mode only requires `librosa`.

The plugin spawns `python3` from `PATH`, so Reaper must be launched with the venv's `bin/` on `PATH` for separation to work. Use `./run-reaper.sh` (described below under **Run**) — it handles this automatically.

Build and deploy in one step (configures on first run, builds, symlinks to `~/.vst3`):
```bash
./build-linux.sh
```

Or run the steps manually:
```bash
cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build Builds
ln -sfn "$PWD/Builds/StemSep_artefacts/Release/VST3/StemSep.vst3" ~/.vst3/StemSep.vst3
```

Output: `Builds/StemSep_artefacts/Release/VST3/StemSep.vst3`. Reaper scans `~/.vst3` by default.

After deploy, in Reaper: *Options → Preferences → Plug-ins → VST → Re-scan*.

**Run Reaper with the demucs venv on PATH:**
```bash
./run-reaper.sh
```

The script defaults to venv `~/.venvs/stemsep` and the Reaper binary at `~/reaper_linux_x86_64/REAPER/reaper`. Override with `STEMSEP_VENV=/path REAPER=/path ./run-reaper.sh`. It verifies `demucs` and `soundfile` are importable before launching.

Launching Reaper from the desktop launcher will *not* find demucs unless the venv's `bin/` is added to the desktop session's `PATH` (e.g. via `~/.profile`).

## Multi-output routing

The plugin exposes 6 output buses: `Main` (stereo sum, on by default) plus `Drums`, `Bass`, `Guitar`, `Vocals`, `Other` (each stereo, off by default). Both BPF and Demucs modes write the per-stem audio to the matching stem bus *and* to `Main` simultaneously, so users who don't enable the stem buses get the original single-output behaviour.

### Reaper

The cleanest workflow uses one **source track** carrying StemSep + 12 channels, plus one **child track per stem** that pulls a specific channel pair from the source.

```
  +-------------------------------------------------------+
  |  Source track (12 channels)                           |
  |                                                       |
  |  audio item --> [ StemSep VST ]                       |
  |                                                       |
  |  Pin Connector (Reaper default mapping):              |
  |    plugin out 1/2   -> track ch 1/2    (Main mix)     |
  |    plugin out 3/4   -> track ch 3/4    (Drums)        |
  |    plugin out 5/6   -> track ch 5/6    (Bass)         |
  |    plugin out 7/8   -> track ch 7/8    (Guitar)       |
  |    plugin out 9/10  -> track ch 9/10   (Vocals)       |
  |    plugin out 11/12 -> track ch 11/12  (Other)        |
  |                                                       |
  |  Master/parent send: OFF  <- avoids duplicate 1/2     |
  +-------------------------------------------------------+
        |        |        |        |         |
      ch 3/4   ch 5/6   ch 7/8   ch 9/10   ch 11/12
        |        |        |        |         |
        v        v        v        v         v
    +------+ +------+ +------+ +------+ +------+
    |Drums | |Bass  | |Guitar| |Vocals| |Other |   <- child tracks
    |      | |      | |      | |      | |      |      each with a
    | rcv  | | rcv  | | rcv  | | rcv  | | rcv  |      Track Receive
    | 3/4  | | 5/6  | | 7/8  | | 9/10 | |11/12 |      from source,
    | ->1/2| | ->1/2| | ->1/2| | ->1/2| | ->1/2|      mapping that
    +------+ +------+ +------+ +------+ +------+      pair to 1/2.
        |        |        |        |         |
        +--------+--------+--------+---------+
                          |
                          v
                     master mix
```

#### 1. Set up the source track

1. Drop your audio item onto a new track (call it `StemSep`).
2. Set the track's channel count to **12** (gives the plugin somewhere to write 6 stereo buses). Easiest path: **right-click the track header → Track channels → 12**. Alternative: click the small **Route** button on the TCP (next to the I/O area; shows a small arrows icon) to open the routing dialog, and change *Track channels* there.
3. Insert StemSep on this track's FX chain.
4. In the FX chain, click the **2 in / 2 out** button next to the StemSep entry — that opens the **Pin Connector**.
5. Switch to the *Audio outputs* tab. You'll see 12 plugin output channels on the left (1–2 = Main, 3–4 = Drums, 5–6 = Bass, 7–8 = Guitar, 9–10 = Vocals, 11–12 = Other) and 12 track channels on the right.
6. Reaper's default mapping (plugin out N → track ch N) is already what you want, but verify each plugin output 3–12 has a dot on the matching track channel. Close the Pin Connector.

At this point the source track has all 6 stems available on its channels 1–12. The default *Master/parent send* still mixes channels 1–2 only, so playback sounds normal.

#### 2. Add a child track for each stem

For each stem (Drums, Bass, Guitar, Vocals, Other):

1. Create a new track named after the stem.
2. Open the **child track's** I/O dialog → click **Add new receive…** → pick the `StemSep` source track.
3. In the receive row that appears, change the channel mapping:
   - Drums child: source `3/4` → receive `1/2`
   - Bass child: source `5/6` → receive `1/2`
   - Guitar child: source `7/8` → receive `1/2`
   - Vocals child: source `9/10` → receive `1/2`
   - Other child: source `11/12` → receive `1/2`
4. The dropdown labelled `Audio: 1/2 → 1/2` is where you change source channels — click it, choose `Multichannel source → 2 channels → 3/4` (etc).

Each child now has one isolated stem on its stereo bus and you can record-arm, EQ, route to busses, or freeze them independently.

#### 3. Avoid double output

You're now hearing the same audio twice — once via the source track's Main bus (channels 1–2) and once via the children. Two ways to fix it:

- **Mute the source track's master send**: open its I/O, untick *Master/parent send*. The source track keeps producing channels 3–12 for the children but stops contributing to the master mix on 1–2.
- **Or disable StemSep's Main bus in the Pin Connector**: clear the dots on plugin outputs 1–2.

The first option is reversible with one click, so it's usually preferred.

#### 4. Automate it

The repo ships a ReaScript that builds the entire setup in one action — `scripts/StemSep_setup.lua`. It inserts the source track + 5 child tracks, sets the channel count, loads StemSep, configures all 5 receives with the right channel pairs, and disables the source's master send.

**Install** once (note: use absolute paths, not `$PWD`, to avoid bad symlinks if the working directory differs):
```bash
ln -s /absolute/path/to/stemsep/scripts/StemSep_setup.lua ~/.config/REAPER/Scripts/
```
(Windows: copy the file to `%APPDATA%\REAPER\Scripts\`.)

**Register it in Reaper** — scripts in the Scripts folder are *not* auto-discovered, you have to load each one once:
1. `Actions → Show action list…` (default shortcut: `?`).
2. Click **ReaScript: Load…** at the bottom of the dialog (or **New action → Load ReaScript** depending on Reaper version).
3. Browse to `~/.config/REAPER/Scripts/StemSep_setup.lua` and open it.
4. The action list now has **Script: StemSep_setup.lua** — search "StemSep" to find it. Bind a shortcut via the **Add…** button if you want one.

Run the action and a 6-track stack appears at the bottom of the project. Drop your audio item onto the `StemSep` track and you're ready.

If you'd rather use a track template instead: select all 6 tracks (the source plus the 5 children) → right-click → *Save tracks as track template…*. The .RTrackTemplate captures channel counts, FX state, and all sends/receives.

#### 5. If the children show meters but you can't hear anything

The source track's master send is off (good — that prevents 1/2 from doubling), so audio reaches the master only via the children. Check, in order:

- **Each child's master/parent send is on.** Open the child's I/O dialog, top-left checkbox `Master send channels from/to:` must be ticked. New tracks default to on, but if you copied this from a template or accidentally toggled it, you'll get exactly this symptom.
- **No track is soloed.** A lit `S` button anywhere silences every non-soloed track.
- **No child is muted, faders aren't pulled to −∞.**
- **Master meter shows activity.** If yes, audio is reaching master and your output device routing is the issue.

#### 6. Get waveforms on the child tracks

By default the children are receiving *live* audio via Track Receives — there's no media item on them, so Reaper has no waveform to draw. Two ways to get waveforms:

**Option A — drop the saved stems onto the children (simplest, highest fidelity).**
1. Tick the `Save copy next to source` toggle on the StemSep Separation panel before running separation.
2. After separation finishes, the panel says `Done — saved to <song>_stems`. Drag each WAV from that folder onto the matching child track.
3. The Track Receives can be deleted (or muted) at this point — the WAV alone is enough.

**Option B — render the live multi-out routing.** Keeps the live setup as the source of truth and freezes a snapshot:
1. Select the 5 child tracks.
2. **File → Render…** (default shortcut `Ctrl+Alt+R`).
3. *Source*: **Selected tracks (stems)**. *Bounds*: Project. *Output*: enable **Add rendered items to existing tracks**.
4. Render. Reaper writes one file per child and inserts it as a media item with a visible waveform.

Option A bypasses Reaper's sample-rate conversion and goes straight from Demucs at the model's native rate. Option B is one render but goes through whatever conversion Reaper has configured for the project.

### Ableton Live (Windows)

Live 12 surfaces multi-out VST3 buses through the **Audio From** chooser on the receiving track. Stem buses default to disabled — you have to activate them in the device's bus chooser the first time you load StemSep.

#### Build the routing

1. Drop your audio item on a new track named `StemSep` and load the StemSep VST3 on it.
2. Click the device's title bar to expand it and find the bus enable switches — turn on `Drums`, `Bass`, `Guitar`, `Vocals`, `Other`. (Live shows them as additional outputs on the device.)
3. Create 5 new audio tracks named `Drums`, `Bass`, `Guitar`, `Vocals`, `Other`.
4. On each child track, set:
   - **Audio From** → `StemSep` (the source track)
   - The bus dropdown directly underneath → the matching stem (`StemSep | Drums L+R`, etc.)
   - **Monitor** → `In`
5. Either disable the source track's output (set its Monitor to `Off` and arm-record nothing) or pull its volume to −∞ so channels 1/2 don't double up against the children.

#### Save as a reusable Group Track

Live has no scripting equivalent without Max for Live (Suite-only), but Group Tracks are durable across projects:

1. Select all 6 tracks → `Ctrl+G` to group them.
2. Drag the Group Track header from the Session/Arrangement view *into Live's Browser* (somewhere under **User Library**). Live saves it as an `.als` template file referencing StemSep by name.
3. To reuse: drag the saved item from the Browser into a new Set. The 6 tracks reappear with all routing intact.

Caveats:
- The Audio From references inside the group are preserved by track name, so don't rename `StemSep` after saving the template, or rebuild the routing if you do.
- StemSep is referenced by VST3 plugin name + manufacturer, not by file path — same template works on any machine where the plugin is installed.

## Tests

### Windows

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

### Linux

```bash
# Build tests
cmake --build Builds --target StemSepTests

# Run all tests
Builds/StemSepTests_artefacts/Release/StemSepTests

# Run by tag
Builds/StemSepTests_artefacts/Release/StemSepTests "[dsp]"
Builds/StemSepTests_artefacts/Release/StemSepTests "[processor]"

# Run a single test by name
Builds/StemSepTests_artefacts/Release/StemSepTests "BandFilter: BPF passes centre frequency at unity"
```

Tags: `[dsp]` for `BandFilterTests.cpp`, `[processor]` for `PluginProcessorTests.cpp`.

## Architecture

StemSep is a single-input 5-way splitter with two operating modes:
- **BPF mode**: input runs through 5 parallel bandpass filters (Drums/Bass/Guitar/Vocals/Other).
- **Demucs mode**: a Python child process (Demucs `htdemucs_6s`) separates a chosen audio file into 6 stems; the plugin loads 5 of them into RAM and plays them back synchronised to the host playhead, with per-stem level/enable controls.

Each stem (in either mode) is written to its own VST3 output bus *and* summed into the `Main` bus. Stem buses default to disabled so the plugin behaves like a single-output effect until the host activates them.

### Signal flow

```
Stereo In  → inputCopy_ (saved before output buses are cleared)

BPF mode (per stem i):
    BandFilter[i].prepareCoefficients(freq, Q, gainDB)
    stemScratch_.clear()
    BandFilter[i].processBlock(inL, inR, scratchL, scratchR)   // accumulates +=
    Main += stemScratch_
    StemBus[i] = stemScratch_                                  // if bus is enabled

Demucs mode (per stem i, sample n):
    sample = separatedStems_[i][playhead + n] * gainLin
    Main += sample
    StemBus[i] = sample                                        // if bus is enabled
```

`inputCopy_` exists because JUCE shares input/main-output channels in-place — the input must be saved before `mainOut.clear()`.

### Parameter layout

`NUM_STEMS = 5` drives the entire parameter set. IDs are indexed: `freq0`–`freq4`, `q0`–`q4`, `gain0`–`gain4`, `enable0`–`enable4`. Cached as `std::atomic<float>*` arrays for lock-free audio-thread reads.

### Capture from system audio

The Separation panel exposes a **Capture** button alongside *Browse*. Output goes to `~/Music/StemSep_captures/capture-YYYY-MM-DD-HHMMSS.wav` (or `%USERPROFILE%\Music\StemSep_captures\…` on Windows). The resulting file is loaded as `selectedFile_` and runs through the same Demucs path as a browsed file — so multi-bus routing, the "Save copy" toggle, and tab generation all work unchanged.

Use case: separating tracks you can play locally but can't download (Spotify, Apple Music, streamed YouTube). Capture is real-time — a 4-minute song takes 4 minutes to record.

`CaptureRunner` (subclass of `juce::Thread`) has two backends, picked at compile time:

- **Linux** — spawns `ffmpeg -f pulse -i <sink>.monitor` to record the PulseAudio/PipeWire default sink monitor. Stop sends `SIGINT` (via a pid file written by a `sh -c` wrapper around `exec ffmpeg`) so ffmpeg flushes the WAV's RIFF size fields. Runtime deps: `ffmpeg` and `pactl` (from `pulseaudio-utils`). Writes 16-bit PCM stereo at 48 kHz.

- **Windows** — opens the default render endpoint via WASAPI (`IMMDeviceEnumerator::GetDefaultAudioEndpoint(eRender)` → `IAudioClient::Initialize(..., AUDCLNT_STREAMFLAGS_LOOPBACK, ...)` → `IAudioCaptureClient::GetBuffer`) and writes WAV directly with `juce::WavAudioFormat`. The endpoint's mix format is detected via `WAVE_FORMAT_EXTENSIBLE` + `KSDATAFORMAT_SUBTYPE_IEEE_FLOAT`; PCM 16-/32-bit fallbacks are also handled, anything else is rejected with a clear error. Output is 32-bit float WAV at the device's native rate (typically 48 kHz). No external tools and no Stereo Mix / VB-Cable required. CoInitializeEx is called per-thread with `COINIT_MULTITHREADED`; if the host has already initialised COM in STA mode we get `RPC_E_CHANGED_MODE` and skip the matching `CoUninitialize`.

Neither backend needs Python/venv — that's only on the Separate path.

### DSP — `BandFilter`

Implements the Audio EQ Cookbook constant-0dB-peak bandpass biquad (Direct Form I). Each call to `prepareCoefficients()` writes *target* coefficients (`tb0`, `tb1`, `tb2`, `ta1`, `ta2`, `tGainLin_`); `processBlock()` linearly interpolates the live coefficients toward the targets sample-by-sample to prevent clicks. `processBlock()` **accumulates** (`+=`) into the caller's output buffers — the processor owns clearing.

`getMagnitudeForFrequency()` evaluates the transfer function using the *target* (not current) coefficients, so the `FrequencyDisplay` previews changes instantly before they're audible.

### UI

`FrequencyDisplay` polls `getMagnitudeResponses()` at 30 Hz (JUCE Timer), drawing 512 log-spaced curves (20 Hz–20 kHz) per stem plus a summed white curve. Each `StemStrip` owns APVTS `SliderAttachment`/`ButtonAttachment` objects that keep the knobs in sync with parameters automatically.

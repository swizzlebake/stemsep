# StemSep

A JUCE/VST3 audio plugin that splits a stereo input into 5 stems — *Drums*, *Bass*, *Guitar*, *Vocals*, *Other* — and surfaces each as its own output bus.

Two modes:
- **BPF** — runs the input through 5 parallel bandpass filters with adjustable freq / Q / gain per stem.
- **Demucs** — separates an audio file via Demucs `htdemucs_6s` (Python child process) and plays the resulting stems back in sync with the host playhead.

In either mode each stem is written to its own VST3 output bus *and* summed into a `Main` bus, so the plugin works as both a single-output effect and a multi-out source.

## Build & run

- **Linux (Reaper)**: `./build-linux.sh` configures, builds, and symlinks the bundle into `~/.vst3`. `./run-reaper.sh` launches Reaper with the demucs venv on PATH.
- **Windows (Ableton Live 12)**: `cmake -B Builds -G "Visual Studio 18 2026" -A x64 && cmake --build Builds --config Release`, then copy the `.vst3` bundle to `C:\Program Files\Common Files\VST3`.

Full build, test, deploy, and DAW-routing instructions are in [CLAUDE.md](CLAUDE.md).

## Reaper helpers

- `scripts/StemSep_setup.lua` — ReaScript that builds the source-track + 5-child-track multi-out routing in one action.
- `build-linux.sh` / `run-reaper.sh` — Linux build and launch wrappers.

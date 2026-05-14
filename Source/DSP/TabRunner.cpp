#include "TabRunner.h"

// Tab transcription script — written to temp dir at runtime.
// Mono: librosa.pyin + onset_detect.
// Poly: basic_pitch.predict (Spotify ML model).
// Both render an ASCII tab snapshot to the output text file.
static const char* kTranscribeScript = R"python(
import sys, os, argparse, math
import numpy as np
import soundfile as sf

def emit(p):
    print(f"progress: {p:.2f}", flush=True)

def parse_args():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--instrument", choices=["bass", "guitar"], required=True)
    ap.add_argument("--mode", choices=["mono", "poly"], required=True)
    ap.add_argument("--tempo", type=float, default=0.0)
    ap.add_argument("--time-sig", default="4/4", dest="time_sig")
    ap.add_argument("--tuning", required=True,
                    help="comma-separated MIDI numbers, low string first")
    return ap.parse_args()

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
MAX_FRET = 22

def parse_tuning(spec):
    midis = [int(s.strip()) for s in spec.split(",") if s.strip()]
    if len(midis) < 2:
        raise ValueError(f"tuning must have >= 2 strings, got {spec!r}")
    return midis

def midi_label(midi, used):
    """One-character label for the open string. Disambiguates duplicates by
    lowercasing the higher one (matches the standard 'E ... e' convention)."""
    name = NOTE_NAMES[midi % 12]
    base = name[0]
    if base in used:
        return base.lower()
    return base

def assign_chord(midis, tuning):
    """Greedy: highest midi first, pick lowest fret on an unused string."""
    used = set()
    out = []
    for midi in sorted(midis, reverse=True):
        best = None
        for s, open_midi in enumerate(tuning):
            if s in used:
                continue
            fret = midi - open_midi
            if 0 <= fret <= MAX_FRET:
                if best is None or fret < best[1]:
                    best = (s, fret)
        if best is not None:
            out.append(best)
            used.add(best[0])
    return out

def quantize_slot(time_sec, tempo_bpm, slots_per_beat=4):
    bps = tempo_bpm / 60.0
    return int(round(time_sec * bps * slots_per_beat))

def detect_mono(y, sr):
    import librosa
    fmin = librosa.note_to_hz("C2")
    fmax = librosa.note_to_hz("C7")
    f0, voiced, _ = librosa.pyin(y, fmin=fmin, fmax=fmax, sr=sr)
    times = librosa.times_like(f0, sr=sr)
    onsets = librosa.onset.onset_detect(y=y, sr=sr, units="time")
    if len(onsets) == 0:
        return []
    notes = []
    for i, onset_t in enumerate(onsets):
        end_t = onsets[i + 1] if i + 1 < len(onsets) else float(times[-1])
        mask = (times >= onset_t) & (times < end_t)
        seg = f0[mask]
        valid = seg[~np.isnan(seg)]
        if len(valid) == 0:
            continue
        midi = int(round(librosa.hz_to_midi(float(np.median(valid)))))
        notes.append((float(onset_t), float(end_t), midi))
    return notes

def detect_poly(input_path):
    from basic_pitch.inference import predict
    _, _, note_events = predict(input_path)
    out = []
    for ev in note_events:
        # basic-pitch returns (start, end, pitch, amplitude, [pitch_bends])
        start, end, pitch = ev[0], ev[1], ev[2]
        out.append((float(start), float(end), int(round(float(pitch)))))
    return out

def render_tab(notes, instrument, tuning, tempo, time_sig_str, source_name):
    used = set()
    labels = []
    for m in tuning:
        lbl = midi_label(m, used)
        used.add(lbl.upper())
        labels.append(lbl)
    num_strings = len(tuning)

    SLOTS_PER_BEAT = 4
    SLOTS_PER_BAR  = SLOTS_PER_BEAT * 4   # 4/4 assumed
    BARS_PER_LINE  = 4

    slot_to_midis = {}
    for start, _end, midi in notes:
        slot = quantize_slot(start, tempo, SLOTS_PER_BEAT)
        slot_to_midis.setdefault(slot, []).append(midi)

    slot_to_assign = {}
    out_of_range = 0
    for slot, midis in slot_to_midis.items():
        a = assign_chord(midis, tuning)
        slot_to_assign[slot] = a
        out_of_range += len(midis) - len(a)

    tuning_desc = " ".join(NOTE_NAMES[m % 12] for m in tuning)
    header = []
    header.append(f"# {source_name}  --  {instrument} tab")
    header.append(f"# Tuning (low to high): {tuning_desc}")
    header.append(f"# Tempo: {tempo:.1f} BPM   Time: {time_sig_str}   Subdivision: 16th notes")
    if out_of_range:
        header.append(f"# {out_of_range} note(s) outside {instrument} range omitted")
    header.append("")

    if not slot_to_assign:
        header.append("(no notes detected)")
        return "\n".join(header) + "\n"

    max_slot = max(slot_to_assign.keys())
    total_bars = (max_slot // SLOTS_PER_BAR) + 1

    lines = list(header)
    string_order = list(range(num_strings - 1, -1, -1))   # high pitch on top

    for line_start in range(0, total_bars, BARS_PER_LINE):
        line_end = min(line_start + BARS_PER_LINE, total_bars)
        bars = list(range(line_start, line_end))

        for s_high in string_order:
            row = labels[s_high] + "|"
            for bar in bars:
                for slot_in_bar in range(SLOTS_PER_BAR):
                    slot = bar * SLOTS_PER_BAR + slot_in_bar
                    cell = "--"
                    if slot in slot_to_assign:
                        for (s_idx, fret) in slot_to_assign[slot]:
                            if s_idx == s_high:
                                cell = str(fret) if fret >= 10 else f"{fret}-"
                                break
                    row += cell
                row += "|"
            lines.append(row)
        lines.append("")

    return "\n".join(lines) + "\n"

def main():
    args = parse_args()
    emit(0.05)

    y, sr = sf.read(args.input, always_2d=False)
    if y.ndim > 1:
        y = y.mean(axis=1)
    y = np.asarray(y, dtype=np.float32)

    import librosa
    if sr != 22050:
        y = librosa.resample(y, orig_sr=sr, target_sr=22050)
        sr = 22050
    emit(0.15)

    if args.tempo > 0:
        tempo = args.tempo
    else:
        tempo_arr, _ = librosa.beat.beat_track(y=y, sr=sr)
        tempo = float(np.atleast_1d(tempo_arr)[0])
        if tempo <= 0:
            tempo = 120.0
    emit(0.25)

    tuning = parse_tuning(args.tuning)

    if args.mode == "mono":
        notes = detect_mono(y, sr)
    else:
        emit(0.30)
        notes = detect_poly(args.input)
    emit(0.85)

    text = render_tab(notes, args.instrument, tuning, tempo, args.time_sig,
                      os.path.basename(args.input))
    with open(args.output, "w") as f:
        f.write(text)

    emit(1.0)
    print("done", flush=True)

if __name__ == "__main__":
    main()
)python";

static juce::String findPython()
{
    for (const char* candidate : { "python3", "python" })
    {
        juce::ChildProcess p;
        const juce::StringArray args { candidate, "--version" };
        if (p.start(args, juce::ChildProcess::wantStdErr | juce::ChildProcess::wantStdOut)
            && p.waitForProcessToFinish(3000)
            && p.getExitCode() == 0)
            return candidate;
    }
    return {};
}

TabRunner::TabRunner()
    : juce::Thread("TabRunner")
{
}

TabRunner::~TabRunner()
{
    signalThreadShouldExit();
    stopThread(5000);
}

void TabRunner::start(juce::File       stemWav,
                      juce::File       outputTxt,
                      juce::String     instrument,
                      Mode             mode,
                      double           hostBpm,
                      std::vector<int> tuningMidi,
                      ProgressFn       onProgress,
                      CompletionFn     onDone)
{
    if (isThreadRunning())
        return;

    stemWav_     = std::move(stemWav);
    outputTxt_   = std::move(outputTxt);
    instrument_  = std::move(instrument);
    mode_        = mode;
    hostBpm_     = hostBpm;
    tuningMidi_  = std::move(tuningMidi);
    onProgress_  = std::move(onProgress);
    onDone_      = std::move(onDone);
    startThread();
}

void TabRunner::cancel()
{
    signalThreadShouldExit();
}

void TabRunner::run()
{
    auto failAsync = [this](const juce::String& msg) {
        auto cb = onDone_;
        juce::MessageManager::callAsync([cb, msg]() { cb({ false, {}, msg }); });
    };

    if (pythonExe_.isEmpty()) pythonExe_ = findPython();
    if (pythonExe_.isEmpty())
    {
        failAsync("Python not found. Install python3 and ensure it is on PATH.");
        return;
    }

    // Probe imports — different requirements per mode.
    {
        const juce::String probeImports = (mode_ == Mode::Mono)
            ? "import librosa, soundfile, numpy"
            : "import basic_pitch, librosa, soundfile, numpy";

        juce::ChildProcess probe;
        const juce::StringArray probeArgs { pythonExe_, "-c", probeImports };
        if (! probe.start(probeArgs,
                          juce::ChildProcess::wantStdErr | juce::ChildProcess::wantStdOut))
        {
            failAsync("Could not launch Python. Is it on PATH?");
            return;
        }
        probe.waitForProcessToFinish(15000);
        if (probe.getExitCode() != 0)
        {
            const auto err = probe.readAllProcessOutput();
            const juce::String pkgs = (mode_ == Mode::Mono)
                ? "librosa soundfile"
                : "basic-pitch librosa soundfile";
           #if JUCE_WINDOWS
            const juce::String hint =
                "Install in the Python on PATH:  py -m pip install " + pkgs;
           #else
            const juce::String hint =
                "Install with: ~/.venvs/stemsep/bin/pip install " + pkgs;
           #endif
            failAsync("Required Python packages missing.\n" + hint + "\n" + err);
            return;
        }
    }

    if (threadShouldExit()) { failAsync("Cancelled"); return; }

    const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("StemSep_tab");
    tempDir.createDirectory();
    const auto scriptFile = tempDir.getChildFile("stemsep_transcribe.py");
    scriptFile.replaceWithText(kTranscribeScript);

    juce::StringArray cmd {
        pythonExe_,
        scriptFile.getFullPathName(),
        "--input",      stemWav_.getFullPathName(),
        "--output",     outputTxt_.getFullPathName(),
        "--instrument", instrument_,
        "--mode",       (mode_ == Mode::Mono ? "mono" : "poly")
    };
    if (hostBpm_ > 0.0)
    {
        cmd.add("--tempo");
        cmd.add(juce::String(hostBpm_, 2));
    }
    if (! tuningMidi_.empty())
    {
        juce::String csv;
        for (size_t i = 0; i < tuningMidi_.size(); ++i)
        {
            if (i > 0) csv += ",";
            csv += juce::String(tuningMidi_[i]);
        }
        cmd.add("--tuning");
        cmd.add(csv);
    }
    else
    {
        failAsync("No tuning specified for tab generation");
        return;
    }

    juce::ChildProcess proc;
    if (! proc.start(cmd, juce::ChildProcess::wantStdErr | juce::ChildProcess::wantStdOut))
    {
        failAsync("Failed to start tab generation process");
        return;
    }

    juce::String lineBuffer;
    juce::String fullOutput;

    auto parseProgress = [](const juce::String& line) -> float {
        // expected format: "progress: 0.42"
        const int colon = line.indexOf(":");
        if (colon < 0 || ! line.startsWith("progress")) return -1.f;
        const auto numStr = line.substring(colon + 1).trim();
        if (numStr.isEmpty()) return -1.f;
        const float v = numStr.getFloatValue();
        if (v < 0.f || v > 1.f) return -1.f;
        return v;
    };

    while (! threadShouldExit())
    {
        char buf[512];
        const auto bytesRead = proc.readProcessOutput(buf, sizeof(buf) - 1);
        if (bytesRead > 0)
        {
            buf[bytesRead] = '\0';
            const auto chunk = juce::String::fromUTF8(buf, (int)bytesRead);
            fullOutput += chunk;
            lineBuffer += chunk;

            for (;;)
            {
                int sep = lineBuffer.indexOfAnyOf("\r\n");
                if (sep < 0) break;
                const auto line = lineBuffer.substring(0, sep);
                lineBuffer = lineBuffer.substring(sep + 1);
                const float p = parseProgress(line);
                if (p >= 0.f && onProgress_)
                {
                    auto cb = onProgress_;
                    juce::MessageManager::callAsync([cb, p]() { cb(p); });
                }
            }
        }
        else
        {
            if (! proc.isRunning()) break;
            wait(100);
        }
    }

    if (threadShouldExit())
    {
        proc.kill();
        failAsync("Cancelled");
        return;
    }

    proc.waitForProcessToFinish(30000);

    {
        char buf[1024];
        int n;
        while ((n = (int)proc.readProcessOutput(buf, (juce::uint32)(sizeof(buf) - 1))) > 0)
        {
            buf[n] = '\0';
            fullOutput += juce::String::fromUTF8(buf, n);
        }
    }

    tempDir.getChildFile("transcribe.log").replaceWithText(fullOutput);

    if (proc.getExitCode() != 0)
    {
        juce::String errMsg;
        for (auto& line : juce::StringArray::fromLines(fullOutput))
            if (line.trim().isNotEmpty()) errMsg = line.trim();
        if (errMsg.isEmpty())
            errMsg = "exit code " + juce::String(proc.getExitCode());
        failAsync(errMsg);
        return;
    }

    if (! outputTxt_.existsAsFile())
    {
        failAsync("Tab file was not written");
        return;
    }

    const auto out = outputTxt_;
    auto cb = onDone_;
    juce::MessageManager::callAsync([cb, out]() { cb({ true, out, {} }); });
}

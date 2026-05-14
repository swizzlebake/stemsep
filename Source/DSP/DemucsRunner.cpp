#include "DemucsRunner.h"

// Separation script written to temp dir at runtime.
// Uses soundfile for loading and saving, completely bypassing
// torchaudio.save() which requires torchcodec in torchaudio 2.6+.
static const char* kSeparateScript = R"python(
import sys, os, torch, soundfile as sf
from demucs.pretrained import get_model
from demucs.apply import apply_model

input_file, output_dir, model_name = sys.argv[1], sys.argv[2], sys.argv[3]

model = get_model(model_name)
model.eval()

# Load with soundfile — no torchaudio.load, no codec issues
data, file_sr = sf.read(input_file, always_2d=True)  # (samples, channels)
wav = torch.tensor(data.T, dtype=torch.float32)       # (channels, samples)
if wav.shape[0] == 1:
    wav = wav.repeat(2, 1)

# Resample to model SR if needed
if file_sr != model.samplerate:
    import julius
    wav = julius.resample_frac(wav, file_sr, model.samplerate)

# Separate (tqdm progress goes to stderr, parsed by the plugin)
with torch.no_grad():
    sources = apply_model(model, wav.unsqueeze(0), progress=True)[0]

# Save with soundfile — no torchaudio.save, no torchcodec
track = os.path.splitext(os.path.basename(input_file))[0]
out_dir = os.path.join(output_dir, model_name, track)
os.makedirs(out_dir, exist_ok=True)

for name, source in zip(model.sources, sources):
    sf.write(os.path.join(out_dir, name + '.wav'),
             source.T.cpu().numpy(), model.samplerate, subtype='FLOAT')
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

DemucsRunner::DemucsRunner()
    : juce::Thread("DemucsRunner")
{
}

DemucsRunner::~DemucsRunner()
{
    signalThreadShouldExit();
    stopThread(5000);
}

void DemucsRunner::start(juce::File inputFile, ProgressFn onProgress, CompletionFn onDone)
{
    if (isThreadRunning())
        return;

    inputFile_  = std::move(inputFile);
    onProgress_ = std::move(onProgress);
    onDone_     = std::move(onDone);
    startThread();
}

void DemucsRunner::cancel()
{
    signalThreadShouldExit();
}

void DemucsRunner::run()
{
    if (pythonExe_.isEmpty()) pythonExe_ = findPython();
    if (pythonExe_.isEmpty())
    {
        auto cb = onDone_;
        juce::MessageManager::callAsync([cb]() {
            cb({ false, {}, "Python not found. Install python3 (Linux) or add python to PATH (Windows)." });
        });
        return;
    }

    // Probe — make sure demucs + soundfile are importable
    {
        juce::ChildProcess probe;
        const juce::StringArray probeArgs { pythonExe_, "-c", "import demucs, soundfile" };
        if (!probe.start(probeArgs,
                         juce::ChildProcess::wantStdErr | juce::ChildProcess::wantStdOut))
        {
            auto cb = onDone_;
            juce::MessageManager::callAsync([cb]() {
                cb({ false, {}, "Could not launch Python. Is it on PATH?" });
            });
            return;
        }
        probe.waitForProcessToFinish(10000);
        if (probe.getExitCode() != 0)
        {
            const auto err = probe.readAllProcessOutput();
            auto cb = onDone_;
            juce::MessageManager::callAsync([cb, err]() {
               #if JUCE_WINDOWS
                const auto hint = juce::String(
                    "Demucs not found. Install with:  py -m pip install demucs soundfile julius");
               #else
                const auto hint = juce::String(
                    "Demucs not found. Install with: ~/.venvs/stemsep/bin/pip install demucs soundfile julius");
               #endif
                cb({ false, {}, hint + "\n" + err });
            });
            return;
        }
    }

    if (threadShouldExit()) { auto cb = onDone_; juce::MessageManager::callAsync([cb]() { cb({ false, {}, "Cancelled" }); }); return; }

    const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("StemSep_demucs");
    tempDir.createDirectory();

    // Write the separator script (soundfile-based, no torchaudio.save)
    const auto scriptFile = tempDir.getChildFile("stemsep_separate.py");
    scriptFile.replaceWithText(kSeparateScript);

    const juce::StringArray cmd {
        pythonExe_,
        scriptFile.getFullPathName(),
        inputFile_.getFullPathName(),
        tempDir.getFullPathName(),
        "htdemucs_6s"
    };

    juce::ChildProcess proc;
    if (!proc.start(cmd, juce::ChildProcess::wantStdErr | juce::ChildProcess::wantStdOut))
    {
        auto cb = onDone_;
        juce::MessageManager::callAsync([cb]() {
            cb({ false, {}, "Failed to start separation process" });
        });
        return;
    }

    // Poll output, accumulate everything, and parse tqdm progress lines
    juce::String lineBuffer;
    juce::String fullOutput;

    auto parsePercent = [](const juce::String& line) -> float {
        const int pctPos = line.indexOf("%");
        if (pctPos < 1) return -1.f;
        int start = pctPos - 1;
        while (start > 0 && juce::CharacterFunctions::isDigit(line[start - 1]))
            --start;
        const auto numStr = line.substring(start, pctPos).trim();
        if (numStr.isEmpty() || !numStr.containsOnly("0123456789"))
            return -1.f;
        return numStr.getIntValue() / 100.f;
    };

    while (!threadShouldExit())
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
                const float p = parsePercent(line);
                if (p >= 0.f && onProgress_)
                {
                    auto cb = onProgress_;
                    juce::MessageManager::callAsync([cb, p]() { cb(p); });
                }
            }
        }
        else
        {
            if (!proc.isRunning()) break;
            wait(100);
        }
    }

    if (threadShouldExit())
    {
        proc.kill();
        auto cb = onDone_;
        juce::MessageManager::callAsync([cb]() { cb({ false, {}, "Cancelled" }); });
        return;
    }

    proc.waitForProcessToFinish(30000);

    // Drain any remaining output
    {
        char buf[1024];
        int n;
        while ((n = (int)proc.readProcessOutput(buf, (juce::uint32)(sizeof(buf) - 1))) > 0)
        {
            buf[n] = '\0';
            fullOutput += juce::String::fromUTF8(buf, n);
        }
    }

    tempDir.getChildFile("demucs.log").replaceWithText(fullOutput);

    if (proc.getExitCode() != 0)
    {
        juce::String errMsg;
        for (auto& line : juce::StringArray::fromLines(fullOutput))
            if (line.trim().isNotEmpty()) errMsg = line.trim();
        if (errMsg.isEmpty())
            errMsg = "exit code " + juce::String(proc.getExitCode());

        auto cb = onDone_;
        juce::MessageManager::callAsync([cb, errMsg]() {
            cb({ false, {}, errMsg });
        });
        return;
    }

    // Locate output: <tempDir>/htdemucs_6s/<trackname>/
    const auto modelDir = tempDir.getChildFile("htdemucs_6s");
    juce::Array<juce::File> subdirs;
    modelDir.findChildFiles(subdirs, juce::File::findDirectories, false);
    if (subdirs.isEmpty())
    {
        auto cb = onDone_;
        juce::MessageManager::callAsync([cb]() {
            cb({ false, {}, "Demucs output folder not found" });
        });
        return;
    }

    const auto outputFolder = subdirs.getFirst();
    auto cb = onDone_;
    juce::MessageManager::callAsync([cb, outputFolder]() {
        cb({ true, outputFolder, {} });
    });
}

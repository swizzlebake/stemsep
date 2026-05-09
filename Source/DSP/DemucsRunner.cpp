#include "DemucsRunner.h"

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
    // Probe — make sure demucs is importable
    {
        juce::ChildProcess probe;
        if (!probe.start("python -m demucs --version",
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
                cb({ false, {}, "Demucs not found. Install with: pip install demucs\n" + err });
            });
            return;
        }
    }

    if (threadShouldExit()) { auto cb = onDone_; juce::MessageManager::callAsync([cb]() { cb({ false, {}, "Cancelled" }); }); return; }

    const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("StemSep_demucs");
    tempDir.createDirectory();

    const auto cmd = "python -m demucs --model htdemucs_6s --out \""
                     + tempDir.getFullPathName() + "\" \""
                     + inputFile_.getFullPathName() + "\"";

    juce::ChildProcess proc;
    if (!proc.start(cmd, juce::ChildProcess::wantStdErr | juce::ChildProcess::wantStdOut))
    {
        auto cb = onDone_;
        juce::MessageManager::callAsync([cb]() {
            cb({ false, {}, "Failed to start Demucs process" });
        });
        return;
    }

    // Poll output and parse tqdm progress lines
    juce::String lineBuffer;
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
            lineBuffer += juce::String::fromUTF8(buf, (int)bytesRead);

            // Process complete lines (split on \r or \n)
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
    if (proc.getExitCode() != 0)
    {
        const auto errText = lineBuffer + proc.readAllProcessOutput();
        auto cb = onDone_;
        juce::MessageManager::callAsync([cb, errText]() {
            cb({ false, {}, "Demucs failed:\n" + errText });
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

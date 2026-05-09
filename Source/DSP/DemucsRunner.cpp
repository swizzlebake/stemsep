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
    // Probe — make sure demucs is importable (import check, not --version which requires args)
    {
        juce::ChildProcess probe;
        if (!probe.start("python -c \"import demucs\"",
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

    const auto cmd = "python -m demucs -n htdemucs_6s -o \""
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

    // Poll output, accumulate everything, and parse tqdm progress lines
    juce::String lineBuffer;  // incomplete current line, for progress parsing
    juce::String fullOutput;  // all output retained for error reporting

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
            fullOutput  += chunk;
            lineBuffer  += chunk;

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

    // Drain any remaining output after process exits
    {
        char buf[1024];
        int n;
        while ((n = (int)proc.readProcessOutput(buf, (juce::uint32)(sizeof(buf) - 1))) > 0)
        {
            buf[n] = '\0';
            fullOutput += juce::String::fromUTF8(buf, n);
        }
    }

    // Always write a log so the full output is inspectable
    tempDir.getChildFile("demucs.log").replaceWithText(fullOutput);

    if (proc.getExitCode() != 0)
    {
        // Surface the last non-empty line as the short error message
        juce::String lastLine;
        for (auto& line : juce::StringArray::fromLines(fullOutput))
            if (line.trim().isNotEmpty()) lastLine = line.trim();

        const auto errMsg = lastLine.isNotEmpty() ? lastLine : "exit code " + juce::String(proc.getExitCode());
        auto cb = onDone_;
        juce::MessageManager::callAsync([cb, errMsg, fullOutput]() {
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

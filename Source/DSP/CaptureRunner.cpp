#include "CaptureRunner.h"

#if JUCE_LINUX
 #include <signal.h>
 #include <sys/types.h>
#elif JUCE_WINDOWS
 #include <juce_audio_formats/juce_audio_formats.h>
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
 #include <mmdeviceapi.h>
 #include <audioclient.h>
 #include <functiondiscoverykeys_devpkey.h>
 #include <ks.h>
 #include <ksmedia.h>
 #pragma comment(lib, "ole32.lib")
#endif

CaptureRunner::CaptureRunner() : juce::Thread("CaptureRunner") {}

CaptureRunner::~CaptureRunner()
{
    signalThreadShouldExit();
    stopThread(5000);
}

void CaptureRunner::start(juce::File outputFile, CompletionFn onDone)
{
    if (isThreadRunning())
        return;

    outputFile_ = std::move(outputFile);
    onDone_     = std::move(onDone);
    startTimeMs_.store(0);
    startThread();
}

void CaptureRunner::stop()
{
    signalThreadShouldExit();
}

juce::int64 CaptureRunner::elapsedSeconds() const
{
    const auto start = startTimeMs_.load();
    if (start == 0) return 0;
    return (juce::Time::currentTimeMillis() - start) / 1000;
}

// ─────────────────────────────────────────────────────────────────────────────
// Linux: ffmpeg + PulseAudio/PipeWire monitor source
// ─────────────────────────────────────────────────────────────────────────────
#if JUCE_LINUX

namespace
{
    juce::String findFfmpeg()
    {
        juce::ChildProcess p;
        const juce::StringArray args { "ffmpeg", "-version" };
        if (p.start(args, juce::ChildProcess::wantStdErr | juce::ChildProcess::wantStdOut)
            && p.waitForProcessToFinish(3000)
            && p.getExitCode() == 0)
            return "ffmpeg";
        return {};
    }

    juce::String findDefaultMonitorSource()
    {
        juce::ChildProcess p;
        const juce::StringArray args { "pactl", "get-default-sink" };
        if (! p.start(args, juce::ChildProcess::wantStdErr | juce::ChildProcess::wantStdOut))
            return {};
        if (! p.waitForProcessToFinish(2000) || p.getExitCode() != 0)
            return {};
        const auto sink = p.readAllProcessOutput().trim();
        if (sink.isEmpty())
            return {};
        return sink + ".monitor";
    }
}

void CaptureRunner::run()
{
    const auto ffmpeg = findFfmpeg();
    if (ffmpeg.isEmpty())
    {
        auto cb = onDone_;
        juce::MessageManager::callAsync([cb]() {
            cb({ false, {},
                 "ffmpeg not found on PATH \xe2\x80\x94 install via: sudo apt install ffmpeg" });
        });
        return;
    }

    const auto monitor = findDefaultMonitorSource();
    if (monitor.isEmpty())
    {
        auto cb = onDone_;
        juce::MessageManager::callAsync([cb]() {
            cb({ false, {},
                 "Cannot find default sink monitor \xe2\x80\x94 is PulseAudio/PipeWire running?" });
        });
        return;
    }

    outputFile_.getParentDirectory().createDirectory();

    // Wrap ffmpeg in a shell that writes its pid before exec'ing — `exec` keeps
    // the same pid, so the pid in the file is ffmpeg's. juce::ChildProcess in
    // JUCE 8 has no public getPID() and its kill() sends SIGKILL, which leaves
    // the RIFF size fields un-finalised. SIGINT via the pid file lets ffmpeg
    // flush the WAV header.
    const auto pidFile = outputFile_.getSiblingFile(
        outputFile_.getFileNameWithoutExtension() + ".pid");
    pidFile.deleteFile();

    const auto shellCmd =
          "echo $$ > " + pidFile.getFullPathName().quoted()
        + "; exec " + ffmpeg.quoted() + " -y -f pulse -i " + monitor.quoted()
        + " -ac 2 -ar 48000 " + outputFile_.getFullPathName().quoted();

    const juce::StringArray cmd { "sh", "-c", shellCmd };

    juce::ChildProcess proc;
    if (! proc.start(cmd, juce::ChildProcess::wantStdErr | juce::ChildProcess::wantStdOut))
    {
        auto cb = onDone_;
        juce::MessageManager::callAsync([cb]() {
            cb({ false, {}, "Failed to start ffmpeg" });
        });
        return;
    }

    startTimeMs_.store(juce::Time::currentTimeMillis());

    juce::String fullOutput;
    char buf[512];

    while (! threadShouldExit())
    {
        const auto n = proc.readProcessOutput(buf, sizeof(buf) - 1);
        if (n > 0)
        {
            buf[n] = '\0';
            fullOutput += juce::String::fromUTF8(buf, (int)n);
        }
        else
        {
            if (! proc.isRunning()) break;
            wait(100);
        }
    }

    if (proc.isRunning())
    {
        const auto pid = (pid_t) pidFile.loadFileAsString().trim().getIntValue();
        if (pid > 0)
            ::kill(pid, SIGINT);

        const auto deadline = juce::Time::currentTimeMillis() + 2000;
        while (proc.isRunning() && juce::Time::currentTimeMillis() < deadline)
        {
            const auto n = proc.readProcessOutput(buf, sizeof(buf) - 1);
            if (n > 0)
            {
                buf[n] = '\0';
                fullOutput += juce::String::fromUTF8(buf, (int)n);
            }
            else
            {
                wait(50);
            }
        }
        if (proc.isRunning())
            proc.kill();
    }

    proc.waitForProcessToFinish(2000);

    {
        int n;
        while ((n = (int)proc.readProcessOutput(buf, (juce::uint32)(sizeof(buf) - 1))) > 0)
        {
            buf[n] = '\0';
            fullOutput += juce::String::fromUTF8(buf, n);
        }
    }

    pidFile.deleteFile();

    const bool wavExists = outputFile_.existsAsFile() && outputFile_.getSize() > 1024;
    if (! wavExists)
    {
        juce::String errMsg;
        for (auto& line : juce::StringArray::fromLines(fullOutput))
            if (line.trim().isNotEmpty()) errMsg = line.trim();
        if (errMsg.isEmpty())
            errMsg = "ffmpeg produced no output";

        auto cb = onDone_;
        juce::MessageManager::callAsync([cb, errMsg]() {
            cb({ false, {}, errMsg.substring(0, 200) });
        });
        return;
    }

    auto cb = onDone_;
    auto file = outputFile_;
    juce::MessageManager::callAsync([cb, file]() {
        cb({ true, file, {} });
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Windows: WASAPI loopback on the default render endpoint
// ─────────────────────────────────────────────────────────────────────────────
#elif JUCE_WINDOWS

namespace
{
    template <typename T> void safeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

    bool detectFloatFormat(const WAVEFORMATEX* fmt)
    {
        if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
            return true;
        if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        {
            const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt);
            return IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != FALSE;
        }
        return false;
    }
}

void CaptureRunner::run()
{
    auto failAsync = [this](juce::String msg) {
        auto cb = onDone_;
        juce::MessageManager::callAsync([cb, msg]() { cb({ false, {}, msg }); });
    };

    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comOwned = SUCCEEDED(coHr);   // RPC_E_CHANGED_MODE => host already CoInit'd; do NOT uninit

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice*           device     = nullptr;
    IAudioClient*        client     = nullptr;
    IAudioCaptureClient* capture    = nullptr;
    WAVEFORMATEX*        mixFormat  = nullptr;

    auto cleanup = [&]() {
        safeRelease(capture);
        if (mixFormat) { CoTaskMemFree(mixFormat); mixFormat = nullptr; }
        safeRelease(client);
        safeRelease(device);
        safeRelease(enumerator);
        if (comOwned) CoUninitialize();
    };

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) { failAsync("Could not create audio device enumerator (COM error)"); cleanup(); return; }

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) { failAsync("No default audio output device found"); cleanup(); return; }

    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(&client));
    if (FAILED(hr)) { failAsync("Could not activate IAudioClient on default output"); cleanup(); return; }

    hr = client->GetMixFormat(&mixFormat);
    if (FAILED(hr) || mixFormat == nullptr) { failAsync("GetMixFormat failed"); cleanup(); return; }

    constexpr REFERENCE_TIME REFTIMES_PER_SEC = 10000000;   // 1 second buffer
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            AUDCLNT_STREAMFLAGS_LOOPBACK,
                            REFTIMES_PER_SEC, 0, mixFormat, nullptr);
    if (FAILED(hr)) { failAsync("IAudioClient::Initialize failed (loopback)"); cleanup(); return; }

    hr = client->GetService(__uuidof(IAudioCaptureClient),
                            reinterpret_cast<void**>(&capture));
    if (FAILED(hr)) { failAsync("Could not get IAudioCaptureClient"); cleanup(); return; }

    const bool   isFloat        = detectFloatFormat(mixFormat);
    const int    srcChannels    = (int) mixFormat->nChannels;
    const int    bitsPerSample  = (int) mixFormat->wBitsPerSample;
    const double sampleRate     = (double) mixFormat->nSamplesPerSec;
    const int    outChannels    = juce::jmin(srcChannels, 2);   // mono passthrough or stereo

    // Reject formats we don't decode (24-bit packed PCM is unusual in shared mode
    // and would need bit-shuffling; surface a clear error rather than write garbage).
    if (! isFloat && bitsPerSample != 16 && bitsPerSample != 32)
    {
        failAsync(juce::String::formatted(
            "Unsupported endpoint format: %d-bit %s. "
            "Set the default device's shared format to 16/24/32-bit float in Sound Control Panel.",
            bitsPerSample, isFloat ? "float" : "PCM"));
        cleanup();
        return;
    }

    outputFile_.getParentDirectory().createDirectory();
    if (outputFile_.existsAsFile())
        outputFile_.deleteFile();

    auto rawStream = outputFile_.createOutputStream();
    if (rawStream == nullptr) { failAsync("Could not open WAV file for writing"); cleanup(); return; }

    juce::WavAudioFormat wav;
    // 32-bit float WAV at the device's native rate. Demucs reads via soundfile,
    // which handles any sample rate, and float WAV survives loud transients cleanly.
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(rawStream.get(), sampleRate,
                            (unsigned int) outChannels, 32, {}, 0));
    if (writer == nullptr) { failAsync("Could not create WAV writer"); cleanup(); return; }
    rawStream.release();   // writer owns it now

    hr = client->Start();
    if (FAILED(hr)) { failAsync("IAudioClient::Start failed"); writer.reset(); cleanup(); return; }

    startTimeMs_.store(juce::Time::currentTimeMillis());

    juce::AudioBuffer<float> scratch(outChannels, 4096);

    while (! threadShouldExit())
    {
        UINT32 packetSize = 0;
        hr = capture->GetNextPacketSize(&packetSize);
        if (FAILED(hr)) break;

        if (packetSize == 0)
        {
            wait(10);
            continue;
        }

        BYTE*  data      = nullptr;
        UINT32 numFrames = 0;
        DWORD  flags     = 0;
        hr = capture->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
        if (FAILED(hr)) break;

        if (numFrames > 0)
        {
            if ((int) numFrames > scratch.getNumSamples())
                scratch.setSize(outChannels, (int) numFrames, false, false, true);

            if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
            {
                scratch.clear(0, (int) numFrames);
            }
            else if (isFloat)
            {
                const float* src = reinterpret_cast<const float*>(data);
                for (int c = 0; c < outChannels; ++c)
                {
                    float* dst = scratch.getWritePointer(c);
                    for (UINT32 i = 0; i < numFrames; ++i)
                        dst[i] = src[i * srcChannels + c];
                }
            }
            else if (bitsPerSample == 16)
            {
                const int16_t* src = reinterpret_cast<const int16_t*>(data);
                const float inv = 1.0f / 32768.0f;
                for (int c = 0; c < outChannels; ++c)
                {
                    float* dst = scratch.getWritePointer(c);
                    for (UINT32 i = 0; i < numFrames; ++i)
                        dst[i] = (float) src[i * srcChannels + c] * inv;
                }
            }
            else // bitsPerSample == 32, PCM
            {
                const int32_t* src = reinterpret_cast<const int32_t*>(data);
                const float inv = 1.0f / 2147483648.0f;
                for (int c = 0; c < outChannels; ++c)
                {
                    float* dst = scratch.getWritePointer(c);
                    for (UINT32 i = 0; i < numFrames; ++i)
                        dst[i] = (float) src[i * srcChannels + c] * inv;
                }
            }

            const float* readPtrs[2] = {
                scratch.getReadPointer(0),
                outChannels > 1 ? scratch.getReadPointer(1) : scratch.getReadPointer(0)
            };
            if (! writer->writeFromFloatArrays(readPtrs, outChannels, (int) numFrames))
            {
                capture->ReleaseBuffer(numFrames);
                client->Stop();
                writer.reset();
                failAsync("WAV writer failed mid-capture (disk full?)");
                cleanup();
                return;
            }

        }

        capture->ReleaseBuffer(numFrames);
    }

    client->Stop();
    writer.reset();   // flush + finalise RIFF size fields
    cleanup();

    const bool wavOK = outputFile_.existsAsFile() && outputFile_.getSize() > 44;
    if (! wavOK)
    {
        failAsync("Capture produced no audio (default output may be muted)");
        return;
    }

    auto cb = onDone_;
    auto file = outputFile_;
    juce::MessageManager::callAsync([cb, file]() { cb({ true, file, {} }); });
}

// ─────────────────────────────────────────────────────────────────────────────
// Unsupported platforms
// ─────────────────────────────────────────────────────────────────────────────
#else

void CaptureRunner::run()
{
    auto cb = onDone_;
    juce::MessageManager::callAsync([cb]() {
        cb({ false, {}, "System audio capture is not yet supported on this platform." });
    });
}

#endif

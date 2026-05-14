#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>

// Records the system audio output to a WAV file. Two backends:
//   Linux   - spawns ffmpeg against the PulseAudio/PipeWire default sink's .monitor source.
//   Windows - opens the default render endpoint via WASAPI in loopback mode and writes
//             WAV directly with juce::AudioFormatWriter (no external tools required).
// On unsupported platforms start() reports an unsupported-platform error via the
// completion callback.
class CaptureRunner : public juce::Thread
{
public:
    struct Result
    {
        bool         success;
        juce::File   outputFile;
        juce::String errorMessage;
    };

    using CompletionFn = std::function<void(Result)>;

    CaptureRunner();
    ~CaptureRunner() override;

    void start(juce::File outputFile, CompletionFn onDone);
    void stop();
    juce::int64 elapsedSeconds() const;

private:
    void run() override;

    juce::File   outputFile_;
    CompletionFn onDone_;
    std::atomic<juce::int64> startTimeMs_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CaptureRunner)
};

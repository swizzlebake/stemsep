#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <functional>

class DemucsRunner : public juce::Thread
{
public:
    struct Result
    {
        bool         success;
        juce::File   outputFolder;
        juce::String errorMessage;
    };

    using ProgressFn   = std::function<void(float)>;
    using CompletionFn = std::function<void(Result)>;

    DemucsRunner();
    ~DemucsRunner() override;

    void start(juce::File inputFile, ProgressFn onProgress, CompletionFn onDone);
    void cancel();

private:
    void run() override;

    juce::File     inputFile_;
    ProgressFn     onProgress_;
    CompletionFn   onDone_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DemucsRunner)
};

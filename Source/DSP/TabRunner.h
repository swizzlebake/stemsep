#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <functional>
#include <vector>

class TabRunner : public juce::Thread
{
public:
    enum class Mode { Mono, Poly };

    struct Result
    {
        bool         success;
        juce::File   outputFile;
        juce::String errorMessage;
    };

    using ProgressFn   = std::function<void(float)>;
    using CompletionFn = std::function<void(Result)>;

    TabRunner();
    ~TabRunner() override;

    void start(juce::File       stemWav,
               juce::File       outputTxt,
               juce::String     instrument,    // "bass" or "guitar"
               Mode             mode,
               double           hostBpm,       // 0.0 if unknown
               std::vector<int> tuningMidi,    // open-string MIDI notes, low to high
               ProgressFn       onProgress,
               CompletionFn     onDone);

    void cancel();

private:
    void run() override;

    juce::File       stemWav_;
    juce::File       outputTxt_;
    juce::String     instrument_;
    Mode             mode_ { Mode::Mono };
    double           hostBpm_ { 0.0 };
    std::vector<int> tuningMidi_;
    ProgressFn       onProgress_;
    CompletionFn     onDone_;
    juce::String     pythonExe_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TabRunner)
};

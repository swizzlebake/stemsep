#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "DSP/BandFilter.h"
#include <array>
#include <atomic>
#include <vector>

static constexpr int NUM_STEMS = 5;

class StemSepProcessor : public juce::AudioProcessor
{
public:
    enum class Mode { BPF, Demucs };

    StemSepProcessor();
    ~StemSepProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "StemSep"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    void getMagnitudeResponses(const std::vector<float>& freqPoints,
                               std::array<std::vector<float>, NUM_STEMS>& outMagnitudes) const;

    void refreshCoefficients();

    Mode getMode() const                { return mode_.load(std::memory_order_acquire); }
    void setMode(Mode m)                { mode_.store(m, std::memory_order_release); }
    bool hasSeparatedAudio() const      { return separatedAudioReady_.load(std::memory_order_acquire); }
    void loadSeparatedStems(const juce::File& demucsOutputFolder);

    juce::File getSeparatedSourceFolder() const { return juce::File(separatedSourcePath_); }

    // Returns the host's current BPM, or 0.0 if unavailable. Message-thread safe.
    double getHostBpm();

    // Tuning helpers — stemIndex must be 1 (bass) or 2 (guitar).
    std::vector<int> getTuningMidi(int stemIndex) const;
    juce::String     getCustomTuningText(int stemIndex) const;
    void             setCustomTuningText(int stemIndex, const juce::String& text);

    // Fired on the message thread when loadSeparatedStems completes.
    std::function<void()> onSeparatedStemsLoaded;

private:
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::array<BandFilter, NUM_STEMS> filters;

    std::array<std::atomic<float>*, NUM_STEMS> freqParams{};
    std::array<std::atomic<float>*, NUM_STEMS> qParams{};
    std::array<std::atomic<float>*, NUM_STEMS> gainParams{};
    std::array<std::atomic<float>*, NUM_STEMS> enableParams{};

    // BPF mode: pre-allocated input copy (in/out share channels in in-place mode)
    juce::AudioBuffer<float> inputCopy_;

    // Per-stem scratch — used when writing to stem output buses
    juce::AudioBuffer<float> stemScratch_;

    // Demucs mode
    std::array<juce::AudioBuffer<float>, NUM_STEMS> separatedStems_;
    std::atomic<bool>    separatedAudioReady_{ false };
    std::atomic<Mode>    mode_{ Mode::BPF };
    juce::AudioFormatManager formatManager_;
    juce::String         separatedSourcePath_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StemSepProcessor)
};

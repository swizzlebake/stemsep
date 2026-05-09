#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/BandFilter.h"
#include <array>
#include <vector>

static constexpr int NUM_STEMS = 4;

class StemSepProcessor : public juce::AudioProcessor
{
public:
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

private:
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::array<BandFilter, NUM_STEMS> filters;

    std::array<std::atomic<float>*, NUM_STEMS> freqParams{};
    std::array<std::atomic<float>*, NUM_STEMS> qParams{};
    std::array<std::atomic<float>*, NUM_STEMS> gainParams{};
    std::array<std::atomic<float>*, NUM_STEMS> enableParams{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StemSepProcessor)
};

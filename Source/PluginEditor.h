#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "UI/FrequencyDisplay.h"
#include "UI/StemStrip.h"
#include "UI/SeparationPanel.h"
#include <array>
#include <memory>

class StemSepEditor : public juce::AudioProcessorEditor
{
public:
    explicit StemSepEditor(StemSepProcessor&);
    ~StemSepEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    StemSepProcessor& processor_;

    FrequencyDisplay freqDisplay_;
    SeparationPanel  separationPanel_;
    std::array<std::unique_ptr<StemStrip>, NUM_STEMS> stemStrips_;

    static constexpr int editorWidth    = 800;
    static constexpr int editorHeight   = 755;
    static constexpr int displayHeight  = 300;
    static constexpr int panelHeight    =  80;
    static constexpr int stripHeight    =  75;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StemSepEditor)
};

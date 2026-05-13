#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "DSP/TabRunner.h"
#include "UI/FrequencyDisplay.h"
#include "UI/StemStrip.h"
#include "UI/SeparationPanel.h"
#include <array>
#include <memory>

class StemSepEditor : public juce::AudioProcessorEditor
{
public:
    explicit StemSepEditor(StemSepProcessor&);
    ~StemSepEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void rebuildThumbnails();
    void runTabGeneration(int stemIndex, TabRunner::Mode mode);
    void openTabWindow(const juce::File& tabFile);
    void updateTabButtonState();

    StemSepProcessor& processor_;

    FrequencyDisplay freqDisplay_;
    SeparationPanel  separationPanel_;
    std::array<std::unique_ptr<StemStrip>, NUM_STEMS> stemStrips_;

    juce::AudioFormatManager thumbFormatManager_;
    juce::AudioThumbnailCache thumbCache_ { NUM_STEMS };
    std::array<std::unique_ptr<juce::AudioThumbnail>, NUM_STEMS> thumbs_;

    TabRunner tabRunner_;
    juce::Component::SafePointer<juce::DialogWindow> tabWindow_;
    bool tabRunning_ = false;

    static constexpr int editorWidth    = 1000;
    static constexpr int editorHeight   = 800;
    static constexpr int displayHeight  = 300;
    static constexpr int panelHeight    =  90;
    static constexpr int stripHeight    =  82;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StemSepEditor)
};

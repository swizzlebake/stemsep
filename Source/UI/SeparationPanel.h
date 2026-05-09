#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "DSP/DemucsRunner.h"
#include "PluginProcessor.h"
#include <functional>
#include <memory>

class SeparationPanel : public juce::Component
{
public:
    explicit SeparationPanel(StemSepProcessor& processor);
    ~SeparationPanel() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    std::function<void(StemSepProcessor::Mode)> onModeChanged;
    std::function<void(juce::File)>             onSeparationComplete;

private:
    void browseForFile();
    void startSeparation();
    void updateModeButtons();

    StemSepProcessor& processor_;

    juce::TextButton bpfButton_      { "BPF Mode" };
    juce::TextButton demucsButton_   { "Demucs Mode" };
    juce::Label      filePathLabel_;
    juce::TextButton browseButton_   { "Browse\xe2\x80\xa6" };
    juce::TextButton separateButton_ { "Separate" };

    double progressValue_ = 0.0;
    juce::ProgressBar progressBar_ { progressValue_ };

    juce::Label statusLabel_;

    juce::File                            selectedFile_;
    std::unique_ptr<juce::FileChooser>    fileChooser_;
    DemucsRunner                          runner_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SeparationPanel)
};

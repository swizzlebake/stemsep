#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "DSP/CaptureRunner.h"
#include "DSP/DemucsRunner.h"
#include "PluginProcessor.h"
#include <functional>
#include <memory>

class SeparationPanel : public juce::Component,
                        private juce::Timer
{
public:
    explicit SeparationPanel(StemSepProcessor& processor);
    ~SeparationPanel() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    std::function<void(StemSepProcessor::Mode)> onModeChanged;
    std::function<void(juce::File)>             onSeparationComplete;
    std::function<void(bool)>                   onShowWaveformsChanged;

    bool getShowWaveforms() const { return showWaveformsToggle_.getToggleState(); }

    void setStatusText(const juce::String& text)
    {
        statusLabel_.setText(text, juce::dontSendNotification);
    }

private:
    void browseForFile();
    void startSeparation();
    void loadStemsFromFolder();
    void updateModeButtons();
    void toggleCapture();
    void timerCallback() override;

    StemSepProcessor& processor_;

    juce::TextButton bpfButton_      { "BPF Mode" };
    juce::TextButton demucsButton_   { "Demucs Mode" };
    juce::Label      filePathLabel_;
    juce::TextButton browseButton_   { "Browse" };
    juce::TextButton captureButton_  { "Capture" };
    juce::TextButton separateButton_ { "Separate" };
    juce::TextButton loadStemsButton_ { "Load stems" };
    juce::ToggleButton saveCopyToggle_ { "Save copy next to source" };
    juce::ToggleButton showWaveformsToggle_ { "Show waveforms" };

    double progressValue_ = 0.0;
    juce::ProgressBar progressBar_ { progressValue_ };

    juce::Label statusLabel_;

    juce::File                            selectedFile_;
    std::unique_ptr<juce::FileChooser>    fileChooser_;
    DemucsRunner                          runner_;
    CaptureRunner                         captureRunner_;
    bool                                  capturing_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SeparationPanel)
};

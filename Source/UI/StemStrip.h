#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "DSP/TabRunner.h"
#include <functional>
#include <memory>

class StemStrip : public juce::Component
{
public:
    StemStrip(int stemIndex, const juce::String& name,
              juce::AudioProcessorValueTreeState& apvts,
              juce::Colour color);
    ~StemStrip() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    void setWaveformVisible(bool shouldBeVisible);
    void setWaveformSource(juce::AudioThumbnail* thumb);

    // Enable/disable the right-click Tab action (only meaningful for Bass and Guitar strips).
    void setTabActionEnabled(bool shouldBeEnabled);

    // Fired when the user picks a transcription mode from the Tab popup menu.
    std::function<void(int /*stemIndex*/, TabRunner::Mode)> onTabRequested;

    // Hooks for the Custom tuning dialog (set by the editor).
    // getCustomTuningText returns the saved free-form tuning string for this stem.
    // setCustomTuningText saves a new one. Both are no-ops on non-Bass/Guitar stems.
    std::function<juce::String()>                onGetCustomTuning;
    std::function<void(const juce::String&)>     onSetCustomTuning;

private:
    void mouseDown(const juce::MouseEvent& e) override;
    void showTabMenu(juce::Component* targetComponent);
    void handleTuningChanged();
    void promptForCustomTuning(int previousIndex);

    int stemIndex_;
    juce::Colour stemColor_;

    juce::Label        stemLabel_;
    juce::Slider       freqKnob_;
    juce::Slider       qKnob_;
    juce::Slider       gainSlider_;
    juce::ToggleButton enableButton_;
    juce::ComboBox     tuningCombo_;
    int                tuningCustomIndex_ = -1;     // index of "Custom\xe2\x80\xa6" entry
    int                lastValidTuningIndex_ = 0;
    bool               suppressTuningCallback_ = false;
    bool               supportsTab_ = false;
    bool               tabEnabled_  = false;

    class WaveformView;
    std::unique_ptr<WaveformView> waveform_;
    bool waveformVisible_ = false;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   freqAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   qAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   gainAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   enableAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tuningAttachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StemStrip)
};

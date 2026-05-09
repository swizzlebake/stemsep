#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

class StemStrip : public juce::Component
{
public:
    StemStrip(int stemIndex, const juce::String& name,
              juce::AudioProcessorValueTreeState& apvts,
              juce::Colour color);
    ~StemStrip() override = default;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    int stemIndex_;
    juce::Colour stemColor_;

    juce::Label        stemLabel_;
    juce::Slider       freqKnob_;
    juce::Slider       qKnob_;
    juce::Slider       gainSlider_;
    juce::ToggleButton enableButton_;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> qAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StemStrip)
};

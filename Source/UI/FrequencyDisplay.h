#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include <vector>
#include <array>

class FrequencyDisplay : public juce::Component,
                         private juce::Timer
{
public:
    explicit FrequencyDisplay(StimSepProcessor& processor);
    ~FrequencyDisplay() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setStemColor(int stemIndex, juce::Colour c);

private:
    void timerCallback() override;

    void drawGrid(juce::Graphics& g);
    void drawCurve(juce::Graphics& g, const std::vector<float>& magnitudes,
                   juce::Colour color, float strokeWidth);

    float freqToX(float freqHz) const;
    float dBToY(float dB) const;

    StimSepProcessor& processor_;
    std::array<juce::Colour, NUM_STEMS> stemColors_;

    std::vector<float> freqPoints_;
    std::array<std::vector<float>, NUM_STEMS> magnitudes_;
    std::vector<float> combinedMagnitudes_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrequencyDisplay)
};

#include "StemStrip.h"

StemStrip::StemStrip(int stemIndex,
                     juce::AudioProcessorValueTreeState& apvts,
                     juce::Colour color)
    : stemIndex_(stemIndex), stemColor_(color)
{
    const auto id  = juce::String(stemIndex);
    const auto num = juce::String(stemIndex + 1);

    stemLabel_.setText("Stem " + num, juce::dontSendNotification);
    stemLabel_.setColour(juce::Label::textColourId, color);
    stemLabel_.setFont(juce::Font(13.f, juce::Font::bold));
    stemLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(stemLabel_);

    auto configKnob = [&](juce::Slider& s, const juce::String& tooltip) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 68, 16);
        s.setColour(juce::Slider::rotarySliderFillColourId, color);
        s.setColour(juce::Slider::rotarySliderOutlineColourId,
                    juce::Colours::white.withAlpha(0.15f));
        s.setTooltip(tooltip);
        addAndMakeVisible(s);
    };

    configKnob(freqKnob_, "Center frequency (Hz)");
    freqAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "freq" + id, freqKnob_);

    configKnob(qKnob_, "Bandwidth (Q)");
    qAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "q" + id, qKnob_);

    gainSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    gainSlider_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 56, 20);
    gainSlider_.setColour(juce::Slider::trackColourId, color.withAlpha(0.5f));
    gainSlider_.setColour(juce::Slider::thumbColourId, color);
    gainSlider_.setTooltip("Peak gain (dB)");
    addAndMakeVisible(gainSlider_);
    gainAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "gain" + id, gainSlider_);

    enableButton_.setButtonText("On");
    enableButton_.setColour(juce::ToggleButton::tickColourId, color);
    enableButton_.setTooltip("Enable / bypass this stem");
    addAndMakeVisible(enableButton_);
    enableAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, "enable" + id, enableButton_);
}

void StemStrip::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff16213e));

    // Coloured left accent bar
    g.setColour(stemColor_);
    g.fillRect(0, 0, 5, getHeight());

    // Subtle separator line at the bottom
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.drawHorizontalLine(getHeight() - 1, 0.f, (float)getWidth());
}

void StemStrip::resized()
{
    auto area = getLocalBounds().reduced(6).withTrimmedLeft(6);

    stemLabel_.setBounds(area.removeFromLeft(60).withSizeKeepingCentre(60, 32));
    enableButton_.setBounds(area.removeFromRight(46).withSizeKeepingCentre(46, 24));
    freqKnob_.setBounds(area.removeFromLeft(72));
    qKnob_.setBounds(area.removeFromLeft(72));
    gainSlider_.setBounds(area.withTrimmedLeft(8));
}

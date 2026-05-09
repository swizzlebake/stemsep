#include "PluginEditor.h"

static const juce::Colour stemColors[NUM_STEMS] = {
    juce::Colour(0xff4fc3f7),  // sky blue  — Drums
    juce::Colour(0xffaed581),  // lime      — Bass
    juce::Colour(0xffffb74d),  // amber     — Guitar
    juce::Colour(0xffce93d8),  // lavender  — Vocals
    juce::Colour(0xffef9a9a),  // coral     — Other
};

static const char* stemNames[NUM_STEMS] = {
    "Drums", "Bass", "Guitar", "Vocals", "Other"
};

StemSepEditor::StemSepEditor(StemSepProcessor& p)
    : AudioProcessorEditor(&p),
      processor_(p),
      freqDisplay_(p)
{
    for (int i = 0; i < NUM_STEMS; ++i)
    {
        freqDisplay_.setStemColor(i, stemColors[i]);
        stemStrips_[i] = std::make_unique<StemStrip>(i, stemNames[i], p.getAPVTS(), stemColors[i]);
        addAndMakeVisible(*stemStrips_[i]);
    }
    addAndMakeVisible(freqDisplay_);

    setSize(editorWidth, editorHeight);
    setResizable(false, false);
}

void StemSepEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a2e));
}

void StemSepEditor::resized()
{
    auto area = getLocalBounds();
    freqDisplay_.setBounds(area.removeFromTop(displayHeight));
    for (int i = 0; i < NUM_STEMS; ++i)
        stemStrips_[i]->setBounds(area.removeFromTop(stripHeight));
}

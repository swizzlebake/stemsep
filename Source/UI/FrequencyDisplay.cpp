#include "FrequencyDisplay.h"
#include <cmath>
#include <algorithm>

static constexpr int   NUM_FREQ_POINTS = 512;
static constexpr float DB_MAX          =  24.f;
static constexpr float DB_MIN          = -48.f;

FrequencyDisplay::FrequencyDisplay(StimSepProcessor& processor)
    : processor_(processor)
{
    const float logMin = std::log10(20.f);
    const float logMax = std::log10(20000.f);
    freqPoints_.resize(NUM_FREQ_POINTS);
    for (int i = 0; i < NUM_FREQ_POINTS; ++i)
    {
        const float t = (float)i / (float)(NUM_FREQ_POINTS - 1);
        freqPoints_[i] = std::pow(10.f, logMin + t * (logMax - logMin));
    }

    for (auto& m : magnitudes_)
        m.resize(NUM_FREQ_POINTS, 1.f);
    combinedMagnitudes_.resize(NUM_FREQ_POINTS, 0.f);

    startTimerHz(30);
}

FrequencyDisplay::~FrequencyDisplay()
{
    stopTimer();
}

void FrequencyDisplay::setStemColor(int idx, juce::Colour c)
{
    if (idx >= 0 && idx < NUM_STEMS)
        stemColors_[idx] = c;
}

void FrequencyDisplay::timerCallback()
{
    processor_.getMagnitudeResponses(freqPoints_, magnitudes_);

    for (int i = 0; i < NUM_FREQ_POINTS; ++i)
    {
        combinedMagnitudes_[i] = 0.f;
        for (int s = 0; s < NUM_STEMS; ++s)
            combinedMagnitudes_[i] += magnitudes_[s][i];
    }

    repaint();
}

void FrequencyDisplay::resized() {}

void FrequencyDisplay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0d0d1a));
    drawGrid(g);

    for (int s = 0; s < NUM_STEMS; ++s)
        drawCurve(g, magnitudes_[s], stemColors_[s].withAlpha(0.65f), 1.5f);

    drawCurve(g, combinedMagnitudes_, juce::Colours::white.withAlpha(0.85f), 2.0f);
}

void FrequencyDisplay::drawGrid(juce::Graphics& g)
{
    g.setColour(juce::Colours::white.withAlpha(0.08f));

    static const float gridFreqs[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
    for (float f : gridFreqs)
        g.drawVerticalLine((int)freqToX(f), 0.f, (float)getHeight());

    static const float gridDBs[] = { -48, -36, -24, -12, 0, 12, 24 };
    for (float db : gridDBs)
        g.drawHorizontalLine((int)dBToY(db), 0.f, (float)getWidth());

    // 0 dB line is slightly brighter
    g.setColour(juce::Colours::white.withAlpha(0.2f));
    g.drawHorizontalLine((int)dBToY(0.f), 0.f, (float)getWidth());

    // Frequency labels along the bottom
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.setFont(10.f);
    for (float f : gridFreqs)
    {
        const juce::String label = f >= 1000.f
            ? juce::String((int)(f / 1000.f)) + "k"
            : juce::String((int)f);
        g.drawText(label, (int)freqToX(f) - 15, getHeight() - 16, 30, 14,
                   juce::Justification::centred);
    }

    // dB labels along the left edge
    for (float db : gridDBs)
    {
        const juce::String label = juce::String((int)db) + " dB";
        g.drawText(label, 2, (int)dBToY(db) - 7, 40, 14,
                   juce::Justification::left);
    }
}

void FrequencyDisplay::drawCurve(juce::Graphics& g,
                                  const std::vector<float>& magnitudes,
                                  juce::Colour color, float strokeWidth)
{
    if (magnitudes.empty())
        return;

    juce::Path path;
    bool started = false;

    for (int i = 0; i < (int)freqPoints_.size(); ++i)
    {
        const float mag = magnitudes[i];
        const float db  = mag > 1e-10f ? 20.f * std::log10(mag) : DB_MIN;
        const float x   = freqToX(freqPoints_[i]);
        const float y   = dBToY(std::max(DB_MIN, std::min(DB_MAX, db)));

        if (!started) { path.startNewSubPath(x, y); started = true; }
        else          { path.lineTo(x, y); }
    }

    g.setColour(color);
    g.strokePath(path, juce::PathStrokeType(strokeWidth,
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

float FrequencyDisplay::freqToX(float freqHz) const
{
    const float logMin = std::log10(20.f);
    const float logMax = std::log10(20000.f);
    const float t = (std::log10(std::max(freqHz, 20.f)) - logMin) / (logMax - logMin);
    return t * (float)getWidth();
}

float FrequencyDisplay::dBToY(float dB) const
{
    const float t = (DB_MAX - dB) / (DB_MAX - DB_MIN);
    return t * (float)getHeight();
}

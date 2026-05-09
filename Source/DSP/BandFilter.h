#pragma once
#include <array>

// Second-order bandpass biquad (Audio EQ Cookbook constant-0dB-peak BPF).
// One instance per stem. Call prepareCoefficients() whenever freq/Q/gain change;
// processBlock() applies the BPF × gain and accumulates into caller-owned buffers.
class BandFilter
{
public:
    BandFilter();

    void prepare(double sampleRate, int maxBlockSize);
    void prepareCoefficients(float freqHz, float Q, float gainDB);
    void processBlock(const float* inL, const float* inR,
                      float* outL, float* outR, int numSamples);
    void reset();

    // Returns linear magnitude of BPF × gain at freqHz — used by the visualiser.
    float getMagnitudeForFrequency(float freqHz, double sampleRate) const;

private:
    // Current coefficients (linearly interpolated toward target each block)
    double b0 = 0.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;
    double gainLin_ = 1.0;

    // Target coefficients set by prepareCoefficients()
    double tb0 = 0.0, tb1 = 0.0, tb2 = 0.0;
    double ta1 = 0.0, ta2 = 0.0;
    double tGainLin_ = 1.0;

    struct State { double x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
    std::array<State, 2> state;

    double sampleRate_ = 44100.0;
};

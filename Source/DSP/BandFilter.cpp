#include "BandFilter.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
static constexpr double M_PI = 3.14159265358979323846;
#endif

BandFilter::BandFilter()
{
    reset();
}

void BandFilter::prepare(double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;
    reset();
}

void BandFilter::prepareCoefficients(float freqHz, float Q, float gainDB)
{
    const double fs = sampleRate_;
    const double f0 = std::max(20.0, std::min((double)freqHz, fs * 0.5 - 1.0));
    const double q  = std::max(0.01, (double)Q);

    const double A      = std::pow(10.0, (double)gainDB / 40.0);
    const double w0     = 2.0 * M_PI * f0 / fs;
    const double cosW0  = std::cos(w0);
    const double sinW0  = std::sin(w0);
    const double alpha  = sinW0 / (2.0 * q);
    const double a0_inv = 1.0 / (1.0 + alpha / A);

    tb0 = (1.0 + alpha * A) * a0_inv;
    tb1 = (-2.0 * cosW0)    * a0_inv;
    tb2 = (1.0 - alpha * A) * a0_inv;
    ta1 = (-2.0 * cosW0)    * a0_inv;
    ta2 = (1.0 - alpha / A) * a0_inv;
}

void BandFilter::processBlock(const float* inL, const float* inR,
                              float* outL, float* outR, int numSamples)
{
    // Linearly interpolate coefficients over the block to avoid zipper noise
    const double inv = numSamples > 1 ? 1.0 / (double)numSamples : 1.0;
    const double db0 = (tb0 - b0) * inv;
    const double db1 = (tb1 - b1) * inv;
    const double db2 = (tb2 - b2) * inv;
    const double da1 = (ta1 - a1) * inv;
    const double da2 = (ta2 - a2) * inv;

    for (int n = 0; n < numSamples; ++n)
    {
        b0 += db0; b1 += db1; b2 += db2;
        a1 += da1; a2 += da2;

        const float inputs[2] = { inL[n], inR[n] };
        for (int ch = 0; ch < 2; ++ch)
        {
            const double x = inputs[ch];
            auto& s = state[ch];
            const double y = b0 * x  + b1 * s.x1 + b2 * s.x2
                              - a1 * s.y1 - a2 * s.y2;
            s.x2 = s.x1; s.x1 = x;
            s.y2 = s.y1; s.y1 = y;
            if (ch == 0) outL[n] += (float)y;
            else         outR[n] += (float)y;
        }
    }

    // Snap to target to prevent floating-point drift
    b0 = tb0; b1 = tb1; b2 = tb2;
    a1 = ta1; a2 = ta2;
}

void BandFilter::reset()
{
    for (auto& s : state)
        s = {};
}

float BandFilter::getMagnitudeForFrequency(float freqHz, double sampleRate) const
{
    const double w  = 2.0 * M_PI * (double)freqHz / sampleRate;
    const double c1 = std::cos(w);
    const double s1 = std::sin(w);
    const double c2 = std::cos(2.0 * w);
    const double s2 = std::sin(2.0 * w);

    // Use target coefficients so the visualiser reflects parameter changes immediately.
    const double numR = tb0 + tb1 * c1 + tb2 * c2;
    const double numI =     - tb1 * s1 - tb2 * s2;
    const double denR = 1.0 + ta1 * c1 + ta2 * c2;
    const double denI =     - ta1 * s1 - ta2 * s2;

    const double numMag = std::sqrt(numR * numR + numI * numI);
    const double denMag = std::sqrt(denR * denR + denI * denI);

    return (float)(numMag / (denMag + 1e-10));
}

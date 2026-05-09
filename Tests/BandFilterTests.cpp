#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "DSP/BandFilter.h"
#include <cmath>
#include <vector>

// ── test helpers ────────────────────────────────────────────────────────────

static constexpr double kSR   = 44100.0;
static constexpr float  kSR_f = 44100.f;
static constexpr float  kPi   = 3.14159265f;

static std::vector<float> makeSine(float freqHz, float sr, int n)
{
    std::vector<float> v(n);
    for (int i = 0; i < n; ++i)
        v[i] = std::sin(2.f * kPi * freqHz * (float)i / sr);
    return v;
}

// RMS of the second half of a buffer (skips the transient)
static float rms(const std::vector<float>& v)
{
    const int half = (int)v.size() / 2;
    float sum = 0.f;
    for (int i = half; i < (int)v.size(); ++i)
        sum += v[i] * v[i];
    return std::sqrt(sum / (float)(v.size() - half));
}

// Feed a sine through a prepared filter and return steady-state magnitude
// (output RMS / input RMS, both measured in the second half of the block).
// Two passes: first pass settles the coefficient interpolation to the target,
// second pass measures the true steady-state response.
static float measuredMagnitude(BandFilter& f, float freqHz, int n = 16384)
{
    auto in = makeSine(freqHz, kSR_f, n);
    std::vector<float> outL(n, 0.f), outR(n, 0.f);
    f.processBlock(in.data(), in.data(), outL.data(), outR.data(), n);   // settle

    std::fill(outL.begin(), outL.end(), 0.f);
    std::fill(outR.begin(), outR.end(), 0.f);
    f.processBlock(in.data(), in.data(), outL.data(), outR.data(), n);   // measure

    const float inputRms  = rms(in);
    const float outputRms = rms(outL);
    return inputRms > 0.f ? outputRms / inputRms : 0.f;
}

// ── frequency response ──────────────────────────────────────────────────────

TEST_CASE("BandFilter: 0 dB gain is unity at all frequencies", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);
    f.prepareCoefficients(1000.f, 1.f, 0.f);

    for (float freq : { 50.f, 200.f, 1000.f, 5000.f, 15000.f })
        CHECK_THAT(f.getMagnitudeForFrequency(freq, kSR),
                   Catch::Matchers::WithinAbs(1.f, 0.001f));
}

TEST_CASE("BandFilter: +12 dB boosts centre frequency by ~3.98x", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);
    f.prepareCoefficients(1000.f, 2.f, 12.f);

    const float expected = std::pow(10.f, 12.f / 20.f); // ≈ 3.981
    CHECK_THAT(f.getMagnitudeForFrequency(1000.f, kSR),
               Catch::Matchers::WithinRel(expected, 0.01f));
}

TEST_CASE("BandFilter: -12 dB cuts centre frequency by ~0.25x", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);
    f.prepareCoefficients(1000.f, 2.f, -12.f);

    const float expected = std::pow(10.f, -12.f / 20.f); // ≈ 0.251
    CHECK_THAT(f.getMagnitudeForFrequency(1000.f, kSR),
               Catch::Matchers::WithinRel(expected, 0.01f));
}

TEST_CASE("BandFilter: -48 dB gain nearly silences centre", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);
    f.prepareCoefficients(1000.f, 2.f, -48.f);

    // < -46 dB in practice due to coefficient precision
    CHECK(f.getMagnitudeForFrequency(1000.f, kSR) < 0.005f);
}

TEST_CASE("BandFilter: high Q concentrates boost to a narrow band", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);
    f.prepareCoefficients(1000.f, 10.f, 12.f);

    const float atCentre   = f.getMagnitudeForFrequency(1000.f, kSR);
    const float atOctaveUp = f.getMagnitudeForFrequency(2000.f, kSR);
    const float atOctaveDn = f.getMagnitudeForFrequency(500.f,  kSR);

    CHECK(atCentre > atOctaveUp * 2.f);
    CHECK(atCentre > atOctaveDn * 2.f);
}

TEST_CASE("BandFilter: low Q spreads boost across a wide band", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);
    f.prepareCoefficients(1000.f, 0.1f, 12.f);

    const float atCentre   = f.getMagnitudeForFrequency(1000.f, kSR);
    const float atOctaveUp = f.getMagnitudeForFrequency(2000.f, kSR);

    // With Q=0.1 the octave above is still substantially boosted
    CHECK(atOctaveUp > 1.5f);
    // Centre still has the highest magnitude
    CHECK(atCentre >= atOctaveUp);
}

TEST_CASE("BandFilter: magnitude is finite for all extreme parameter combinations", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);

    for (float freq : { 20.f, 1000.f, 20000.f })
    for (float gain : { -48.f, 0.f, 24.f })
    for (float q    : { 0.1f, 1.f, 10.f })
    {
        f.prepareCoefficients(freq, q, gain);
        INFO("freq=" << freq << "  gain=" << gain << "  Q=" << q);
        const float m = f.getMagnitudeForFrequency(freq, kSR);
        CHECK(std::isfinite(m));
        CHECK(m >= 0.f);
    }
}

TEST_CASE("BandFilter: frequency below 20 Hz is clamped without crash or NaN", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);
    REQUIRE_NOTHROW(f.prepareCoefficients(-500.f, 1.f, 0.f));
    CHECK(std::isfinite(f.getMagnitudeForFrequency(20.f, kSR)));
}

TEST_CASE("BandFilter: frequency above Nyquist is clamped without crash or NaN", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);
    REQUIRE_NOTHROW(f.prepareCoefficients(100000.f, 1.f, 0.f));
    CHECK(std::isfinite(f.getMagnitudeForFrequency(1000.f, kSR)));
}

TEST_CASE("BandFilter: different sample rates produce different responses near Nyquist", "[dsp]")
{
    BandFilter f44, f48;
    f44.prepare(44100.0, 512);
    f48.prepare(48000.0, 512);
    f44.prepareCoefficients(18000.f, 2.f, 12.f);
    f48.prepareCoefficients(18000.f, 2.f, 12.f);

    // Evaluate both at 18 kHz using the SAME reference rate (44100).
    // f44 sees 18 kHz exactly at its centre (peak gain); f48 sees it above its centre
    // (designed at 18k/48k normalised, evaluated at 18k/44100 normalised).
    const float m44 = f44.getMagnitudeForFrequency(18000.f, 44100.0);
    const float m48 = f48.getMagnitudeForFrequency(18000.f, 44100.0);
    CHECK(std::abs(m44 - m48) > 0.1f);
}

// ── processBlock behaviour ───────────────────────────────────────────────────

TEST_CASE("BandFilter: processBlock accumulates into output, does not overwrite", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);
    f.prepareCoefficients(1000.f, 1.f, 0.f); // unity gain

    const int N = 512;
    auto in = makeSine(440.f, kSR_f, N);

    // Pre-fill output with 1.0 — accumulation means some samples will exceed 1
    std::vector<float> outL(N, 1.f), outR(N, 1.f);
    f.processBlock(in.data(), in.data(), outL.data(), outR.data(), N);

    bool anyAboveOne = false;
    for (float v : outL)
        if (v > 1.01f) { anyAboveOne = true; break; }

    CHECK(anyAboveOne); // overwrite would keep everything ≤ 1
}

TEST_CASE("BandFilter: reset clears filter memory — silence in gives silence out", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);
    f.prepareCoefficients(1000.f, 1.f, 12.f);

    // Build up state with a burst
    const int N = 512;
    auto burst = makeSine(1000.f, kSR_f, N);
    std::vector<float> discard(N, 0.f);
    f.processBlock(burst.data(), burst.data(), discard.data(), discard.data(), N);

    f.reset();

    // Process silence — with clean state, output must be zero
    std::vector<float> silence(N, 0.f), outL(N, 0.f), outR(N, 0.f);
    f.processBlock(silence.data(), silence.data(), outL.data(), outR.data(), N);

    for (int i = 0; i < N; ++i)
        CHECK_THAT(outL[i], Catch::Matchers::WithinAbs(0.f, 1e-6f));
}

TEST_CASE("BandFilter: getMagnitudeForFrequency matches steady-state processBlock output", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);
    f.prepareCoefficients(2000.f, 2.f, 10.f);

    const float predicted = f.getMagnitudeForFrequency(2000.f, kSR);
    const float measured  = measuredMagnitude(f, 2000.f);

    CHECK_THAT(measured, Catch::Matchers::WithinRel(predicted, 0.05f));
}

TEST_CASE("BandFilter: left and right channels are processed identically", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);
    f.prepareCoefficients(1000.f, 2.f, 8.f);

    const int N = 512;
    auto in = makeSine(1000.f, kSR_f, N);
    std::vector<float> outL(N, 0.f), outR(N, 0.f);
    f.processBlock(in.data(), in.data(), outL.data(), outR.data(), N);

    for (int i = 0; i < N; ++i)
        CHECK_THAT(outL[i], Catch::Matchers::WithinAbs(outR[i], 1e-6f));
}

TEST_CASE("BandFilter: coefficient interpolation — gain ramps smoothly across the block", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);

    // Settle at 0 dB so current == target
    f.prepareCoefficients(1000.f, 1.f, 0.f);
    const int N = 1024;
    auto in = makeSine(1000.f, kSR_f, N);
    std::vector<float> warm(N, 0.f), tmp(N, 0.f);
    f.processBlock(in.data(), in.data(), warm.data(), tmp.data(), N);

    // Switch to 12 dB — coefficients should interpolate across the next block
    f.prepareCoefficients(1000.f, 1.f, 12.f);
    std::vector<float> out(N, 0.f), tmp2(N, 0.f);
    f.processBlock(in.data(), in.data(), out.data(), tmp2.data(), N);

    // RMS of first quarter should still be near 0 dB (smooth start)
    float sumQ1 = 0.f;
    for (int i = 0; i < N / 4; ++i) sumQ1 += out[i] * out[i];
    const float rmsQ1 = std::sqrt(sumQ1 * 4.f / (float)N);

    // RMS of last quarter should be significantly larger (approaching 12 dB ≈ 3.98x)
    float sumQ4 = 0.f;
    for (int i = 3 * N / 4; i < N; ++i) sumQ4 += out[i] * out[i];
    const float rmsQ4 = std::sqrt(sumQ4 * 4.f / (float)N);

    // Amplitude ramps up: last quarter is noticeably louder than first quarter
    CHECK(rmsQ4 > rmsQ1 * 1.5f);
    // First quarter stays near 0 dB steady state (unit-amplitude sine RMS ≈ 0.707)
    CHECK_THAT(rmsQ1, Catch::Matchers::WithinAbs(0.707f, 0.25f));
}

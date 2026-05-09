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

static float rms(const std::vector<float>& v)
{
    const int half = (int)v.size() / 2;
    float sum = 0.f;
    for (int i = half; i < (int)v.size(); ++i)
        sum += v[i] * v[i];
    return std::sqrt(sum / (float)(v.size() - half));
}

// Two processBlock passes: first settles coefficient interpolation,
// second measures the true steady-state BPF response.
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

TEST_CASE("BandFilter: BPF passes centre frequency at unity (0 dB gain)", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);
    f.prepareCoefficients(1000.f, 2.f, 0.f);

    CHECK_THAT(f.getMagnitudeForFrequency(1000.f, kSR),
               Catch::Matchers::WithinAbs(1.f, 0.01f));
}

TEST_CASE("BandFilter: BPF attenuates frequencies far from centre", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);
    f.prepareCoefficients(1000.f, 2.f, 0.f);

    // 3 octaves away (125 Hz and 8000 Hz) must be strongly attenuated
    CHECK(f.getMagnitudeForFrequency(125.f,  kSR) < 0.15f);
    CHECK(f.getMagnitudeForFrequency(8000.f, kSR) < 0.15f);
}

TEST_CASE("BandFilter: +6 dB gain doubles the magnitude at centre", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);
    f.prepareCoefficients(1000.f, 2.f, 6.f);

    const float expected = std::pow(10.f, 6.f / 20.f); // ≈ 2.0
    CHECK_THAT(f.getMagnitudeForFrequency(1000.f, kSR),
               Catch::Matchers::WithinRel(expected, 0.01f));
}

TEST_CASE("BandFilter: -6 dB gain halves the magnitude at centre", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);
    f.prepareCoefficients(1000.f, 2.f, -6.f);

    const float expected = std::pow(10.f, -6.f / 20.f); // ≈ 0.5
    CHECK_THAT(f.getMagnitudeForFrequency(1000.f, kSR),
               Catch::Matchers::WithinRel(expected, 0.01f));
}

TEST_CASE("BandFilter: high Q produces a narrower passband", "[dsp]")
{
    BandFilter fNarrow, fWide;
    fNarrow.prepare(kSR, 512);
    fWide.prepare(kSR, 512);
    fNarrow.prepareCoefficients(1000.f, 8.f, 0.f);
    fWide.prepareCoefficients(1000.f, 0.5f, 0.f);

    // One octave away: narrow filter attenuates more than wide filter
    const float narrowAtOctave = fNarrow.getMagnitudeForFrequency(2000.f, kSR);
    const float wideAtOctave   = fWide.getMagnitudeForFrequency(2000.f, kSR);
    CHECK(narrowAtOctave < wideAtOctave);
    // Centre is unity for both
    CHECK_THAT(fNarrow.getMagnitudeForFrequency(1000.f, kSR),
               Catch::Matchers::WithinAbs(1.f, 0.01f));
    CHECK_THAT(fWide.getMagnitudeForFrequency(1000.f, kSR),
               Catch::Matchers::WithinAbs(1.f, 0.01f));
}

TEST_CASE("BandFilter: low Q passes a wide range of frequencies", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);
    f.prepareCoefficients(1000.f, 0.3f, 0.f);

    // With very low Q, one octave up is still substantially passed
    CHECK(f.getMagnitudeForFrequency(2000.f, kSR) > 0.4f);
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
    f44.prepareCoefficients(18000.f, 2.f, 0.f);
    f48.prepareCoefficients(18000.f, 2.f, 0.f);

    // Evaluate both at 18 kHz using the same reference rate (44100).
    // f44 sees 18 kHz at its centre (peak gain ≈ 1); f48 sees it above its centre.
    const float m44 = f44.getMagnitudeForFrequency(18000.f, 44100.0);
    const float m48 = f48.getMagnitudeForFrequency(18000.f, 44100.0);
    CHECK(std::abs(m44 - m48) > 0.05f);
}

// ── processBlock behaviour ───────────────────────────────────────────────────

TEST_CASE("BandFilter: processBlock accumulates into output, does not overwrite", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);
    f.prepareCoefficients(1000.f, 2.f, 0.f);

    const int N = 512;
    auto in = makeSine(1000.f, kSR_f, N);

    // Pre-fill output with 1.0 — accumulation means the BPF output adds on top
    std::vector<float> outL(N, 1.f), outR(N, 1.f);

    // Settle coefficients first so the second block is at steady state
    std::vector<float> tmp(N, 0.f);
    f.processBlock(in.data(), in.data(), tmp.data(), tmp.data(), N);

    std::fill(outL.begin(), outL.end(), 1.f);
    std::fill(outR.begin(), outR.end(), 1.f);
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
    f.prepareCoefficients(1000.f, 2.f, 0.f);

    const int N = 512;
    auto burst = makeSine(1000.f, kSR_f, N);
    std::vector<float> discard(N, 0.f);
    f.processBlock(burst.data(), burst.data(), discard.data(), discard.data(), N);

    f.reset();

    std::vector<float> silence(N, 0.f), outL(N, 0.f), outR(N, 0.f);
    f.processBlock(silence.data(), silence.data(), outL.data(), outR.data(), N);

    for (int i = 0; i < N; ++i)
        CHECK_THAT(outL[i], Catch::Matchers::WithinAbs(0.f, 1e-6f));
}

TEST_CASE("BandFilter: getMagnitudeForFrequency matches steady-state processBlock output", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);
    f.prepareCoefficients(2000.f, 2.f, 0.f);

    const float predicted = f.getMagnitudeForFrequency(2000.f, kSR);
    const float measured  = measuredMagnitude(f, 2000.f);

    CHECK_THAT(measured, Catch::Matchers::WithinRel(predicted, 0.05f));
}

TEST_CASE("BandFilter: left and right channels are processed identically", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);
    f.prepareCoefficients(1000.f, 2.f, 0.f);

    const int N = 512;
    auto in = makeSine(1000.f, kSR_f, N);

    // Settle first
    std::vector<float> t(N, 0.f);
    f.processBlock(in.data(), in.data(), t.data(), t.data(), N);

    std::vector<float> outL(N, 0.f), outR(N, 0.f);
    f.processBlock(in.data(), in.data(), outL.data(), outR.data(), N);

    for (int i = 0; i < N; ++i)
        CHECK_THAT(outL[i], Catch::Matchers::WithinAbs(outR[i], 1e-6f));
}

TEST_CASE("BandFilter: coefficient interpolation — gain ramps smoothly across the block", "[dsp]")
{
    BandFilter f;
    f.prepare(kSR, 512);
    f.prepareCoefficients(1000.f, 2.f, 0.f);

    const int N = 1024;
    auto in = makeSine(1000.f, kSR_f, N);
    std::vector<float> warm(N, 0.f), tmp(N, 0.f);
    f.processBlock(in.data(), in.data(), warm.data(), tmp.data(), N);

    // Switch to +12 dB level — output amplitude should ramp up across the block
    f.prepareCoefficients(1000.f, 2.f, 12.f);
    std::vector<float> out(N, 0.f), tmp2(N, 0.f);
    f.processBlock(in.data(), in.data(), out.data(), tmp2.data(), N);

    // RMS of first quarter should still be near 0 dB steady-state
    float sumQ1 = 0.f;
    for (int i = 0; i < N / 4; ++i) sumQ1 += out[i] * out[i];
    const float rmsQ1 = std::sqrt(sumQ1 * 4.f / (float)N);

    // RMS of last quarter should be larger (approaching +12 dB)
    float sumQ4 = 0.f;
    for (int i = 3 * N / 4; i < N; ++i) sumQ4 += out[i] * out[i];
    const float rmsQ4 = std::sqrt(sumQ4 * 4.f / (float)N);

    CHECK(rmsQ4 > rmsQ1 * 1.5f);
    CHECK_THAT(rmsQ1, Catch::Matchers::WithinAbs(0.707f, 0.25f));
}

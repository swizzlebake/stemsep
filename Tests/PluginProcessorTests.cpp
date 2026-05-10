#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "PluginProcessor.h"
#include <juce_audio_basics/juce_audio_basics.h>

// ── parameter registration ───────────────────────────────────────────────────

TEST_CASE("StemSepProcessor: all 20 parameters are registered", "[processor]")
{
    StemSepProcessor p;
    auto& apvts = p.getAPVTS();

    for (int i = 0; i < NUM_STEMS; ++i)
    {
        const auto id = juce::String(i);
        CHECK(apvts.getParameter("freq"   + id) != nullptr);
        CHECK(apvts.getParameter("q"      + id) != nullptr);
        CHECK(apvts.getParameter("gain"   + id) != nullptr);
        CHECK(apvts.getParameter("enable" + id) != nullptr);
    }
}

TEST_CASE("StemSepProcessor: default parameter values match instrument defaults", "[processor]")
{
    StemSepProcessor p;
    auto& apvts = p.getAPVTS();

    // Expected instrument defaults: Drums=120, Bass=250, Guitar=1500, Vocals=4000, Other=12000
    const float expectedFreq[NUM_STEMS] = { 120.f, 250.f, 1500.f, 4000.f, 12000.f };
    const float expectedQ[NUM_STEMS]    = { 0.7f,  1.5f,  1.5f,   1.5f,   0.7f   };

    for (int i = 0; i < NUM_STEMS; ++i)
    {
        const auto id = juce::String(i);

        const float freq = apvts.getRawParameterValue("freq" + id)->load();
        CHECK_THAT(freq, Catch::Matchers::WithinRel(expectedFreq[i], 0.02f));

        const float q = apvts.getRawParameterValue("q" + id)->load();
        CHECK_THAT(q, Catch::Matchers::WithinAbs(expectedQ[i], 0.05f));

        const float gain = apvts.getRawParameterValue("gain" + id)->load();
        CHECK_THAT(gain, Catch::Matchers::WithinAbs(0.f, 0.01f));

        CHECK(apvts.getRawParameterValue("enable" + id)->load() > 0.5f);
    }
}

// ── bus layout ───────────────────────────────────────────────────────────────

TEST_CASE("StemSepProcessor: has 1 input bus and 1 main + NUM_STEMS output buses", "[processor]")
{
    StemSepProcessor p;
    CHECK(p.getBusCount(true)  == 1);
    CHECK(p.getBusCount(false) == 1 + NUM_STEMS);
}

TEST_CASE("StemSepProcessor: stem output buses are enabled by default", "[processor]")
{
    StemSepProcessor p;
    for (int i = 0; i < NUM_STEMS; ++i)
    {
        const auto* bus = p.getBus(false, i + 1);
        REQUIRE(bus != nullptr);
        CHECK(bus->isEnabled());
    }
}

TEST_CASE("StemSepProcessor: main input bus is enabled and stereo", "[processor]")
{
    StemSepProcessor p;
    const auto* bus = p.getBus(true, 0);
    REQUIRE(bus != nullptr);
    CHECK(bus->isEnabled());
    CHECK(bus->getDefaultLayout() == juce::AudioChannelSet::stereo());
}

TEST_CASE("StemSepProcessor: isBusesLayoutSupported rejects mono output", "[processor]")
{
    StemSepProcessor p;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::stereo());
    layout.outputBuses.add(juce::AudioChannelSet::mono());
    CHECK_FALSE(p.isBusesLayoutSupported(layout));
}

TEST_CASE("StemSepProcessor: isBusesLayoutSupported accepts stereo in + stereo out", "[processor]")
{
    StemSepProcessor p;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::stereo());
    layout.outputBuses.add(juce::AudioChannelSet::stereo());
    CHECK(p.isBusesLayoutSupported(layout));
}

// ── processBlock behaviour ───────────────────────────────────────────────────

TEST_CASE("StemSepProcessor: silence in gives silence out when all stems disabled", "[processor]")
{
    StemSepProcessor p;
    p.prepareToPlay(44100.0, 512);

    for (int i = 0; i < NUM_STEMS; ++i)
        p.getAPVTS().getParameter("enable" + juce::String(i))
            ->setValueNotifyingHost(0.f);

    juce::AudioBuffer<float> buffer(p.getTotalNumOutputChannels(), 512);
    buffer.clear();
    juce::MidiBuffer midi;
    p.processBlock(buffer, midi);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < 512; ++i)
            CHECK_THAT(buffer.getSample(ch, i), Catch::Matchers::WithinAbs(0.f, 1e-6f));
}

TEST_CASE("StemSepProcessor: single stereo input passes through enabled BPF stems", "[processor]")
{
    StemSepProcessor p;
    p.prepareToPlay(44100.0, 512);

    // Enable only stem 0 (Drums, centre ≈ 120 Hz), set gain to 0 dB
    for (int i = 1; i < NUM_STEMS; ++i)
        p.getAPVTS().getParameter("enable" + juce::String(i))
            ->setValueNotifyingHost(0.f);

    // Fill buffer with a 120 Hz sine on the input channels (0/1 = stereo input,
    // shared with Main output bus). Other channels are stem outputs — leave at zero.
    juce::AudioBuffer<float> buffer(p.getTotalNumOutputChannels(), 512);
    buffer.clear();
    const float sr = 44100.f;
    auto fillInput = [&] {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buffer.setSample(ch, i, std::sin(2.f * 3.14159265f * 120.f * i / sr));
    };
    fillInput();

    juce::MidiBuffer midi;
    // Settle: two processBlock passes so BPF is at steady state
    p.processBlock(buffer, midi);

    // Re-fill input for measurement block
    fillInput();
    p.processBlock(buffer, midi);

    // At 120 Hz (near BPF centre), output should be non-trivial
    float sumSq = 0.f;
    for (int i = 0; i < 512; ++i) sumSq += buffer.getSample(0, i) * buffer.getSample(0, i);
    const float outRms = std::sqrt(sumSq / 512.f);
    CHECK(outRms > 0.1f);
}

// ── state serialisation ───────────────────────────────────────────────────────

TEST_CASE("StemSepProcessor: getStateInformation / setStateInformation round-trips", "[processor]")
{
    StemSepProcessor p;

    // Set non-default values for two parameters
    p.getAPVTS().getParameter("freq0")->setValueNotifyingHost(
        p.getAPVTS().getParameter("freq0")->convertTo0to1(500.f));
    p.getAPVTS().getParameter("gain1")->setValueNotifyingHost(
        p.getAPVTS().getParameter("gain1")->convertTo0to1(6.f));

    juce::MemoryBlock block;
    p.getStateInformation(block);

    StemSepProcessor p2;
    p2.setStateInformation(block.getData(), (int)block.getSize());

    const float restoredFreq = p2.getAPVTS().getRawParameterValue("freq0")->load();
    const float restoredGain = p2.getAPVTS().getRawParameterValue("gain1")->load();

    CHECK_THAT(restoredFreq, Catch::Matchers::WithinAbs(500.f, 5.f));
    CHECK_THAT(restoredGain, Catch::Matchers::WithinAbs(6.f, 0.5f));
}

// ── getMagnitudeResponses ─────────────────────────────────────────────────────

TEST_CASE("StemSepProcessor: getMagnitudeResponses returns finite values for all stems", "[processor]")
{
    StemSepProcessor p;
    p.prepareToPlay(44100.0, 512);

    const std::vector<float> freqPoints = { 100.f, 500.f, 1000.f, 5000.f, 15000.f };
    std::array<std::vector<float>, NUM_STEMS> mags;

    p.getMagnitudeResponses(freqPoints, mags);

    for (int s = 0; s < NUM_STEMS; ++s)
    {
        REQUIRE((int)mags[s].size() == (int)freqPoints.size());
        for (float m : mags[s])
        {
            CHECK(std::isfinite(m));
            CHECK(m >= 0.f);
        }
    }
}

TEST_CASE("StemSepProcessor: getMagnitudeResponses peaks near each stem's centre frequency", "[processor]")
{
    StemSepProcessor p;
    p.prepareToPlay(44100.0, 512);

    // Expected centres from kDefaultFreq: 120, 250, 1500, 4000, 12000 Hz
    const float centres[NUM_STEMS] = { 120.f, 250.f, 1500.f, 4000.f, 12000.f };

    for (int s = 0; s < NUM_STEMS; ++s)
    {
        p.refreshCoefficients();
        const std::vector<float> pts = { centres[s] };
        std::array<std::vector<float>, NUM_STEMS> mags;
        p.getMagnitudeResponses(pts, mags);

        // At centre frequency with 0 dB gain, BPF magnitude ≈ 1.0
        CHECK_THAT(mags[s][0], Catch::Matchers::WithinAbs(1.f, 0.02f));
    }
}

TEST_CASE("StemSepProcessor: getMagnitudeResponses reflects a gain change", "[processor]")
{
    StemSepProcessor p;
    p.prepareToPlay(44100.0, 512);

    p.getAPVTS().getParameter("freq0")->setValueNotifyingHost(
        p.getAPVTS().getParameter("freq0")->convertTo0to1(1000.f));
    p.getAPVTS().getParameter("gain0")->setValueNotifyingHost(
        p.getAPVTS().getParameter("gain0")->convertTo0to1(6.f));

    p.refreshCoefficients();

    const std::vector<float> freqPoints = { 1000.f };
    std::array<std::vector<float>, NUM_STEMS> mags;
    p.getMagnitudeResponses(freqPoints, mags);

    const float expected = std::pow(10.f, 6.f / 20.f); // ≈ 2.0
    CHECK_THAT(mags[0][0], Catch::Matchers::WithinRel(expected, 0.05f));
}

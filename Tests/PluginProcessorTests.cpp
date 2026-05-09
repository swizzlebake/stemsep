#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "PluginProcessor.h"
#include <juce_audio_basics/juce_audio_basics.h>

// ── parameter registration ───────────────────────────────────────────────────

TEST_CASE("StemSepProcessor: all 16 parameters are registered", "[processor]")
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

TEST_CASE("StemSepProcessor: default parameter values are correct", "[processor]")
{
    StemSepProcessor p;
    auto& apvts = p.getAPVTS();

    for (int i = 0; i < NUM_STEMS; ++i)
    {
        const auto id = juce::String(i);

        // gain default = 0 dB
        const float gainDefault = apvts.getRawParameterValue("gain" + id)->load();
        CHECK_THAT(gainDefault, Catch::Matchers::WithinAbs(0.f, 0.01f));

        // freq default = 1000 Hz
        const float freqDefault = apvts.getRawParameterValue("freq" + id)->load();
        CHECK_THAT(freqDefault, Catch::Matchers::WithinAbs(1000.f, 1.f));

        // q default = 1.0
        const float qDefault = apvts.getRawParameterValue("q" + id)->load();
        CHECK_THAT(qDefault, Catch::Matchers::WithinAbs(1.f, 0.01f));

        // enable default = true (1.0)
        const float enableDefault = apvts.getRawParameterValue("enable" + id)->load();
        CHECK(enableDefault > 0.5f);
    }
}

// ── bus layout ───────────────────────────────────────────────────────────────

TEST_CASE("StemSepProcessor: has 5 input buses and 1 output bus", "[processor]")
{
    StemSepProcessor p;
    CHECK(p.getBusCount(true)  == 5);
    CHECK(p.getBusCount(false) == 1);
}

TEST_CASE("StemSepProcessor: bus 0 is enabled (dummy main); stem buses start disabled", "[processor]")
{
    StemSepProcessor p;
    CHECK(p.getBus(true, 0) != nullptr);
    CHECK(p.getBus(true, 0)->isEnabled());

    // Buses 2-4 start disabled by default
    for (int i = 2; i <= 4; ++i)
    {
        const auto* bus = p.getBus(true, i);
        CHECK(bus != nullptr);
        CHECK_FALSE(bus->isEnabled());
    }
}

TEST_CASE("StemSepProcessor: isBusesLayoutSupported rejects mono output", "[processor]")
{
    StemSepProcessor p;
    juce::AudioProcessor::BusesLayout layout;
    layout.outputBuses.add(juce::AudioChannelSet::mono());
    layout.inputBuses.add(juce::AudioChannelSet::stereo()); // bus 0
    layout.inputBuses.add(juce::AudioChannelSet::stereo()); // bus 1
    layout.inputBuses.add(juce::AudioChannelSet::disabled());
    layout.inputBuses.add(juce::AudioChannelSet::disabled());
    layout.inputBuses.add(juce::AudioChannelSet::disabled());
    CHECK_FALSE(p.isBusesLayoutSupported(layout));
}

TEST_CASE("StemSepProcessor: isBusesLayoutSupported accepts all-stereo layout", "[processor]")
{
    StemSepProcessor p;
    juce::AudioProcessor::BusesLayout layout;
    layout.outputBuses.add(juce::AudioChannelSet::stereo());
    for (int i = 0; i < 5; ++i)
        layout.inputBuses.add(juce::AudioChannelSet::stereo());
    CHECK(p.isBusesLayoutSupported(layout));
}

// ── processBlock behaviour ───────────────────────────────────────────────────

TEST_CASE("StemSepProcessor: silence in gives silence out after prepare", "[processor]")
{
    StemSepProcessor p;
    p.prepareToPlay(44100.0, 512);

    // Disable all stems so no sidechain routing is needed
    for (int i = 0; i < NUM_STEMS; ++i)
        p.getAPVTS().getParameter("enable" + juce::String(i))
            ->setValueNotifyingHost(0.f);

    // processBlock needs a buffer sized for all buses; use the minimum (main bus only)
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();

    juce::MidiBuffer midi;
    p.processBlock(buffer, midi);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            CHECK_THAT(buffer.getSample(ch, i), Catch::Matchers::WithinAbs(0.f, 1e-6f));
}

// ── state serialisation ───────────────────────────────────────────────────────

TEST_CASE("StemSepProcessor: getStateInformation / setStateInformation round-trips", "[processor]")
{
    StemSepProcessor p;

    // Set non-default values
    p.getAPVTS().getParameter("freq0")->setValueNotifyingHost(
        p.getAPVTS().getParameter("freq0")->convertTo0to1(500.f));
    p.getAPVTS().getParameter("gain1")->setValueNotifyingHost(
        p.getAPVTS().getParameter("gain1")->convertTo0to1(6.f));

    juce::MemoryBlock block;
    p.getStateInformation(block);

    // Create a fresh instance and restore
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

TEST_CASE("StemSepProcessor: getMagnitudeResponses at 0 dB gain returns ~1 at all points", "[processor]")
{
    StemSepProcessor p;
    p.prepareToPlay(44100.0, 512);

    // All gains default to 0 dB
    const std::vector<float> freqPoints = { 200.f, 1000.f, 5000.f };
    std::array<std::vector<float>, NUM_STEMS> mags;
    p.getMagnitudeResponses(freqPoints, mags);

    for (int s = 0; s < NUM_STEMS; ++s)
        for (float m : mags[s])
            CHECK_THAT(m, Catch::Matchers::WithinAbs(1.f, 0.002f));
}

TEST_CASE("StemSepProcessor: getMagnitudeResponses reflects a gain change", "[processor]")
{
    StemSepProcessor p;
    p.prepareToPlay(44100.0, 512);

    p.getAPVTS().getParameter("freq0")->setValueNotifyingHost(
        p.getAPVTS().getParameter("freq0")->convertTo0to1(1000.f));
    p.getAPVTS().getParameter("gain0")->setValueNotifyingHost(
        p.getAPVTS().getParameter("gain0")->convertTo0to1(12.f));

    // Push parameters into the filter objects (normally done inside processBlock)
    p.refreshCoefficients();

    const std::vector<float> freqPoints = { 1000.f };
    std::array<std::vector<float>, NUM_STEMS> mags;
    p.getMagnitudeResponses(freqPoints, mags);

    const float expected = std::pow(10.f, 12.f / 20.f);
    CHECK_THAT(mags[0][0], Catch::Matchers::WithinRel(expected, 0.05f));
}

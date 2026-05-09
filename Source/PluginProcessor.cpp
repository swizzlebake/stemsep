#include "PluginProcessor.h"
#include "PluginEditor.h"

StemSepProcessor::StemSepProcessor()
    : AudioProcessor(
        BusesProperties()
            .withInput ("Input",  juce::AudioChannelSet::stereo(), true)   // main bus — not processed; safety guard against "Main" sidechain routing
            .withInput ("Stem 1", juce::AudioChannelSet::stereo(), true)   // aux sidechain inputs
            .withInput ("Stem 2", juce::AudioChannelSet::stereo(), false)
            .withInput ("Stem 3", juce::AudioChannelSet::stereo(), false)
            .withInput ("Stem 4", juce::AudioChannelSet::stereo(), false)
            .withOutput("Output",  juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout())
{
    for (int i = 0; i < NUM_STEMS; ++i)
    {
        const auto id = juce::String(i);
        freqParams[i]   = apvts.getRawParameterValue("freq"   + id);
        qParams[i]      = apvts.getRawParameterValue("q"      + id);
        gainParams[i]   = apvts.getRawParameterValue("gain"   + id);
        enableParams[i] = apvts.getRawParameterValue("enable" + id);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout
StemSepProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 0; i < NUM_STEMS; ++i)
    {
        const auto id  = juce::String(i);
        const auto num = juce::String(i + 1);

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"freq" + id, 1},
            "Stem " + num + " Freq",
            juce::NormalisableRange<float>(20.f, 20000.f, 0.f, 0.25f),
            1000.f));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"q" + id, 1},
            "Stem " + num + " Q",
            juce::NormalisableRange<float>(0.1f, 10.f, 0.f, 0.5f),
            1.0f));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"gain" + id, 1},
            "Stem " + num + " Gain",
            juce::NormalisableRange<float>(-48.f, 24.f),
            0.f));

        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{"enable" + id, 1},
            "Stem " + num + " Enable",
            true));
    }

    return layout;
}

void StemSepProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    for (auto& f : filters)
        f.prepare(sampleRate, samplesPerBlock);
}

void StemSepProcessor::releaseResources()
{
    for (auto& f : filters)
        f.reset();
}

bool StemSepProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    for (const auto& bus : layouts.inputBuses)
    {
        if (bus != juce::AudioChannelSet::stereo() &&
            bus != juce::AudioChannelSet::disabled())
            return false;
    }
    return true;
}

void StemSepProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    auto outputBuf = getBusBuffer(buffer, false, 0);
    outputBuf.clear();
    float* outL = outputBuf.getWritePointer(0);
    float* outR = outputBuf.getWritePointer(1);

    for (int stemIdx = 0; stemIdx < NUM_STEMS; ++stemIdx)
    {
        if (enableParams[stemIdx]->load() < 0.5f)
            continue;

        const int busIdx = stemIdx + 1; // bus 0 is the dummy main — stems start at bus 1
        const auto* bus = getBus(true, busIdx);
        if (bus == nullptr || !bus->isEnabled())
            continue;

        filters[stemIdx].prepareCoefficients(
            freqParams[stemIdx]->load(),
            qParams[stemIdx]->load(),
            gainParams[stemIdx]->load());

        auto inputBuf = getBusBuffer(buffer, true, busIdx);
        const float* inL = inputBuf.getReadPointer(0);
        const float* inR = inputBuf.getNumChannels() > 1
                               ? inputBuf.getReadPointer(1) : inL;

        filters[stemIdx].processBlock(inL, inR, outL, outR, numSamples);
    }
}

void StemSepProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, dest);
}

void StemSepProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

void StemSepProcessor::getMagnitudeResponses(
    const std::vector<float>& freqPoints,
    std::array<std::vector<float>, NUM_STEMS>& outMagnitudes) const
{
    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    for (int s = 0; s < NUM_STEMS; ++s)
    {
        outMagnitudes[s].resize(freqPoints.size());
        for (size_t i = 0; i < freqPoints.size(); ++i)
            outMagnitudes[s][i] = filters[s].getMagnitudeForFrequency(freqPoints[i], sr);
    }
}

juce::AudioProcessorEditor* StemSepProcessor::createEditor()
{
    return new StemSepEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StemSepProcessor();
}

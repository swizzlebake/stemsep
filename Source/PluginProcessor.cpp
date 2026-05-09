#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

static const char* kStemNames[NUM_STEMS] = {
    "Drums", "Bass", "Guitar", "Vocals", "Other"
};
static const char* kStemFiles[NUM_STEMS] = {
    "drums.wav", "bass.wav", "guitar.wav", "vocals.wav", "other.wav"
};

static const float kDefaultFreq[NUM_STEMS] = { 120.f, 250.f, 1500.f, 4000.f, 12000.f };
static const float kDefaultQ[NUM_STEMS]    = { 0.7f,  1.5f,  1.5f,   1.5f,   0.7f   };

StemSepProcessor::StemSepProcessor()
    : AudioProcessor(
        BusesProperties()
            .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
            .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout())
{
    formatManager_.registerBasicFormats();

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
        const auto id   = juce::String(i);
        const auto name = juce::String(kStemNames[i]);

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"freq" + id, 1},
            name + " Freq",
            juce::NormalisableRange<float>(20.f, 20000.f, 0.f, 0.25f),
            kDefaultFreq[i]));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"q" + id, 1},
            name + " Q",
            juce::NormalisableRange<float>(0.1f, 10.f, 0.f, 0.5f),
            kDefaultQ[i]));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"gain" + id, 1},
            name + " Level",
            juce::NormalisableRange<float>(-48.f, 24.f),
            0.f));

        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{"enable" + id, 1},
            name + " Enable",
            true));
    }

    return layout;
}

void StemSepProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    for (auto& f : filters)
        f.prepare(sampleRate, samplesPerBlock);
    inputCopy_.setSize(2, samplesPerBlock, false, true, false);
}

void StemSepProcessor::releaseResources()
{
    for (auto& f : filters)
        f.reset();
}

bool StemSepProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo();
}

void StemSepProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    auto outputBuf = getBusBuffer(buffer, false, 0);
    float* outL = outputBuf.getWritePointer(0);
    float* outR = outputBuf.getWritePointer(1);

    // Demucs playback mode
    if (mode_.load(std::memory_order_acquire) == Mode::Demucs
        && separatedAudioReady_.load(std::memory_order_acquire))
    {
        int64_t startSample = 0;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto t = pos->getTimeInSamples())
                    startSample = *t;

        outputBuf.clear();
        for (int i = 0; i < NUM_STEMS; ++i)
        {
            if (enableParams[i]->load() < 0.5f)
                continue;

            const float gainLin = std::pow(10.f, gainParams[i]->load() / 20.f);
            const auto& stem    = separatedStems_[i];
            const int   stemLen = stem.getNumSamples();

            for (int n = 0; n < numSamples; ++n)
            {
                const int64_t idx = startSample + n;
                if (idx < 0 || idx >= stemLen) continue;
                outL[n] += stem.getSample(0, (int)idx) * gainLin;
                outR[n] += stem.getSample(stem.getNumChannels() > 1 ? 1 : 0, (int)idx) * gainLin;
            }
        }
        return;
    }

    // BPF mode
    // Copy input before clearing output — they share the same channels in in-place mode.
    auto inputBuf = getBusBuffer(buffer, true, 0);
    inputCopy_.setSize(2, numSamples, false, false, true);
    inputCopy_.copyFrom(0, 0, inputBuf, 0, 0, numSamples);
    if (inputBuf.getNumChannels() > 1)
        inputCopy_.copyFrom(1, 0, inputBuf, 1, 0, numSamples);
    else
        inputCopy_.copyFrom(1, 0, inputBuf, 0, 0, numSamples);
    const float* inL = inputCopy_.getReadPointer(0);
    const float* inR = inputCopy_.getReadPointer(1);

    outputBuf.clear();

    for (int i = 0; i < NUM_STEMS; ++i)
    {
        if (enableParams[i]->load() < 0.5f)
            continue;

        filters[i].prepareCoefficients(
            freqParams[i]->load(),
            qParams[i]->load(),
            gainParams[i]->load());

        filters[i].processBlock(inL, inR, outL, outR, numSamples);
    }
}

void StemSepProcessor::loadSeparatedStems(const juce::File& folder)
{
    separatedAudioReady_.store(false, std::memory_order_release);
    separatedSourcePath_ = folder.getFullPathName();

    const double hostSR = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;

    for (int i = 0; i < NUM_STEMS; ++i)
    {
        const auto wavFile = folder.getChildFile(kStemFiles[i]);
        std::unique_ptr<juce::AudioFormatReader> reader(
            formatManager_.createReaderFor(wavFile));

        if (reader == nullptr)
        {
            separatedStems_[i].setSize(2, 0);
            continue;
        }

        const auto numSamples = (int)reader->lengthInSamples;
        separatedStems_[i].setSize(2, numSamples, false, true, false);
        reader->read(&separatedStems_[i], 0, numSamples, 0, true, true);

        // Resample once if host SR differs from file SR
        if (std::abs(reader->sampleRate - hostSR) > 0.5)
        {
            const double speedRatio = reader->sampleRate / hostSR;
            const int newLen = juce::roundToInt(numSamples / speedRatio);
            juce::AudioBuffer<float> resampled(2, newLen);
            resampled.clear();

            for (int ch = 0; ch < 2; ++ch)
            {
                juce::LagrangeInterpolator interp;
                interp.process(speedRatio,
                               separatedStems_[i].getReadPointer(ch),
                               resampled.getWritePointer(ch),
                               newLen);
            }
            separatedStems_[i] = std::move(resampled);
        }
    }

    separatedAudioReady_.store(true, std::memory_order_release);
}

void StemSepProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty("mode",       (int)mode_.load(), nullptr);
    state.setProperty("sourceFile", separatedSourcePath_, nullptr);
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, dest);
}

void StemSepProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml == nullptr || !xml->hasTagName(apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml(*xml);
    apvts.replaceState(state);

    mode_.store((Mode)(int)state.getProperty("mode", 0), std::memory_order_release);
    separatedSourcePath_ = state.getProperty("sourceFile", "").toString();

    if (separatedSourcePath_.isNotEmpty())
    {
        const juce::File folder(separatedSourcePath_);
        if (folder.isDirectory())
            loadSeparatedStems(folder);
    }
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

void StemSepProcessor::refreshCoefficients()
{
    for (int i = 0; i < NUM_STEMS; ++i)
        filters[i].prepareCoefficients(
            freqParams[i]->load(),
            qParams[i]->load(),
            gainParams[i]->load());
}

juce::AudioProcessorEditor* StemSepProcessor::createEditor()
{
    return new StemSepEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StemSepProcessor();
}

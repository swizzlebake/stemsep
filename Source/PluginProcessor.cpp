#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "UI/Formatters.h"
#include "DSP/Tunings.h"
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
            .withOutput("Main",   juce::AudioChannelSet::stereo(), true)
            .withOutput("Drums",  juce::AudioChannelSet::stereo(), true)
            .withOutput("Bass",   juce::AudioChannelSet::stereo(), true)
            .withOutput("Guitar", juce::AudioChannelSet::stereo(), true)
            .withOutput("Vocals", juce::AudioChannelSet::stereo(), true)
            .withOutput("Other",  juce::AudioChannelSet::stereo(), true)),
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

    using FloatAttrs = juce::AudioParameterFloatAttributes;

    const auto freqAttrs = FloatAttrs()
        .withStringFromValueFunction([](float v, int) { return stemsep::formatFreq(v); })
        .withValueFromStringFunction([](const juce::String& s) { return (float)stemsep::parseFreq(s); });

    const auto qAttrs = FloatAttrs()
        .withStringFromValueFunction([](float v, int) { return stemsep::formatQ(v); })
        .withValueFromStringFunction([](const juce::String& s) { return s.getFloatValue(); });

    const auto gainAttrs = FloatAttrs()
        .withStringFromValueFunction([](float v, int) { return stemsep::formatGain(v); })
        .withValueFromStringFunction([](const juce::String& s) { return (float)stemsep::parseGain(s); });

    for (int i = 0; i < NUM_STEMS; ++i)
    {
        const auto id   = juce::String(i);
        const auto name = juce::String(kStemNames[i]);

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"freq" + id, 1},
            name + " Freq",
            juce::NormalisableRange<float>(20.f, 20000.f, 0.f, 0.25f),
            kDefaultFreq[i],
            freqAttrs));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"q" + id, 1},
            name + " Q",
            juce::NormalisableRange<float>(0.1f, 10.f, 0.f, 0.5f),
            kDefaultQ[i],
            qAttrs));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"gain" + id, 1},
            name + " Level",
            juce::NormalisableRange<float>(-48.f, 24.f),
            0.f,
            gainAttrs));

        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{"enable" + id, 1},
            name + " Enable",
            true));
    }

    // Per-stem instrument tunings (bass = stem 1, guitar = stem 2).
    auto buildChoices = [](const std::vector<stemsep::TuningPreset>& presets) {
        juce::StringArray choices;
        for (const auto& p : presets) choices.add(p.label);
        choices.add("Custom\xe2\x80\xa6");
        return choices;
    };

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"tuning1", 1}, "Bass Tuning",
        buildChoices(stemsep::bassPresets()), 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"tuning2", 1}, "Guitar Tuning",
        buildChoices(stemsep::guitarPresets()), 0));

    return layout;
}

void StemSepProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    for (auto& f : filters)
        f.prepare(sampleRate, samplesPerBlock);
    inputCopy_.setSize(2, samplesPerBlock, false, true, false);
    stemScratch_.setSize(2, samplesPerBlock, false, true, false);
}

void StemSepProcessor::releaseResources()
{
    for (auto& f : filters)
        f.reset();
}

bool StemSepProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet()  != juce::AudioChannelSet::stereo()) return false;
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;

    // Stem output buses (1..NUM_STEMS) must be stereo when active.
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& cs = layouts.outputBuses.getReference(i);
        if (!cs.isDisabled() && cs != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void StemSepProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    auto mainOut = getBusBuffer(buffer, false, 0);
    float* mainL = mainOut.getNumChannels() > 0 ? mainOut.getWritePointer(0) : nullptr;
    float* mainR = mainOut.getNumChannels() > 1 ? mainOut.getWritePointer(1) : nullptr;

    // Stem bus pointers (nullptr when host has not activated that bus)
    float* stemL[NUM_STEMS] = {};
    float* stemR[NUM_STEMS] = {};
    for (int i = 0; i < NUM_STEMS; ++i)
    {
        const int busIdx = i + 1;
        if (busIdx < getBusCount(false))
        {
            auto bus = getBusBuffer(buffer, false, busIdx);
            if (bus.getNumChannels() >= 2)
            {
                stemL[i] = bus.getWritePointer(0);
                stemR[i] = bus.getWritePointer(1);
            }
        }
    }

    const Mode currentMode  = mode_.load(std::memory_order_acquire);
    const bool demucsReady  = separatedAudioReady_.load(std::memory_order_acquire);
    const bool bpfMode      = !(currentMode == Mode::Demucs && demucsReady);

    // BPF mode reads from the input bus, which may share memory with the main output
    // bus in in-place mode — copy *before* clearing the output.
    const float* inL = nullptr;
    const float* inR = nullptr;
    if (bpfMode)
    {
        auto inputBuf = getBusBuffer(buffer, true, 0);
        inputCopy_.setSize(2, numSamples, false, false, true);
        inputCopy_.copyFrom(0, 0, inputBuf, 0, 0, numSamples);
        if (inputBuf.getNumChannels() > 1)
            inputCopy_.copyFrom(1, 0, inputBuf, 1, 0, numSamples);
        else
            inputCopy_.copyFrom(1, 0, inputBuf, 0, 0, numSamples);
        inL = inputCopy_.getReadPointer(0);
        inR = inputCopy_.getReadPointer(1);
    }

    // Clear all output buses we may write into.
    mainOut.clear();
    for (int i = 0; i < NUM_STEMS; ++i)
        if (stemL[i] != nullptr) { std::fill_n(stemL[i], numSamples, 0.f); std::fill_n(stemR[i], numSamples, 0.f); }

    // Demucs playback mode
    if (!bpfMode)
    {
        int64_t startSample = 0;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto t = pos->getTimeInSamples())
                    startSample = *t;

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
                const float l = stem.getSample(0, (int)idx) * gainLin;
                const float r = stem.getSample(stem.getNumChannels() > 1 ? 1 : 0, (int)idx) * gainLin;
                if (mainL) mainL[n] += l;
                if (mainR) mainR[n] += r;
                if (stemL[i]) stemL[i][n] = l;
                if (stemR[i]) stemR[i][n] = r;
            }
        }
        return;
    }

    // BPF mode
    stemScratch_.setSize(2, numSamples, false, false, true);
    float* tmpL = stemScratch_.getWritePointer(0);
    float* tmpR = stemScratch_.getWritePointer(1);

    for (int i = 0; i < NUM_STEMS; ++i)
    {
        if (enableParams[i]->load() < 0.5f)
            continue;

        filters[i].prepareCoefficients(
            freqParams[i]->load(),
            qParams[i]->load(),
            gainParams[i]->load());

        std::fill_n(tmpL, numSamples, 0.f);
        std::fill_n(tmpR, numSamples, 0.f);
        filters[i].processBlock(inL, inR, tmpL, tmpR, numSamples);

        for (int n = 0; n < numSamples; ++n)
        {
            if (mainL) mainL[n] += tmpL[n];
            if (mainR) mainR[n] += tmpR[n];
        }

        if (stemL[i]) std::copy_n(tmpL, numSamples, stemL[i]);
        if (stemR[i]) std::copy_n(tmpR, numSamples, stemR[i]);
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

    if (onSeparatedStemsLoaded)
        juce::MessageManager::callAsync(onSeparatedStemsLoaded);
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

std::vector<int> StemSepProcessor::getTuningMidi(int stemIndex) const
{
    if (stemIndex != 1 && stemIndex != 2) return {};

    const auto& presets = (stemIndex == 1) ? stemsep::bassPresets()
                                            : stemsep::guitarPresets();
    const juce::String paramId = (stemIndex == 1) ? "tuning1" : "tuning2";
    const int total = (int)presets.size() + 1; // + Custom

    int idx = 0;
    if (auto* p = apvts.getParameter(paramId))
        idx = juce::jlimit(0, total - 1,
                           (int)std::round(p->convertFrom0to1(p->getValue())));

    if (idx < (int)presets.size())
        return presets[(size_t)idx].midi;

    const auto custom = getCustomTuningText(stemIndex);
    const int strings = (stemIndex == 1) ? 4 : 6;
    const int defaultOct = (stemIndex == 1) ? 1 : 2;
    auto parsed = stemsep::parseCustomTuning(custom, strings, defaultOct);
    if (! parsed.empty()) return parsed;

    return presets.front().midi; // fallback
}

juce::String StemSepProcessor::getCustomTuningText(int stemIndex) const
{
    const juce::Identifier id { stemIndex == 1 ? "bassCustomTuning"
                                                : "guitarCustomTuning" };
    return apvts.state.getProperty(id, juce::String()).toString();
}

void StemSepProcessor::setCustomTuningText(int stemIndex, const juce::String& text)
{
    const juce::Identifier id { stemIndex == 1 ? "bassCustomTuning"
                                                : "guitarCustomTuning" };
    apvts.state.setProperty(id, text, nullptr);
}

double StemSepProcessor::getHostBpm()
{
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                return *bpm;
    return 0.0;
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

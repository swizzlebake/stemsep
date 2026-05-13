#include "PluginEditor.h"

static const juce::Colour stemColors[NUM_STEMS] = {
    juce::Colour(0xff4fc3f7),  // sky blue  — Drums
    juce::Colour(0xffaed581),  // lime      — Bass
    juce::Colour(0xffffb74d),  // amber     — Guitar
    juce::Colour(0xffce93d8),  // lavender  — Vocals
    juce::Colour(0xffef9a9a),  // coral     — Other
};

static const char* stemNames[NUM_STEMS] = {
    "Drums", "Bass", "Guitar", "Vocals", "Other"
};

static const char* kStemFiles[NUM_STEMS] = {
    "drums.wav", "bass.wav", "guitar.wav", "vocals.wav", "other.wav"
};

StemSepEditor::StemSepEditor(StemSepProcessor& p)
    : AudioProcessorEditor(&p),
      processor_(p),
      freqDisplay_(p),
      separationPanel_(p)
{
    thumbFormatManager_.registerBasicFormats();

    for (int i = 0; i < NUM_STEMS; ++i)
    {
        freqDisplay_.setStemColor(i, stemColors[i]);
        stemStrips_[i] = std::make_unique<StemStrip>(i, stemNames[i], p.getAPVTS(), stemColors[i]);
        addAndMakeVisible(*stemStrips_[i]);

        thumbs_[i] = std::make_unique<juce::AudioThumbnail>(
            512, thumbFormatManager_, thumbCache_);
        stemStrips_[i]->setWaveformSource(thumbs_[i].get());

        stemStrips_[i]->onTabRequested = [this](int idx, TabRunner::Mode m) {
            runTabGeneration(idx, m);
        };
        if (i == 1 || i == 2)
        {
            const int stemIdx = i;
            stemStrips_[i]->onGetCustomTuning = [this, stemIdx]() {
                return processor_.getCustomTuningText(stemIdx);
            };
            stemStrips_[i]->onSetCustomTuning = [this, stemIdx](const juce::String& text) {
                processor_.setCustomTuningText(stemIdx, text);
            };
        }
    }
    addAndMakeVisible(freqDisplay_);
    addAndMakeVisible(separationPanel_);

    separationPanel_.onModeChanged = [this](StemSepProcessor::Mode m) {
        processor_.setMode(m);
    };
    separationPanel_.onSeparationComplete = [this](juce::File folder) {
        processor_.loadSeparatedStems(folder);
    };
    separationPanel_.onShowWaveformsChanged = [this](bool show) {
        for (auto& s : stemStrips_)
            s->setWaveformVisible(show);
    };

    processor_.onSeparatedStemsLoaded = [this] {
        rebuildThumbnails();
        updateTabButtonState();
    };

    if (processor_.hasSeparatedAudio())
        rebuildThumbnails();

    updateTabButtonState();

    setSize(editorWidth, editorHeight);
    setResizable(false, false);
}

StemSepEditor::~StemSepEditor()
{
    tabRunner_.cancel();
    tabRunner_.stopThread(5000);
    processor_.onSeparatedStemsLoaded = nullptr;
    for (auto& s : stemStrips_)
        s->setWaveformSource(nullptr);
    if (auto* w = tabWindow_.getComponent())
        delete w;
}

void StemSepEditor::rebuildThumbnails()
{
    const auto folder = processor_.getSeparatedSourceFolder();
    for (int i = 0; i < NUM_STEMS; ++i)
    {
        const auto wav = folder.getChildFile(kStemFiles[i]);
        thumbs_[i]->setSource(wav.existsAsFile()
            ? new juce::FileInputSource(wav)
            : nullptr);
    }
}

void StemSepEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a2e));
}

void StemSepEditor::resized()
{
    auto area = getLocalBounds();
    freqDisplay_    .setBounds(area.removeFromTop(displayHeight));
    separationPanel_.setBounds(area.removeFromTop(panelHeight));
    for (int i = 0; i < NUM_STEMS; ++i)
        stemStrips_[i]->setBounds(area.removeFromTop(stripHeight));
}

void StemSepEditor::updateTabButtonState()
{
    const bool stemsReady = processor_.hasSeparatedAudio() && ! tabRunning_;
    for (int i = 0; i < NUM_STEMS; ++i)
        stemStrips_[i]->setTabActionEnabled(stemsReady);
}

void StemSepEditor::runTabGeneration(int stemIndex, TabRunner::Mode mode)
{
    if (tabRunning_) return;
    if (stemIndex != 1 && stemIndex != 2) return;
    if (! processor_.hasSeparatedAudio()) return;

    const auto folder  = processor_.getSeparatedSourceFolder();
    const auto stemWav = folder.getChildFile(kStemFiles[stemIndex]);
    if (! stemWav.existsAsFile())
    {
        separationPanel_.setStatusText("Stem file not found: " + stemWav.getFileName());
        return;
    }

    const juce::String instrument = (stemIndex == 1) ? "bass" : "guitar";
    const auto outputTxt = folder.getChildFile(instrument + "_tab.txt");
    const double bpm = processor_.getHostBpm();
    auto tuning = processor_.getTuningMidi(stemIndex);
    if (tuning.empty())
    {
        separationPanel_.setStatusText("Tab error: tuning is empty");
        return;
    }

    tabRunning_ = true;
    updateTabButtonState();
    separationPanel_.setStatusText("Generating " + instrument + " tab\xe2\x80\xa6");

    juce::Component::SafePointer<StemSepEditor> safeThis(this);

    tabRunner_.start(
        stemWav, outputTxt, instrument, mode, bpm, std::move(tuning),
        [safeThis, instrument](float p) {
            if (! safeThis) return;
            const int pct = juce::jlimit(0, 100, (int)std::round(p * 100.f));
            safeThis->separationPanel_.setStatusText(
                "Generating " + instrument + " tab\xe2\x80\xa6 " + juce::String(pct) + "%");
        },
        [safeThis, instrument](TabRunner::Result result) {
            if (! safeThis) return;
            safeThis->tabRunning_ = false;
            safeThis->updateTabButtonState();

            if (result.success)
            {
                safeThis->separationPanel_.setStatusText(
                    "Tab saved to " + result.outputFile.getFileName());
                safeThis->openTabWindow(result.outputFile);
            }
            else
            {
                safeThis->separationPanel_.setStatusText(
                    "Tab error: " + result.errorMessage.substring(0, 140));
            }
        });
}

namespace
{
    static juce::String pickMonospaceFamily()
    {
        static const char* candidates[] = {
            "DejaVu Sans Mono",
            "Liberation Mono",
            "Ubuntu Mono",
            "Monaco",
            "Menlo",
            "Consolas",
            "Courier New"
        };
        const auto names = juce::Font::findAllTypefaceNames();
        for (auto* c : candidates)
            if (names.contains(c)) return c;
        return juce::Font::getDefaultMonospacedFontName();
    }

    class TabViewerComponent : public juce::Component
    {
    public:
        explicit TabViewerComponent(const juce::String& tabText)
        {
            editor_.setMultiLine(true, false);
            editor_.setReadOnly(true);
            editor_.setScrollbarsShown(true);
            editor_.setCaretVisible(false);
            editor_.setReturnKeyStartsNewLine(true);
            editor_.setFont(juce::Font(juce::FontOptions(pickMonospaceFamily(), 13.f, juce::Font::plain)));
            editor_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0d0d1a));
            editor_.setColour(juce::TextEditor::textColourId, juce::Colours::white.withAlpha(0.9f));
            editor_.setText(tabText, false);
            addAndMakeVisible(editor_);
            setSize(720, 520);
        }

        void resized() override { editor_.setBounds(getLocalBounds().reduced(8)); }

    private:
        juce::TextEditor editor_;
    };
}

void StemSepEditor::openTabWindow(const juce::File& tabFile)
{
    if (auto* w = tabWindow_.getComponent())
        delete w;

    const auto text = tabFile.loadFileAsString();

    juce::DialogWindow::LaunchOptions opts;
    opts.dialogTitle             = tabFile.getFileName();
    opts.content.setOwned(new TabViewerComponent(text));
    opts.componentToCentreAround = this;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar       = false;
    opts.resizable               = true;
    opts.dialogBackgroundColour  = juce::Colour(0xff1a1a2e);

    tabWindow_ = opts.launchAsync();
}

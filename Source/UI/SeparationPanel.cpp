#include "SeparationPanel.h"

namespace
{
    constexpr const char* kExpectedStemFiles[] = {
        "drums.wav", "bass.wav", "guitar.wav", "vocals.wav", "other.wav"
    };

    bool folderHasAllStems(const juce::File& dir)
    {
        for (auto* name : kExpectedStemFiles)
            if (! dir.getChildFile(name).existsAsFile())
                return false;
        return true;
    }
}

SeparationPanel::SeparationPanel(StemSepProcessor& processor)
    : processor_(processor)
{
    bpfButton_.onClick = [this] {
        if (onModeChanged) onModeChanged(StemSepProcessor::Mode::BPF);
        updateModeButtons();
    };
    demucsButton_.onClick = [this] {
        if (onModeChanged) onModeChanged(StemSepProcessor::Mode::Demucs);
        updateModeButtons();
    };
    addAndMakeVisible(bpfButton_);
    addAndMakeVisible(demucsButton_);

    filePathLabel_.setText("No file selected", juce::dontSendNotification);
    filePathLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.55f));
    filePathLabel_.setMinimumHorizontalScale(0.6f);
    addAndMakeVisible(filePathLabel_);

    browseButton_.onClick = [this] { browseForFile(); };
    addAndMakeVisible(browseButton_);

    captureButton_.onClick = [this] { toggleCapture(); };
    captureButton_.setTooltip("Record system audio output (play a Spotify/YouTube/etc track locally), then separate the captured WAV.");
    addAndMakeVisible(captureButton_);

    separateButton_.onClick = [this] { startSeparation(); };
    separateButton_.setEnabled(false);
    addAndMakeVisible(separateButton_);

    loadStemsButton_.onClick = [this] { loadStemsFromFolder(); };
    loadStemsButton_.setTooltip("Load previously-separated stems from a folder containing drums.wav, bass.wav, etc.");
    addAndMakeVisible(loadStemsButton_);

    saveCopyToggle_.setColour(juce::ToggleButton::textColourId, juce::Colours::white.withAlpha(0.7f));
    saveCopyToggle_.setTooltip("After separation, copy WAV stems into <source>_stems/ next to the input file so they survive a DAW restart.");
    saveCopyToggle_.setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(saveCopyToggle_);

    showWaveformsToggle_.setColour(juce::ToggleButton::textColourId, juce::Colours::white.withAlpha(0.7f));
    showWaveformsToggle_.setTooltip("Display the separated stem waveform inside each strip (Demucs mode).");
    showWaveformsToggle_.onClick = [this] {
        if (onShowWaveformsChanged) onShowWaveformsChanged(showWaveformsToggle_.getToggleState());
    };
    addAndMakeVisible(showWaveformsToggle_);

    progressBar_.setVisible(false);
    addAndMakeVisible(progressBar_);

    statusLabel_.setText("Ready", juce::dontSendNotification);
    statusLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
    addAndMakeVisible(statusLabel_);

    updateModeButtons();
}

SeparationPanel::~SeparationPanel()
{
    stopTimer();
    runner_.cancel();
    runner_.stopThread(5000);
    captureRunner_.stop();
    captureRunner_.stopThread(5000);
}

void SeparationPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff12122a));
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.fillRect(0, 0, getWidth(), 1);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
}

void SeparationPanel::resized()
{
    auto area = getLocalBounds().reduced(8, 0);
    const int half = area.getHeight() / 2;
    auto top = area.removeFromTop(half);
    auto bot = area;

    bpfButton_   .setBounds(top.removeFromLeft(86));
    top.removeFromLeft(4);
    demucsButton_.setBounds(top.removeFromLeft(100));
    top.removeFromLeft(8);
    statusLabel_ .setBounds(top);

    filePathLabel_ .setBounds(bot.removeFromLeft(200));
    bot.removeFromLeft(4);
    browseButton_  .setBounds(bot.removeFromLeft(72));
    bot.removeFromLeft(4);
    captureButton_ .setBounds(bot.removeFromLeft(108));
    bot.removeFromLeft(4);
    separateButton_.setBounds(bot.removeFromLeft(80));
    bot.removeFromLeft(4);
    loadStemsButton_.setBounds(bot.removeFromLeft(96));
    bot.removeFromLeft(8);
    saveCopyToggle_.setBounds(bot.removeFromLeft(160));
    bot.removeFromLeft(8);
    showWaveformsToggle_.setBounds(bot.removeFromLeft(132));
    bot.removeFromLeft(8);
    progressBar_   .setBounds(bot);
}

void SeparationPanel::updateModeButtons()
{
    const bool isDemucs = processor_.getMode() == StemSepProcessor::Mode::Demucs;
    bpfButton_   .setColour(juce::TextButton::buttonColourId,
                            isDemucs ? juce::Colour(0xff2a2a4a) : juce::Colour(0xff4a4a8a));
    demucsButton_.setColour(juce::TextButton::buttonColourId,
                            isDemucs ? juce::Colour(0xff4a4a8a) : juce::Colour(0xff2a2a4a));
}

void SeparationPanel::browseForFile()
{
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Select audio file to separate",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        "*.wav;*.mp3;*.aiff;*.flac;*.ogg");

    fileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc) {
            const auto result = fc.getResult();
            if (result.existsAsFile())
            {
                selectedFile_ = result;
                filePathLabel_.setText(result.getFileName(), juce::dontSendNotification);
                separateButton_.setEnabled(true);
            }
        });
}

void SeparationPanel::startSeparation()
{
    if (!selectedFile_.existsAsFile() || runner_.isThreadRunning())
        return;

    separateButton_.setEnabled(false);
    browseButton_  .setEnabled(false);
    progressValue_ = 0.0;
    progressBar_   .setVisible(true);
    statusLabel_   .setText("Separating\xe2\x80\xa6", juce::dontSendNotification);

    const bool saveCopyRequested = saveCopyToggle_.getToggleState();
    const juce::File saveDir = saveCopyRequested
        ? selectedFile_.getParentDirectory()
              .getChildFile(selectedFile_.getFileNameWithoutExtension() + "_stems")
        : juce::File();

    juce::Component::SafePointer<SeparationPanel> safeThis(this);

    runner_.start(
        selectedFile_,
        [safeThis](float p) {
            if (safeThis) safeThis->progressValue_ = p;
        },
        [safeThis, saveCopyRequested, saveDir](DemucsRunner::Result result) {
            if (!safeThis) return;
            safeThis->progressBar_   .setVisible(false);
            safeThis->browseButton_  .setEnabled(true);
            safeThis->separateButton_.setEnabled(true);

            if (! result.success)
            {
                safeThis->statusLabel_.setText(
                    "Error: " + result.errorMessage.substring(0, 120),
                    juce::dontSendNotification);
                return;
            }

            juce::File loadFolder = result.outputFolder;
            juce::String statusMessage = "Done";

            if (saveCopyRequested)
            {
                const auto createResult = saveDir.createDirectory();
                if (createResult.wasOk())
                {
                    juce::Array<juce::File> wavs;
                    result.outputFolder.findChildFiles(wavs, juce::File::findFiles, false, "*.wav");
                    for (const auto& f : wavs)
                        f.copyFileTo(saveDir.getChildFile(f.getFileName()));

                    if (folderHasAllStems(saveDir))
                    {
                        loadFolder = saveDir;
                        statusMessage = "Done \xe2\x80\x94 saved to " + saveDir.getFileName();
                    }
                    else
                    {
                        statusMessage = "Done \xe2\x80\x94 save copy incomplete, using temp folder";
                    }
                }
                else
                {
                    statusMessage = "Done \xe2\x80\x94 could not create save folder, using temp";
                }
            }

            safeThis->statusLabel_.setText(statusMessage, juce::dontSendNotification);

            if (safeThis->onSeparationComplete)
                safeThis->onSeparationComplete(loadFolder);
            if (safeThis->onModeChanged)
                safeThis->onModeChanged(StemSepProcessor::Mode::Demucs);
            safeThis->updateModeButtons();
        });
}

namespace
{
    juce::String formatElapsed(juce::int64 seconds)
    {
        const auto m = seconds / 60;
        const auto s = seconds % 60;
        return juce::String::formatted("%lld:%02lld",
                                       (long long)m, (long long)s);
    }
}

void SeparationPanel::toggleCapture()
{
    if (capturing_)
    {
        captureButton_.setEnabled(false);
        statusLabel_.setText("Finalising capture\xe2\x80\xa6", juce::dontSendNotification);
        captureRunner_.stop();
        return;
    }

    const auto captureDir = juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                                .getChildFile("StemSep_captures");
    const auto stamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d-%H%M%S");
    const auto outFile = captureDir.getChildFile("capture-" + stamp + ".wav");

    capturing_ = true;
    browseButton_  .setEnabled(false);
    separateButton_.setEnabled(false);
    loadStemsButton_.setEnabled(false);
    captureButton_ .setButtonText("Stop (0:00)");
    statusLabel_   .setText("\xe2\x97\x8f Recording \xe2\x80\x94 play your track now", juce::dontSendNotification);
    startTimer(1000);

    juce::Component::SafePointer<SeparationPanel> safeThis(this);

    captureRunner_.start(outFile,
        [safeThis](CaptureRunner::Result result) {
            if (! safeThis) return;
            safeThis->stopTimer();
            safeThis->capturing_ = false;
            safeThis->captureButton_.setEnabled(true);
            safeThis->captureButton_.setButtonText("Capture");
            safeThis->browseButton_  .setEnabled(true);
            safeThis->loadStemsButton_.setEnabled(true);

            if (! result.success)
            {
                safeThis->separateButton_.setEnabled(safeThis->selectedFile_.existsAsFile());
                safeThis->statusLabel_.setText(
                    "Capture failed: " + result.errorMessage.substring(0, 140),
                    juce::dontSendNotification);
                return;
            }

            safeThis->selectedFile_ = result.outputFile;
            safeThis->filePathLabel_.setText(result.outputFile.getFileName(),
                                             juce::dontSendNotification);
            safeThis->separateButton_.setEnabled(true);
            safeThis->statusLabel_.setText(
                "Captured \xe2\x80\x94 click Separate to continue",
                juce::dontSendNotification);
        });
}

void SeparationPanel::timerCallback()
{
    if (! capturing_)
    {
        stopTimer();
        return;
    }
    const auto elapsed = captureRunner_.elapsedSeconds();
    captureButton_.setButtonText("Stop (" + formatElapsed(elapsed) + ")");
}

void SeparationPanel::loadStemsFromFolder()
{
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Select a folder containing separated stems",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory));

    juce::Component::SafePointer<SeparationPanel> safeThis(this);
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safeThis](const juce::FileChooser& fc) {
            if (! safeThis) return;
            const auto folder = fc.getResult();
            if (! folder.isDirectory())
                return;

            if (! folder.getChildFile("drums.wav").existsAsFile())
            {
                safeThis->statusLabel_.setText("No stems found in folder",
                                                 juce::dontSendNotification);
                return;
            }

            if (safeThis->onSeparationComplete)
                safeThis->onSeparationComplete(folder);
            if (safeThis->onModeChanged)
                safeThis->onModeChanged(StemSepProcessor::Mode::Demucs);
            safeThis->updateModeButtons();
            safeThis->statusLabel_.setText("Loaded stems from " + folder.getFileName(),
                                             juce::dontSendNotification);
        });
}

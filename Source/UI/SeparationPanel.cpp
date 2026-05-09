#include "SeparationPanel.h"

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

    separateButton_.onClick = [this] { startSeparation(); };
    separateButton_.setEnabled(false);
    addAndMakeVisible(separateButton_);

    progressBar_.setVisible(false);
    addAndMakeVisible(progressBar_);

    statusLabel_.setText("Ready", juce::dontSendNotification);
    statusLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
    addAndMakeVisible(statusLabel_);

    updateModeButtons();
}

SeparationPanel::~SeparationPanel()
{
    runner_.cancel();
    runner_.stopThread(5000);
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

    filePathLabel_ .setBounds(bot.removeFromLeft(280));
    bot.removeFromLeft(4);
    browseButton_  .setBounds(bot.removeFromLeft(72));
    bot.removeFromLeft(4);
    separateButton_.setBounds(bot.removeFromLeft(80));
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

    juce::Component::SafePointer<SeparationPanel> safeThis(this);

    runner_.start(
        selectedFile_,
        [safeThis](float p) {
            if (safeThis) safeThis->progressValue_ = p;
        },
        [safeThis](DemucsRunner::Result result) {
            if (!safeThis) return;
            safeThis->progressBar_   .setVisible(false);
            safeThis->browseButton_  .setEnabled(true);
            safeThis->separateButton_.setEnabled(true);

            if (result.success)
            {
                safeThis->statusLabel_.setText("Done", juce::dontSendNotification);
                if (safeThis->onSeparationComplete)
                    safeThis->onSeparationComplete(result.outputFolder);
                if (safeThis->onModeChanged)
                    safeThis->onModeChanged(StemSepProcessor::Mode::Demucs);
                safeThis->updateModeButtons();
            }
            else
            {
                safeThis->statusLabel_.setText(
                    "Error: " + result.errorMessage.substring(0, 50),
                    juce::dontSendNotification);
            }
        });
}

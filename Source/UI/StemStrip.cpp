#include "StemStrip.h"
#include "Formatters.h"
#include "DSP/Tunings.h"

class StemStrip::WaveformView : public juce::Component
{
public:
    explicit WaveformView(juce::Colour c) : color_(c) {}

    void setThumbnail(juce::AudioThumbnail* t)
    {
        if (thumb_ == t) return;
        if (thumb_) thumb_->removeChangeListener(&listener_);
        thumb_ = t;
        if (thumb_) thumb_->addChangeListener(&listener_);
        repaint();
    }

    ~WaveformView() override
    {
        if (thumb_) thumb_->removeChangeListener(&listener_);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0d0d1a));

        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawRect(getLocalBounds(), 1);

        if (thumb_ == nullptr || thumb_->getTotalLength() <= 0.0)
        {
            g.setColour(juce::Colours::white.withAlpha(0.3f));
            g.setFont(11.f);
            g.drawText("(no stem loaded)", getLocalBounds(), juce::Justification::centred);
            return;
        }

        g.setColour(color_.withAlpha(0.85f));
        thumb_->drawChannels(g, getLocalBounds().reduced(2),
                              0.0, thumb_->getTotalLength(), 0.9f);
    }

private:
    struct Listener : public juce::ChangeListener
    {
        explicit Listener(WaveformView* o) : owner(o) {}
        WaveformView* owner;
        void changeListenerCallback(juce::ChangeBroadcaster*) override
        {
            if (owner) owner->repaint();
        }
    };

    juce::Colour color_;
    juce::AudioThumbnail* thumb_ = nullptr;
    Listener listener_ { this };
};

StemStrip::StemStrip(int stemIndex, const juce::String& name,
                     juce::AudioProcessorValueTreeState& apvts,
                     juce::Colour color)
    : stemIndex_(stemIndex), stemColor_(color)
{
    const auto id = juce::String(stemIndex);

    stemLabel_.setText(name, juce::dontSendNotification);
    stemLabel_.setColour(juce::Label::textColourId, color);
    stemLabel_.setFont(juce::Font(juce::FontOptions().withHeight(14.f).withStyle("Bold")));
    stemLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(stemLabel_);

    auto configKnob = [&](juce::Slider& s, const juce::String& tooltip) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 76, 16);
        s.setColour(juce::Slider::rotarySliderFillColourId, color);
        s.setColour(juce::Slider::rotarySliderOutlineColourId,
                    juce::Colours::white.withAlpha(0.15f));
        s.setTooltip(tooltip);
        addAndMakeVisible(s);
    };

    configKnob(freqKnob_, "Center frequency (Hz)");
    freqKnob_.setNumDecimalPlacesToDisplay(2);
    freqKnob_.textFromValueFunction = [](double v) { return stemsep::formatFreq(v); };
    freqKnob_.valueFromTextFunction = [](const juce::String& t) { return stemsep::parseFreq(t); };
    freqAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "freq" + id, freqKnob_);
    freqKnob_.updateText();

    configKnob(qKnob_, "Bandwidth (Q)");
    qKnob_.setNumDecimalPlacesToDisplay(2);
    qKnob_.textFromValueFunction = [](double v) { return stemsep::formatQ(v); };
    qKnob_.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue(); };
    qAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "q" + id, qKnob_);
    qKnob_.updateText();

    gainSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    gainSlider_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 64, 20);
    gainSlider_.setColour(juce::Slider::trackColourId, color.withAlpha(0.5f));
    gainSlider_.setColour(juce::Slider::thumbColourId, color);
    gainSlider_.setTooltip("Level (dB)");
    gainSlider_.setNumDecimalPlacesToDisplay(2);
    gainSlider_.textFromValueFunction = [](double v) { return stemsep::formatGain(v); };
    gainSlider_.valueFromTextFunction = [](const juce::String& t) { return stemsep::parseGain(t); };
    addAndMakeVisible(gainSlider_);
    gainAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "gain" + id, gainSlider_);
    gainSlider_.updateText();

    enableButton_.setButtonText("On");
    enableButton_.setColour(juce::ToggleButton::tickColourId, color);
    enableButton_.setTooltip("Enable / bypass this stem");
    addAndMakeVisible(enableButton_);
    enableAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, "enable" + id, enableButton_);

    // Tab generation only meaningful for Bass (1) and Guitar (2).
    supportsTab_ = (stemIndex == 1 || stemIndex == 2);
    if (supportsTab_)
    {
        stemLabel_.setTooltip("Right-click for tab generation");
        stemLabel_.addMouseListener(this, false);

        const auto& presets = (stemIndex == 1) ? stemsep::bassPresets()
                                                : stemsep::guitarPresets();
        for (size_t i = 0; i < presets.size(); ++i)
            tuningCombo_.addItem(presets[i].label, (int)i + 1);
        tuningCustomIndex_ = (int)presets.size();
        tuningCombo_.addItem("Custom\xe2\x80\xa6", tuningCustomIndex_ + 1);

        tuningCombo_.setTooltip("Tuning used when generating a tab from this stem");
        tuningCombo_.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0d0d1a));
        tuningCombo_.setColour(juce::ComboBox::textColourId, color);
        tuningCombo_.setColour(juce::ComboBox::outlineColourId, juce::Colours::white.withAlpha(0.15f));
        addAndMakeVisible(tuningCombo_);

        tuningAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, "tuning" + juce::String(stemIndex), tuningCombo_);

        lastValidTuningIndex_ = juce::jmax(0, tuningCombo_.getSelectedItemIndex());
        tuningCombo_.onChange = [this] { handleTuningChanged(); };
    }

    waveform_ = std::make_unique<WaveformView>(color);
    addChildComponent(*waveform_);
}

void StemStrip::handleTuningChanged()
{
    if (suppressTuningCallback_) return;

    const int idx = tuningCombo_.getSelectedItemIndex();
    if (idx == tuningCustomIndex_)
        promptForCustomTuning(lastValidTuningIndex_);
    else if (idx >= 0)
        lastValidTuningIndex_ = idx;
}

void StemStrip::promptForCustomTuning(int previousIndex)
{
    const int strings = (stemIndex_ == 1) ? 4 : 6;
    const int defaultOct = (stemIndex_ == 1) ? 1 : 2;
    const juce::String existing = onGetCustomTuning ? onGetCustomTuning() : juce::String();
    const juce::String example = (stemIndex_ == 1) ? "e.g. D A D G  (or D1 A1 D2 G2)"
                                                    : "e.g. D A D G B E  (or D2 A2 D3 G3 B3 E4)";

    auto* aw = new juce::AlertWindow("Custom tuning",
                                     "Enter " + juce::String(strings) +
                                     " note names, low to high.\n" + example,
                                     juce::AlertWindow::NoIcon, this);
    aw->addTextEditor("tuning", existing, "Tuning");
    aw->addButton("OK",     1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<StemStrip> safeThis(this);
    aw->enterModalState(true,
        juce::ModalCallbackFunction::create([safeThis, aw, previousIndex, strings, defaultOct](int result) {
            std::unique_ptr<juce::AlertWindow> owned(aw);
            if (! safeThis) return;

            if (result == 0)
            {
                safeThis->suppressTuningCallback_ = true;
                safeThis->tuningCombo_.setSelectedItemIndex(previousIndex, juce::sendNotificationSync);
                safeThis->suppressTuningCallback_ = false;
                return;
            }

            const auto text = aw->getTextEditorContents("tuning").trim();
            const auto parsed = stemsep::parseCustomTuning(text, strings, defaultOct);
            if (parsed.empty())
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Invalid tuning",
                    "Could not parse \"" + text + "\". Expected " + juce::String(strings) +
                    " note names like 'D A D G B E'.");
                safeThis->suppressTuningCallback_ = true;
                safeThis->tuningCombo_.setSelectedItemIndex(previousIndex, juce::sendNotificationSync);
                safeThis->suppressTuningCallback_ = false;
                return;
            }

            if (safeThis->onSetCustomTuning) safeThis->onSetCustomTuning(text);
            safeThis->lastValidTuningIndex_ = safeThis->tuningCustomIndex_;
        }));
}

void StemStrip::setTabActionEnabled(bool shouldBeEnabled)
{
    tabEnabled_ = shouldBeEnabled;
}

void StemStrip::mouseDown(const juce::MouseEvent& e)
{
    if (! supportsTab_ || ! tabEnabled_) return;
    if (! e.mods.isPopupMenu()) return;
    showTabMenu(e.eventComponent != nullptr ? e.eventComponent : &stemLabel_);
}

void StemStrip::showTabMenu(juce::Component* targetComponent)
{
    juce::PopupMenu menu;
    menu.addItem(1, "Generate monophonic tab (fast, single notes)");
    menu.addItem(2, "Generate polyphonic tab (slower, captures chords)");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(targetComponent),
        [safeThis = juce::Component::SafePointer<StemStrip>(this)](int choice) {
            if (! safeThis || choice == 0 || ! safeThis->onTabRequested) return;
            const auto mode = (choice == 1) ? TabRunner::Mode::Mono : TabRunner::Mode::Poly;
            safeThis->onTabRequested(safeThis->stemIndex_, mode);
        });
}

StemStrip::~StemStrip() = default;

void StemStrip::setWaveformVisible(bool shouldBeVisible)
{
    if (waveformVisible_ == shouldBeVisible) return;
    waveformVisible_ = shouldBeVisible;
    waveform_->setVisible(shouldBeVisible);
    resized();
}

void StemStrip::setWaveformSource(juce::AudioThumbnail* thumb)
{
    waveform_->setThumbnail(thumb);
}

void StemStrip::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff16213e));

    g.setColour(stemColor_);
    g.fillRect(0, 0, 5, getHeight());

    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.drawHorizontalLine(getHeight() - 1, 0.f, (float)getWidth());
}

void StemStrip::resized()
{
    auto area = getLocalBounds().reduced(8).withTrimmedLeft(6);

    if (supportsTab_)
    {
        auto leftCol = area.removeFromLeft(140);
        auto labelRow  = leftCol.removeFromTop(leftCol.getHeight() / 2);
        stemLabel_   .setBounds(labelRow.withSizeKeepingCentre(140, 22));
        tuningCombo_ .setBounds(leftCol.reduced(4, 2).withSizeKeepingCentre(132, 22));
    }
    else
    {
        stemLabel_.setBounds(area.removeFromLeft(70).withSizeKeepingCentre(70, 32));
    }
    enableButton_.setBounds(area.removeFromRight(56).withSizeKeepingCentre(56, 24));

    area.removeFromLeft(4);
    freqKnob_.setBounds(area.removeFromLeft(84));
    qKnob_   .setBounds(area.removeFromLeft(84));
    area.removeFromLeft(8);

    if (waveformVisible_)
    {
        area.removeFromRight(8);
        waveform_->setBounds(area.removeFromRight(400).reduced(0, 6));
    }

    gainSlider_.setBounds(area);
}

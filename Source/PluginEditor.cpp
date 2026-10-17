#include "PluginEditor.h"

namespace
{
    const juce::Colour backgroundColour (0xff15171c);

    void styleLabel (juce::Label& label, const juce::String& text, juce::Justification justification)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (justification);
        label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
    }
}

void FourRealAudioEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& text, bool isDb)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 20);
    if (isDb)
        k.slider.setTextValueSuffix (" dB");
    k.slider.setDoubleClickReturnValue (true, isDb ? 0.0 : 10.0);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    styleLabel (k.label, text, juce::Justification::centred);

    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<Apvts::SliderAttachment> (audioProcessor.apvts, paramId, k.slider);
}

void FourRealAudioEditor::setupChoice (Choice& c, const juce::String& paramId, const juce::String& text)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (audioProcessor.apvts.getParameter (paramId)))
        c.box.addItemList (choice->choices, 1);
    styleLabel (c.label, text, juce::Justification::centredLeft);

    addAndMakeVisible (c.box);
    addAndMakeVisible (c.label);
    c.attachment = std::make_unique<Apvts::ComboBoxAttachment> (audioProcessor.apvts, paramId, c.box);
}

void FourRealAudioEditor::setupToggle (Toggle& t, const juce::String& paramId, const juce::String& text)
{
    t.button.setButtonText (text);
    addAndMakeVisible (t.button);
    t.attachment = std::make_unique<Apvts::ButtonAttachment> (audioProcessor.apvts, paramId, t.button);
}

FourRealAudioEditor::FourRealAudioEditor (FourRealAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    const auto& target = FourRealAudioProcessor::targetModel();
    accent = juce::Colour (target.accentRgb);

    setupKnob (inputKnob, "inputGain", "INPUT", true);
    for (size_t i = 0; i < target.volumes.size(); ++i)
    {
        modelKnobs.push_back (std::make_unique<Knob>());
        setupKnob (*modelKnobs.back(), "vol" + juce::String ((int) i), target.volumes[i].label, false);
    }
    for (size_t i = 0; i < target.tones.size(); ++i)
    {
        modelKnobs.push_back (std::make_unique<Knob>());
        setupKnob (*modelKnobs.back(), "tone" + juce::String ((int) i), target.tones[i].label, false);
    }
    setupKnob (intensityKnob, "intensity", "INTENSITY", false);
    intensityKnob.slider.setTextValueSuffix (" %");
    intensityKnob.slider.setDoubleClickReturnValue (true, 100.0);
    setupKnob (outputKnob, "outputGain", "OUTPUT", true);

    styleLabel (sourceHeader, "MY GUITAR (SOURCE) - pick the closest recorded guitar", juce::Justification::centredLeft);
    styleLabel (targetHeader, juce::String ("SOUND LIKE: ") + juce::String (target.displayName).toUpperCase(),
                juce::Justification::centredLeft);
    addAndMakeVisible (sourceHeader);
    addAndMakeVisible (targetHeader);

    setupChoice (sourceGuitar, "srcGuitar", "");
    setupChoice (sourcePosition, "srcPos", "");
    setupChoice (targetSelector, "tgtSel", "");
    setupToggle (voicingToggle, "voicing", "Voicing on");
    setupToggle (keepLevelToggle, "keepLevel", "Volume knob changes level");
    setupToggle (autoLevelToggle, "autoLevel", "Auto level (recommended)");
    refreshSourcePositions();
    targetSelector.box.setEnabled (target.positions.size() > 1); // only one recorded position: nothing to choose

    setSize (880, 470);
    startTimerHz (30);
}

FourRealAudioEditor::~FourRealAudioEditor()
{
    stopTimer();
}

// The selector positions depend on which source guitar is chosen: only the positions that were recorded.
void FourRealAudioEditor::refreshSourcePositions()
{
    const auto& models = fourreal::allModels();
    const int guitar = juce::jlimit (0, (int) models.size() - 1,
                                     static_cast<int> (audioProcessor.apvts.getRawParameterValue ("srcGuitar")->load() + 0.5f));
    const auto& positions = models[static_cast<size_t> (guitar)].positions;
    const int count = (int) positions.size();

    auto* param = dynamic_cast<juce::AudioParameterChoice*> (audioProcessor.apvts.getParameter ("srcPos"));
    int index = param != nullptr ? param->getIndex() : 0;
    if (param != nullptr && index >= count)
    {
        *param = count - 1; // clamp the stored position to what this guitar has
        index = count - 1;
    }

    sourcePosition.box.clear (juce::dontSendNotification);
    for (int i = 0; i < count; ++i)
        sourcePosition.box.addItem (positions[(size_t) i].name, i + 1);
    sourcePosition.box.setSelectedId (index + 1, juce::dontSendNotification);
    shownSourceGuitar = guitar;
}

void FourRealAudioEditor::timerCallback()
{
    const int guitar = static_cast<int> (audioProcessor.apvts.getRawParameterValue ("srcGuitar")->load() + 0.5f);
    if (guitar != shownSourceGuitar)
        refreshSourcePositions();

    // Quick attack, slow release.
    inputLevel  = juce::jmax (audioProcessor.inputPeak.load(),  inputLevel  * 0.85f);
    outputLevel = juce::jmax (audioProcessor.outputPeak.load(), outputLevel * 0.85f);
    repaint (meterArea);
}

void FourRealAudioEditor::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour);

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (28.0f, juce::Font::bold)));
    const juce::String title ("4REAL AUDIO");
    g.drawText (title, 24, 14, getWidth() - 48, 32, juce::Justification::centredLeft);
    g.setColour (accent);
    g.drawText (juce::String (FourRealAudioProcessor::targetModel().code), 24 + 205, 14, 120, 32,
                juce::Justification::centredLeft);

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText ("guitar tone transformer  -  v0.6.0 test build (spectral model)",
                24, 44, getWidth() - 48, 18, juce::Justification::centredLeft);

    auto drawMeter = [&g, this] (juce::Rectangle<int> r, float level, const juce::String& name)
    {
        g.setColour (juce::Colour (0xff2a2e38));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);

        const float norm = juce::jlimit (0.0f, 1.0f,
            juce::jmap (juce::Decibels::gainToDecibels (level, -60.0f), -60.0f, 0.0f, 0.0f, 1.0f));

        g.setColour (norm > 0.95f ? juce::Colours::red : accent);
        g.fillRoundedRectangle (r.withWidth (juce::roundToInt ((float) r.getWidth() * norm)).toFloat(), 3.0f);

        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (name, r.reduced (6, 0), juce::Justification::centredLeft);
    };

    // The model was trained on 48 kHz audio. At other rates the frequencies stay right, the analysis timing is only approximate.
    const double sr = audioProcessor.hostSampleRate.load();
    if (std::abs (sr - 48000.0) > 1.0 || ! audioProcessor.modelLoaded())
    {
        g.setColour (juce::Colour (0xffffb347));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (audioProcessor.modelLoaded()
                        ? juce::String ("Sample rate is ") + juce::String (sr / 1000.0, 1)
                              + " kHz: the model was trained at 48 kHz, set the project to 48 kHz for best accuracy"
                        : juce::String ("Model failed to load"),
                    24, getHeight() - 74, getWidth() - 48, 16, juce::Justification::centredLeft);
    }

    auto m = meterArea;
    drawMeter (m.removeFromTop (18), inputLevel, "IN");
    m.removeFromTop (4);
    drawMeter (m.removeFromTop (18), outputLevel, "OUT");
}

void FourRealAudioEditor::resized()
{
    auto area = getLocalBounds().reduced (24);
    area.removeFromTop (60); // header

    meterArea = area.removeFromBottom (40);
    area.removeFromBottom (10);

    // Row of choices: source (guitar + position), target selector, toggles.
    auto top = area.removeFromTop (84);
    auto sourceArea = top.removeFromLeft (420);
    top.removeFromLeft (20);
    auto targetArea = top.removeFromLeft (210);
    top.removeFromLeft (20);
    auto toggleArea = top;

    sourceHeader.setBounds (sourceArea.removeFromTop (22));
    sourceGuitar.box.setBounds (sourceArea.removeFromLeft (210).removeFromTop (28));
    sourceArea.removeFromLeft (10);
    sourcePosition.box.setBounds (sourceArea.removeFromTop (28));

    targetHeader.setBounds (targetArea.removeFromTop (22));
    targetSelector.box.setBounds (targetArea.removeFromTop (28));

    voicingToggle.button.setBounds (toggleArea.removeFromTop (26));
    keepLevelToggle.button.setBounds (toggleArea.removeFromTop (26));
    autoLevelToggle.button.setBounds (toggleArea.removeFromTop (26));

    area.removeFromTop (14);

    // Knob row: INPUT, the target guitar's volume and tone knobs, OUTPUT. Centred.
    std::vector<Knob*> knobs { &inputKnob };
    for (auto& k : modelKnobs)
        knobs.push_back (k.get());
    knobs.push_back (&intensityKnob);
    knobs.push_back (&outputKnob);

    const int colWidth = juce::jmin (130, area.getWidth() / juce::jmax (1, (int) knobs.size()));
    area.removeFromLeft ((area.getWidth() - colWidth * (int) knobs.size()) / 2);
    for (Knob* k : knobs)
    {
        auto col = area.removeFromLeft (colWidth);
        k->label.setBounds (col.removeFromTop (20));
        k->slider.setBounds (col);
    }
}

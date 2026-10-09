#include "PluginEditor.h"

namespace
{
    const juce::Colour backgroundColour (0xff15171c);
    const juce::Colour accentColour (0xff4fd1c5);

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
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, accentColour);
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
    setupKnob (inputKnob, "inputGain", "INPUT", true);
    setupKnob (volNeckKnob, "volNeck", "VOL NECK", false);
    setupKnob (volBridgeKnob, "volBridge", "VOL BRIDGE", false);
    setupKnob (toneNeckKnob, "toneNeck", "TONE NECK", false);
    setupKnob (toneBridgeKnob, "toneBridge", "TONE BRIDGE", false);
    setupKnob (outputKnob, "outputGain", "OUTPUT", true);

    styleLabel (sourceHeader, "MY GUITAR (SOURCE)", juce::Justification::centredLeft);
    styleLabel (targetHeader, "SOUND LIKE (TARGET: LES PAUL)", juce::Justification::centredLeft);
    addAndMakeVisible (sourceHeader);
    addAndMakeVisible (targetHeader);

    setupChoice (sourceGuitar, "srcGuitar", "");
    setupChoice (sourcePosition, "srcPos", "");
    setupChoice (targetSelector, "tgtSel", "");
    setupToggle (voicingToggle, "voicing", "Voicing on");
    setupToggle (keepLevelToggle, "keepLevel", "Keep level difference");

    setSize (760, 470);
    startTimerHz (30);
}

FourRealAudioEditor::~FourRealAudioEditor()
{
    stopTimer();
}

void FourRealAudioEditor::timerCallback()
{
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
    g.drawText ("4REAL AUDIO", 24, 14, getWidth() - 48, 32, juce::Justification::centredLeft);

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText ("guitar tone transformer  -  v0.2 test build (physical model of pickups and knobs, no neural model yet)",
                24, 44, getWidth() - 48, 18, juce::Justification::centredLeft);

    auto drawMeter = [&g] (juce::Rectangle<int> r, float level, const juce::String& name)
    {
        g.setColour (juce::Colour (0xff2a2e38));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);

        const float norm = juce::jlimit (0.0f, 1.0f,
            juce::jmap (juce::Decibels::gainToDecibels (level, -60.0f), -60.0f, 0.0f, 0.0f, 1.0f));

        g.setColour (norm > 0.95f ? juce::Colours::red : accentColour);
        g.fillRoundedRectangle (r.withWidth (juce::roundToInt ((float) r.getWidth() * norm)).toFloat(), 3.0f);

        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (name, r.reduced (6, 0), juce::Justification::centredLeft);
    };

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
    auto top = area.removeFromTop (74);
    auto sourceArea = top.removeFromLeft (350);
    top.removeFromLeft (20);
    auto targetArea = top.removeFromLeft (190);
    top.removeFromLeft (20);
    auto toggleArea = top;

    sourceHeader.setBounds (sourceArea.removeFromTop (22));
    sourceGuitar.box.setBounds (sourceArea.removeFromLeft (165).removeFromTop (28));
    sourceArea.removeFromLeft (10);
    sourcePosition.box.setBounds (sourceArea.removeFromTop (28));

    targetHeader.setBounds (targetArea.removeFromTop (22).withWidth (260));
    targetSelector.box.setBounds (targetArea.removeFromTop (28));

    voicingToggle.button.setBounds (toggleArea.removeFromTop (26));
    keepLevelToggle.button.setBounds (toggleArea.removeFromTop (26));

    area.removeFromTop (14);

    // Six knobs in a row.
    const int colWidth = area.getWidth() / 6;
    for (Knob* k : { &inputKnob, &volNeckKnob, &volBridgeKnob, &toneNeckKnob, &toneBridgeKnob, &outputKnob })
    {
        auto col = area.removeFromLeft (colWidth);
        k->label.setBounds (col.removeFromTop (20));
        k->slider.setBounds (col);
    }
}

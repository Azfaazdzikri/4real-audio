#include "PluginEditor.h"

namespace
{
    const juce::Colour backgroundColour (0xff15171c);
    const juce::Colour accentColour (0xff4fd1c5);

    void setupKnob (juce::Slider& knob)
    {
        knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 20);
        knob.setTextValueSuffix (" dB");
        knob.setDoubleClickReturnValue (true, 0.0);
        knob.setColour (juce::Slider::rotarySliderFillColourId, accentColour);
    }

    void setupLabel (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
    }

    void fillChoices (juce::ComboBox& box, juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (paramId)))
            box.addItemList (choice->choices, 1);
    }
}

FourRealAudioEditor::FourRealAudioEditor (FourRealAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setupKnob (inputKnob);
    setupKnob (outputKnob);
    setupLabel (inputLabel, "INPUT");
    setupLabel (outputLabel, "OUTPUT");
    setupLabel (sourceLabel, "MY GUITAR (SOURCE)");
    setupLabel (targetLabel, "SOUND LIKE (TARGET)");

    fillChoices (sourceBox, audioProcessor.apvts, "source");
    fillChoices (targetBox, audioProcessor.apvts, "target");

    for (juce::Component* c : { static_cast<juce::Component*> (&inputKnob),
                                static_cast<juce::Component*> (&outputKnob),
                                static_cast<juce::Component*> (&inputLabel),
                                static_cast<juce::Component*> (&outputLabel),
                                static_cast<juce::Component*> (&sourceLabel),
                                static_cast<juce::Component*> (&targetLabel),
                                static_cast<juce::Component*> (&sourceBox),
                                static_cast<juce::Component*> (&targetBox) })
        addAndMakeVisible (c);

    inputAttachment  = std::make_unique<SliderAttachment> (audioProcessor.apvts, "inputGain",  inputKnob);
    outputAttachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, "outputGain", outputKnob);
    sourceAttachment = std::make_unique<ComboAttachment>  (audioProcessor.apvts, "source",     sourceBox);
    targetAttachment = std::make_unique<ComboAttachment>  (audioProcessor.apvts, "target",     targetBox);

    setSize (520, 340);
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
    g.drawText ("guitar tone transformer  -  v0.1 test build (clean passthrough, no neural model yet)",
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

    auto left  = area.removeFromLeft (130);
    auto right = area.removeFromRight (130);

    inputLabel.setBounds (left.removeFromTop (20));
    inputKnob.setBounds (left);

    outputLabel.setBounds (right.removeFromTop (20));
    outputKnob.setBounds (right);

    auto mid = area.reduced (12, 10);
    sourceLabel.setBounds (mid.removeFromTop (20));
    sourceBox.setBounds (mid.removeFromTop (28));
    mid.removeFromTop (14);
    targetLabel.setBounds (mid.removeFromTop (20));
    targetBox.setBounds (mid.removeFromTop (28));
}

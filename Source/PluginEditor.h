#pragma once

#include <array>

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

class FourRealAudioEditor : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit FourRealAudioEditor (FourRealAudioProcessor&);
    ~FourRealAudioEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using Apvts = juce::AudioProcessorValueTreeState;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<Apvts::SliderAttachment> attachment; // declared last: destroyed first
    };

    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<Apvts::ComboBoxAttachment> attachment;
    };

    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<Apvts::ButtonAttachment> attachment;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& text, bool isDb);
    void setupChoice (Choice&, const juce::String& paramId, const juce::String& text);
    void setupToggle (Toggle&, const juce::String& paramId, const juce::String& text);
    void timerCallback() override;

    FourRealAudioProcessor& audioProcessor;

    Knob inputKnob, volNeckKnob, volBridgeKnob, toneNeckKnob, toneBridgeKnob, outputKnob;
    Choice sourceGuitar, sourcePosition, targetSelector;
    Toggle voicingToggle, keepLevelToggle;
    juce::Label sourceHeader, targetHeader;

    juce::Rectangle<int> meterArea;
    float inputLevel = 0.0f;
    float outputLevel = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FourRealAudioEditor)
};

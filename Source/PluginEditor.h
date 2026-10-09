#pragma once

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
    void timerCallback() override;

    FourRealAudioProcessor& audioProcessor;

    juce::Slider inputKnob, outputKnob;
    juce::Label inputLabel, outputLabel, sourceLabel, targetLabel;
    juce::ComboBox sourceBox, targetBox;

    // Attachments must be declared after the components they control.
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SliderAttachment> inputAttachment, outputAttachment;
    std::unique_ptr<ComboAttachment> sourceAttachment, targetAttachment;

    juce::Rectangle<int> meterArea;
    float inputLevel = 0.0f;
    float outputLevel = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FourRealAudioEditor)
};

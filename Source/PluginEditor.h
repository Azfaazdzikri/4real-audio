#pragma once

#include <array>
#include <memory>

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "ui/GuitarView.h"
#include "ui/LibraryBrowser.h"
#include "ui/Look.h"

// One window for the whole library: guitar list on the left, the photo of the chosen guitar with its working pickup selector,
// volume and tone knobs in the middle, and below it the source guitar ("My guitar" calibration), input/intensity/output.
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
        std::unique_ptr<Apvts::ComboBoxAttachment> attachment;
    };
    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<Apvts::ButtonAttachment> attachment;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& text, bool isDb);
    void setupToggle (Toggle&, const juce::String& paramId, const juce::String& text);
    void refreshSourcePositions();
    void setSlotPanelVisible (bool shouldShow);
    void timerCallback() override;

    FourRealAudioProcessor& audioProcessor;
    fourreal::ui::Look look;
    fourreal::ui::LibraryBrowser browser;
    fourreal::ui::GuitarView view;
    int shownGuitar = -1;
    juce::Colour accent;

    Knob inputKnob, outputKnob, intensityKnob;
    int shownSourceGuitar = -1;
    Choice sourceGuitar, sourcePosition;
    Toggle voicingToggle, keepLevelToggle, autoLevelToggle;
    juce::Label sourceHeader, autoLabel;

    struct SlotColumn
    {
        juce::Label title, status;
        juce::TextButton learn, clear;
        juce::ComboBox cell;
        std::unique_ptr<Apvts::ComboBoxAttachment> attachment;
    };
    static constexpr int kSlotPanelHeight = 140;
    static constexpr int kBaseHeight = 830;
    std::array<SlotColumn, FourRealAudioProcessor::kNumSlots> slotCols;
    std::array<juce::Rectangle<int>, FourRealAudioProcessor::kNumSlots> slotBars;
    juce::Label slotHelp;
    bool slotsShown = false;

    juce::Rectangle<int> meterArea;
    float inputLevel = 0.0f, outputLevel = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FourRealAudioEditor)
};

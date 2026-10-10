#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/CrossfadeFir.h"
#include "dsp/FilterDesign.h"
#include "dsp/GuitarCircuit.h"
#include "dsp/GuitarModels.h"

// 4real-audio - guitar tone transformer, v0.3 test build.
//
// One code base, one plugin per TARGET guitar (LP, 335, SG, PRS, RIC, STR, TCR, FLC): the
// variant is chosen at compile time (FOURREAL_MODEL_<CODE>, set in CMakeLists.txt).
// The audio is filtered by the difference between two physical models of guitar
// electronics: the SOURCE guitar (chosen by the player) and the TARGET guitar of this
// variant, with its own selector, volume and tone knobs. This is NOT the neural-network
// stage yet: it captures the fixed linear part of pickup/knob behaviour only.
class FourRealAudioProcessor : public juce::AudioProcessor,
                               private juce::AudioProcessorValueTreeState::Listener,
                               private juce::AsyncUpdater
{
public:
    FourRealAudioProcessor();
    ~FourRealAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;

    // The guitar this plugin variant sounds like.
    static const fourreal::GuitarModel& targetModel();
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;

    // Latest block peaks (linear gain), read by the editor for the meters.
    std::atomic<float> inputPeak { 0.0f };
    std::atomic<float> outputPeak { 0.0f };

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    std::vector<float> designTaps() const;

    juce::StringArray voicingParamIds;

    juce::SmoothedValue<float> inputGain { 1.0f };
    juce::SmoothedValue<float> outputGain { 1.0f };

    fourreal::CrossfadeFir voicing;
    std::vector<float> pendingTaps;
    juce::SpinLock tapsLock;
    std::atomic<bool> tapsReady { false };
    double currentSampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FourRealAudioProcessor)
};

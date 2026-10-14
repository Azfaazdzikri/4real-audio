#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/CrossfadeFir.h"
#include "dsp/FilterDesign.h"
#include "dsp/GuitarCircuit.h"
#include "dsp/GuitarModels.h"
#include "dsp/NgmEngine.h"
#include "dsp/NgmModel.h"

// 4real-audio - guitar tone transformer, v0.5 (neural network).
//
// One code base, one plugin per TARGET guitar (LP, 335, SG, PRS, RIC, STR, TCR, FLC): the
// variant is chosen at compile time (FOURREAL_MODEL_<CODE>, set in CMakeLists.txt).
// Signal path:  input gain -> auto level -> per-pair EQ (min-phase FIR) -> conditional TCN
//               (weights from the embedded .ngm file) -> undo auto level -> volume/tone knob
//               filter (physical model of the target guitar's pots) -> output gain.
// The network was trained on recordings made with the knobs full, so the volume and tone knobs
// of the target guitar are applied by the physical circuit model on top of the neural stage.
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
    std::atomic<double> hostSampleRate { 48000.0 };
    bool neuralModelLoaded() const { return ngmLoaded; }

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    struct Taps { std::vector<float> eq, knobs; };
    Taps designTaps() const;
    int sourceIndexFor (int guitar, int position) const;
    void processSlice (float* const* data, int numChannels, int n);

    juce::StringArray voicingParamIds;

    juce::SmoothedValue<float> inputGain { 1.0f };
    juce::SmoothedValue<float> outputGain { 1.0f };

    static constexpr int kSlice = fourreal::NgmStream::kChunk;

    fourreal::NgmModel ngm;
    bool ngmLoaded = false;
    fourreal::NgmStream stream[2];
    fourreal::CrossfadeFir eqFir, knobFir;
    Taps pendingTaps;
    juce::SpinLock tapsLock;
    std::atomic<bool> tapsReady { false };
    int activeTarget = -1, activeSource = -1;
    bool bypassed = false;

    // auto level: a slow estimate of the input level, so the network sees roughly the level it was trained at
    float levelPower = -1.0f, autoGain = 1.0f;
    juce::SmoothedValue<float> wetMix { 1.0f };
    std::vector<float> work[2], dry[2];
    double currentSampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FourRealAudioProcessor)
};

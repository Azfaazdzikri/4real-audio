#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/CrossfadeFir.h"
#include "dsp/FilterDesign.h"
#include "dsp/GuitarCircuit.h"
#include "dsp/GuitarModels.h"
#include "dsp/SpecEngine.h"
#include "dsp/SpecModel.h"

// 4real-audio - guitar tone transformer, v0.6.0 (spectral model).
//
// One code base, one plugin per TARGET guitar (LP, 335, SG, PRS, RIC, STR, TCR, FLC): the
// variant is chosen at compile time (FOURREAL_MODEL_<CODE>, set in CMakeLists.txt).
// Signal path:  input gain -> spectral engine (per-pair EQ + small network that follows the played
//               sound, applied as a min-phase FIR, so it can never add distortion or noise; the model
//               comes from the embedded .spm file) -> volume/tone knob filter (physical model of the
//               target guitar's pots) -> output gain.
// The model was trained on recordings made with the knobs full, so the volume and tone knobs of the
// target guitar are applied by the physical circuit model on top of it.
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
    bool modelLoaded() const { return specLoaded; }

    // Automatic source: the engine listens to the spectrum of the guitar and mixes the source cells itself. The choice "Auto" is
    // the last entry of the source guitar list and only exists when the model carries real prototypes.
    static bool autoSourceAvailable();
    static constexpr int kMaxSources = 32;
    std::array<std::atomic<float>, kMaxSources> autoWeightsUi {}; // current weight of every source cell (for the editor)
    std::atomic<bool> autoActiveUi { false };
    int numSourceCells() const { return specLoaded ? juce::jmin (kMaxSources, spec.numSources) : 0; }
    juce::String sourceLabel (int index) const; // e.g. "Les Paul bridge"

    // "My guitar": the player teaches the plugin the sound of every pickup position of the own guitar (a slot, 5 at most) and says
    // which recorded source cell each one stands for ("slotCell0".."slotCell4"). The plugin then follows the pickup switch by itself.
    static constexpr int kNumSlots = fourreal::SpecStream::kMaxSlots;
    static bool myGuitarAvailable();
    static const std::vector<juce::String>& sourceCellLabels(); // one per source cell, in .spm order
    void requestLearn (int slot);  // start collecting the sound of a slot (UI thread)
    void requestStopLearn();       // end the collection and keep it if it was long enough
    void clearSlot (int slot);
    std::atomic<int> learningSlotUi { -1 };
    std::atomic<float> learnSecondsUi { 0.0f };
    std::array<std::atomic<float>, kNumSlots> slotWeightUi {};
    std::array<std::atomic<bool>, kNumSlots> slotValidUi {};

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    std::vector<float> designKnobTaps() const;
    int sourceIndexFor (int guitar, int position) const;
    void processSlice (float* const* data, int numChannels, int n);

    juce::StringArray voicingParamIds;

    juce::SmoothedValue<float> inputGain { 1.0f };
    juce::SmoothedValue<float> outputGain { 1.0f };

    static constexpr int kSlice = 512;

    fourreal::SpecModel spec;
    bool specLoaded = false;
    fourreal::SpecStream stream[2];
    fourreal::CrossfadeFir knobFir;
    std::vector<float> pendingKnobTaps;
    juce::SpinLock tapsLock;
    std::atomic<bool> tapsReady { false };
    int activeTarget = -1, activeSource = -1;
    bool activeAuto = false, activeSlots = false;

    // calibrated slots: slotsShared is the copy the UI thread and the saved state use, slotsAudio the one the audio thread uses
    struct SlotData
    {
        bool valid[kNumSlots] = {};
        float feat[kNumSlots][64] = {};
    };
    SlotData slotsShared, slotsAudio;
    juce::SpinLock slotsLock;
    std::atomic<bool> slotsToAudio { false };
    bool publishPending = false;
    int appliedCell[kNumSlots] = { -1, -1, -1, -1, -1 };
    int learningSlot = -1;
    std::atomic<int> learnRequest { 0 }; // 0 = nothing, k + 1 = start slot k, -1 = stop
    void handleLearning();
    void applySlots();
    bool bypassed = false;

    // auto level: a slow estimate of the input level, so the network analyses roughly the level it was trained at
    // (only the analysis is scaled; the filter always acts on the real signal)
    float levelPower = -1.0f, autoGain = 1.0f;
    juce::SmoothedValue<float> wetMix { 1.0f };
    std::vector<float> work[2], dry[2];
    bool engineIdle = true;
    double currentSampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FourRealAudioProcessor)
};

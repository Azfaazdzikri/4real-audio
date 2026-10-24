#pragma once

#include <array>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Library.h"
#include "License.h"
#include "dsp/CrossfadeFir.h"
#include "dsp/FilterDesign.h"
#include "dsp/GuitarCircuit.h"
#include "dsp/GuitarModels.h"
#include "dsp/SpecEngine.h"
#include "dsp/SpecModel.h"

// 4real-audio - guitar tone transformer, v0.9.0 (one plugin, guitar library).
//
// ONE plugin / standalone holds the whole guitar library (LP, 335, SG, PRS, RIC, STR, TCR, FLC, see Library.h): the player
// picks which guitar the plugin sounds like, the way a piano app switches between its piano models. The controls of every
// guitar (pickup selector, volume and tone knobs) are separate parameters ("LP_vol0", "STR_tone1", ...), so each guitar
// remembers its own knob positions.
// Signal path:  input gain -> spectral engine of the CURRENT guitar (per-pair EQ + small network that follows the played
//               sound, applied as a min-phase FIR, so it can never add distortion or noise; the model comes from the embedded
//               .spm file of that guitar) -> volume/tone knob filter (physical model of that guitar's pots) -> output gain.
// The models were trained on recordings made with the knobs full, so the volume and tone knobs of the target guitar are
// applied by the physical circuit model on top of it.
// Switching guitar fades the sound out and in over two short slices (no click) and never allocates on the audio thread:
// the engines of all guitars are built in prepareToPlay().
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

    const juce::String getName() const override { return "4real-audio"; }
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

    // --- guitar library -----------------------------------------------------------------------------------------------
    static const fourreal::Library& library() { return fourreal::Library::get(); }
    static int numGuitars() { return library().size(); }
    // Parameter id of a control of a library guitar: kind is "sel", "vol" or "tone" ("sel" has no index).
    // Examples: guitarParamId (0, "sel") = "LP_sel", guitarParamId (5, "tone", 1) = "STR_tone1".
    static juce::String guitarParamId (int guitar, const char* kind, int index = 0);
    // The guitar the user chose (parameter "guitar"). The audio engine follows it with a short fade.
    int selectedGuitar() const;
    void selectGuitar (int index); // from the editor (message thread)
    bool isGuitarUnlocked (int index) const { return fourreal::LicenseManager::isUnlocked (library().at (index)); }

    // Latest block peaks (linear gain), read by the editor for the meters.
    std::atomic<float> inputPeak { 0.0f };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<double> hostSampleRate { 48000.0 };
    bool modelLoaded() const { return library().allLoaded(); }

    // Automatic source: the engine listens to the spectrum of the guitar and mixes the source cells itself. The choice "Auto" is
    // the last entry of the source guitar list and only exists when the models carry real prototypes.
    static bool autoSourceAvailable();
    static constexpr int kMaxSources = 32;
    std::array<std::atomic<float>, kMaxSources> autoWeightsUi {}; // current weight of every source cell (for the editor)
    std::atomic<bool> autoActiveUi { false };
    int numSourceCells() const { return juce::jmin (kMaxSources, library().reference().numSources); }
    juce::String sourceLabel (int index) const; // e.g. "Les Paul bridge"

    // "My guitar": the player teaches the plugin the sound of every pickup position of the own guitar (a slot, 5 at most) and says
    // which recorded source cell each one stands for ("slotCell0".."slotCell4"). The plugin then follows the pickup switch by itself.
    // The calibration belongs to the player's guitar, so it is shared by all library guitars.
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
    void switchEngineTo (int guitar);

    juce::StringArray voicingParamIds;
    std::atomic<float>* guitarRaw = nullptr;       // parameter "guitar"
    std::vector<std::atomic<float>*> selectorRaw;  // pickup selector parameter of every library guitar

    juce::SmoothedValue<float> inputGain { 1.0f };
    juce::SmoothedValue<float> outputGain { 1.0f };

    static constexpr int kSlice = 512;

    // One spectral engine (a stream per channel) for every library guitar, all built in prepareToPlay().
    struct Engine
    {
        fourreal::SpecStream stream[2];
        int activeTarget = -1, activeSource = -1;
        bool activeAuto = false, activeSlots = false;
    };
    std::vector<Engine> engines;
    int activeGuitar = 0; // audio thread: the guitar the engine sounds like right now
    int fadePhase = 0;    // 0 = none, 1 = fading out before a guitar switch, 2 = fading in after it
    const fourreal::SpecModel& activeSpec() const { return library().at (activeGuitar).spec; }
    fourreal::SpecStream* activeStreams() { return engines[static_cast<size_t> (activeGuitar)].stream; }

    fourreal::CrossfadeFir knobFir;
    std::vector<float> pendingKnobTaps;
    juce::SpinLock tapsLock;
    std::atomic<bool> tapsReady { false };

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

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cstring>

namespace
{
    float peakOf (const juce::AudioBuffer<float>& buffer)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));
        return peak;
    }

    void applySmoothedGain (juce::AudioBuffer<float>& buffer, juce::SmoothedValue<float>& gain)
    {
        const int numSamples  = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        if (! gain.isSmoothing())
        {
            buffer.applyGain (gain.getTargetValue());
            return;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const float g = gain.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.getWritePointer (ch)[i] *= g;
        }
    }

    juce::String labelFor (const fourreal::SpmSourceInfo& s)
    {
        const auto* g = fourreal::findModelByCode (s.code);
        if (g == nullptr)
            return s.cell;
        const int pos = juce::jlimit (0, (int) g->positions.size() - 1, s.position);
        return juce::String (g->displayName) + " " + juce::String (g->positions[static_cast<size_t> (pos)].name).toLowerCase();
    }
}

juce::String FourRealAudioProcessor::guitarParamId (int guitar, const char* kind, int index)
{
    juce::String id = juce::String (library().at (guitar).code()) + "_" + kind;
    if (std::strcmp (kind, "sel") != 0)
        id << index;
    return id;
}

FourRealAudioProcessor::FourRealAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    jassert (library().allLoaded());   // the embedded .spm files must always parse
    jassert (library().consistent());  // and agree on the source side

    engines.resize (static_cast<size_t> (numGuitars()));

    voicingParamIds = { "guitar", "voicing", "keepLevel" };
    for (int g = 0; g < numGuitars(); ++g)
    {
        const auto& m = *library().at (g).guitar;
        voicingParamIds.add (guitarParamId (g, "sel"));
        for (size_t i = 0; i < m.volumes.size(); ++i)
            voicingParamIds.add (guitarParamId (g, "vol", (int) i));
        for (size_t i = 0; i < m.tones.size(); ++i)
            voicingParamIds.add (guitarParamId (g, "tone", (int) i));
    }

    for (const auto& id : voicingParamIds)
        apvts.addParameterListener (id, this);

    // raw parameter pointers for the audio thread (looking a parameter up by name would build a String every slice)
    guitarRaw = apvts.getRawParameterValue ("guitar");
    for (int g = 0; g < numGuitars(); ++g)
        selectorRaw.push_back (apvts.getRawParameterValue (guitarParamId (g, "sel")));
}

FourRealAudioProcessor::~FourRealAudioProcessor()
{
    cancelPendingUpdate();
    for (const auto& id : voicingParamIds)
        apvts.removeParameterListener (id, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout FourRealAudioProcessor::createLayout()
{
    using namespace juce;

    std::vector<std::unique_ptr<RangedAudioParameter>> params;
    const auto& lib = library();

    // The guitar of the library the plugin sounds like. Entries are only ever appended, so saved projects keep their choice.
    StringArray guitarNames;
    for (const auto& e : lib.entries())
        guitarNames.add (e.displayName());
    params.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "guitar", 1 }, "Guitar", guitarNames, 0));

    auto gainParam = [&params] (const char* id, const char* name)
    {
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));
    };
    gainParam ("inputGain", "Input");
    gainParam ("outputGain", "Output");

    params.push_back (std::make_unique<AudioParameterBool> (ParameterID { "voicing", 1 }, "Voicing", true));
    params.push_back (std::make_unique<AudioParameterBool> (ParameterID { "keepLevel", 1 }, "Keep level difference", true));
    params.push_back (std::make_unique<AudioParameterBool> (ParameterID { "autoLevel", 1 }, "Auto level", true));
    // How strongly the learned tone change is applied: 0 = off, 100 = as trained, above 100 exaggerates it.
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "intensity", 1 }, "Intensity", NormalisableRange<float> (0.0f, 150.0f, 1.0f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")));

    StringArray sourceNames;
    for (const auto& m : fourreal::allModels())
        sourceNames.add (m.displayName);
    // extra entries come last, so saved projects keep their guitar choice
    if (autoSourceAvailable())
        sourceNames.add ("Auto (matches the sound to the nearest recorded guitar)");
    if (myGuitarAvailable())
        sourceNames.add ("My guitar (calibrated)");
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "srcGuitar", 1 }, "Source guitar", sourceNames, 0));
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "srcPos", 1 }, "Source pickup position",
        StringArray { "Position 1", "Position 2", "Position 3", "Position 4" }, 0)); // at most 4 recorded positions per guitar

    if (myGuitarAvailable())
    {
        StringArray cellNames;
        for (const auto& l : sourceCellLabels())
            cellNames.add (l);
        for (int k = 0; k < kNumSlots; ++k)
            params.push_back (std::make_unique<AudioParameterChoice> (
                ParameterID { "slotCell" + String (k), 1 }, "My pickup " + String (k + 1) + " stands for", cellNames, 0));
    }

    // The controls of every library guitar: its pickup selector, volume knobs and tone knobs. Each guitar keeps its own.
    for (int g = 0; g < lib.size(); ++g)
    {
        const auto& m = *lib.at (g).guitar;
        const String prefix = String (m.displayName) + " ";

        StringArray positions;
        for (const auto& p : m.positions)
            positions.add (p.name);
        params.push_back (std::make_unique<AudioParameterChoice> (
            ParameterID { guitarParamId (g, "sel"), 1 }, prefix + "pickup selector", positions, 0));

        auto knobParam = [&params, &prefix] (const String& id, const String& name)
        {
            params.push_back (std::make_unique<AudioParameterFloat> (
                ParameterID { id, 1 }, prefix + name, NormalisableRange<float> (0.0f, 10.0f, 0.1f), 10.0f));
        };
        for (size_t i = 0; i < m.volumes.size(); ++i)
            knobParam (guitarParamId (g, "vol", (int) i), m.volumes[i].label);
        for (size_t i = 0; i < m.tones.size(); ++i)
            knobParam (guitarParamId (g, "tone", (int) i), m.tones[i].label);
    }

    return { params.begin(), params.end() };
}

bool FourRealAudioProcessor::autoSourceAvailable()
{
    const auto& a = library().reference().autoSource;
    return a.present && a.enabled;
}

bool FourRealAudioProcessor::myGuitarAvailable() { return library().reference().autoSource.present; }

const std::vector<juce::String>& FourRealAudioProcessor::sourceCellLabels()
{
    static const std::vector<juce::String> labels = []
    {
        std::vector<juce::String> r;
        for (const auto& s : library().reference().sources)
            r.push_back (labelFor (s));
        return r;
    }();
    return labels;
}

juce::String FourRealAudioProcessor::sourceLabel (int index) const
{
    const auto& labels = sourceCellLabels();
    return index >= 0 && index < (int) labels.size() ? labels[static_cast<size_t> (index)] : juce::String();
}

int FourRealAudioProcessor::selectedGuitar() const
{
    return juce::jlimit (0, numGuitars() - 1, static_cast<int> (guitarRaw->load() + 0.5f));
}

void FourRealAudioProcessor::selectGuitar (int index)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("guitar")))
    {
        index = juce::jlimit (0, numGuitars() - 1, index);
        if (p->getIndex() == index)
            return;
        p->beginChangeGesture();
        *p = index;
        p->endChangeGesture();
    }
}

int FourRealAudioProcessor::sourceIndexFor (int guitar, int position) const
{
    const auto& models = fourreal::allModels();
    const auto& g = models[static_cast<size_t> (juce::jlimit (0, (int) models.size() - 1, guitar))];
    const int pos = juce::jlimit (0, (int) g.positions.size() - 1, position);
    const int idx = library().at (activeGuitar).loaded ? activeSpec().findSource (g.code, pos) : -1;
    jassert (idx >= 0);
    return idx >= 0 ? idx : 0;
}

// Message thread: the filter of the knobs of the guitar the user chose.
std::vector<float> FourRealAudioProcessor::designKnobTaps() const
{
    auto get = [this] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
    using namespace fourreal;

    const int g = selectedGuitar();
    const auto& targetDef = *library().at (g).guitar;
    const int tgtSel = static_cast<int> (get (guitarParamId (g, "sel")) + 0.5f);

    // volume / tone knobs of the target guitar: physical model, "knobs now" relative to "knobs full"
    // (the model was trained with the knobs full, so at full knobs this is the identity).
    const auto grid = makeFrequencyGrid (currentSampleRate);
    ModelSettings full, now;
    full.position = now.position = juce::jlimit (0, (int) targetDef.positions.size() - 1, tgtSel);
    for (size_t i = 0; i < targetDef.volumes.size(); ++i)
    {
        full.volumes.push_back (10.0);
        now.volumes.push_back (get (guitarParamId (g, "vol", (int) i)));
    }
    for (size_t i = 0; i < targetDef.tones.size(); ++i)
    {
        full.tones.push_back (10.0);
        now.tones.push_back (get (guitarParamId (g, "tone", (int) i)));
    }
    return designVoicingTaps (modelResponse (grid, targetDef, full), modelResponse (grid, targetDef, now),
                              grid, currentSampleRate, get ("keepLevel") > 0.5f);
}

void FourRealAudioProcessor::parameterChanged (const juce::String&, float)
{
    triggerAsyncUpdate(); // coalesces bursts of knob movements; runs on the message thread
}

void FourRealAudioProcessor::handleAsyncUpdate()
{
    auto taps = designKnobTaps();
    {
        const juce::SpinLock::ScopedLockType lock (tapsLock);
        pendingKnobTaps = std::move (taps);
    }
    tapsReady.store (true);
}

void FourRealAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;
    hostSampleRate.store (sampleRate);

    inputGain.reset (sampleRate, 0.02);
    outputGain.reset (sampleRate, 0.02);
    inputGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("inputGain")->load()));
    outputGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("outputGain")->load()));

    const bool on = apvts.getRawParameterValue ("voicing")->load() > 0.5f;
    wetMix.reset (sampleRate, 0.03);
    wetMix.setCurrentAndTargetValue (on ? 1.0f : 0.0f);

    knobFir.prepare (2, fourreal::kVoicingTaps, kSlice);
    for (int c = 0; c < 2; ++c)
    {
        work[c].assign (static_cast<size_t> (kSlice), 0.0f);
        dry[c].assign (static_cast<size_t> (kSlice), 0.0f);
    }
    engineIdle = true;

    // Build the engines of ALL library guitars now, so that switching guitar later never allocates on the audio thread.
    for (int g = 0; g < numGuitars(); ++g)
    {
        auto& e = engines[static_cast<size_t> (g)];
        const auto& entry = library().at (g);
        for (auto& st : e.stream)
            if (entry.loaded)
            {
                st.prepare (entry.spec, sampleRate);
                st.reset();
            }
        e.activeTarget = e.activeSource = -1;
        e.activeAuto = e.activeSlots = false;
    }
    activeGuitar = selectedGuitar();
    fadePhase = 0;
    learningSlot = -1;
    learningSlotUi.store (-1);
    slotsToAudio.store (true); // the streams lose their slots when they are prepared again
    levelPower = -1.0f;
    autoGain = 1.0f;

    {
        const juce::SpinLock::ScopedLockType lock (tapsLock);
        pendingKnobTaps = designKnobTaps();
    }
    tapsReady.store (true);
}

bool FourRealAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return out == layouts.getMainInputChannelSet();
}

void FourRealAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    inputPeak.store (peakOf (buffer));

    if (tapsReady.load())
    {
        const juce::SpinLock::ScopedTryLockType lock (tapsLock);
        if (lock.isLocked())
        {
            knobFir.setTaps (pendingKnobTaps.data());
            tapsReady.store (false);
        }
    }

    inputGain.setTargetValue (
        juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("inputGain")->load()));
    outputGain.setTargetValue (
        juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("outputGain")->load()));
    wetMix.setTargetValue (apvts.getRawParameterValue ("voicing")->load() > 0.5f ? 1.0f : 0.0f);

    applySmoothedGain (buffer, inputGain);

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    float* const* data = buffer.getArrayOfWritePointers();
    for (int done = 0; done < buffer.getNumSamples(); done += kSlice)
    {
        float* ptrs[2] = { data[0] + done, numChannels > 1 ? data[1] + done : nullptr };
        processSlice (ptrs, numChannels, juce::jmin (kSlice, buffer.getNumSamples() - done));
    }

    applySmoothedGain (buffer, outputGain);

    outputPeak.store (peakOf (buffer));
}

// The engine now sounds like another library guitar: start its streams clean and make the slice code apply the pair again.
void FourRealAudioProcessor::switchEngineTo (int guitar)
{
    activeGuitar = juce::jlimit (0, numGuitars() - 1, guitar);
    auto& e = engines[static_cast<size_t> (activeGuitar)];
    if (library().at (activeGuitar).loaded)
        for (auto& st : e.stream)
            st.reset();
    e.activeTarget = e.activeSource = -1;
    e.activeAuto = e.activeSlots = false;
    for (auto& c : appliedCell)
        c = -1; // applySlots() sends the calibration to the new streams
    if (learningSlot >= 0)
    {
        learningSlot = -1;
        learningSlotUi.store (-1);
    }
    engineIdle = true;
}

// n <= kSlice samples, in place.
void FourRealAudioProcessor::processSlice (float* const* data, int numChannels, int n)
{
    // --- which library guitar is played: a change fades the sound out (this slice), switches, and fades in (next slice)
    {
        const int wanted = selectedGuitar();
        const bool silentNow = wetMix.getTargetValue() < 0.5f && ! wetMix.isSmoothing();
        if (wanted != activeGuitar && fadePhase == 0)
        {
            if (silentNow || engineIdle)
                switchEngineTo (wanted); // the engine is not audible right now, nothing to fade
            else
                fadePhase = 1;
        }
    }

    const auto& entry = library().at (activeGuitar);
    const auto& spec = entry.spec;
    const bool specLoaded = entry.loaded;
    auto& eng = engines[static_cast<size_t> (activeGuitar)];
    fourreal::SpecStream* const stream = eng.stream;

    // A guitar that is not unlocked passes the sound through untouched.
    if (! fourreal::LicenseManager::isUnlocked (entry))
    {
        fadePhase = 0;
        return;
    }

    // Which (target position, source cell) pair is selected right now.
    const int target = juce::jlimit (0, juce::jmax (0, spec.numTargets - 1),
                                     static_cast<int> (selectorRaw[static_cast<size_t> (activeGuitar)]->load() + 0.5f));
    const int guitarChoice = static_cast<int> (apvts.getRawParameterValue ("srcGuitar")->load() + 0.5f);
    const int numSourceGuitars = (int) fourreal::allModels().size();
    const bool autoSrc = specLoaded && autoSourceAvailable() && guitarChoice == numSourceGuitars;
    const bool myChoice = specLoaded && myGuitarAvailable() && guitarChoice == numSourceGuitars + (autoSourceAvailable() ? 1 : 0);
    if (specLoaded && myGuitarAvailable())
    {
        handleLearning();
        applySlots();
    }
    int validSlots = 0;
    for (int k = 0; k < kNumSlots; ++k)
        validSlots += slotsAudio.valid[k] ? 1 : 0;
    const bool slotsOn = myChoice && validSlots > 0; // "My guitar" without any calibrated slot behaves like the first manual guitar
    const int manualGuitar = guitarChoice < numSourceGuitars ? guitarChoice : 0;
    const int source = (autoSrc || slotsOn) ? 0 : sourceIndexFor (manualGuitar, manualGuitar == guitarChoice ? static_cast<int> (apvts.getRawParameterValue ("srcPos")->load() + 0.5f) : 0);
    if (specLoaded && (target != eng.activeTarget || source != eng.activeSource || autoSrc != eng.activeAuto || slotsOn != eng.activeSlots))
    {
        for (int c = 0; c < 2; ++c)
        {
            stream[c].setCondition (target, source);
            stream[c].setSlotMode (slotsOn);
            stream[c].setAutoSource (autoSrc);
        }
        eng.activeTarget = target;
        eng.activeSource = source;
        eng.activeAuto = autoSrc;
        eng.activeSlots = slotsOn;
        autoActiveUi.store (autoSrc);
    }

    // Voicing off and settled: nothing to compute (the streams are cleared when it is switched back on).
    if (wetMix.getTargetValue() < 0.5f && ! wetMix.isSmoothing())
    {
        bypassed = true;
        if (fadePhase != 0)
        {
            switchEngineTo (selectedGuitar());
            fadePhase = 0;
        }
        return;
    }
    if (bypassed)
    {
        bypassed = false;
        if (specLoaded)
            for (int c = 0; c < 2; ++c)
                stream[c].reset();
    }

    // A guitar is usually mono: when both channels are identical, run the engine once.
    const bool dualMono = numChannels == 2
        && std::memcmp (data[0], data[1], sizeof (float) * static_cast<size_t> (n)) == 0;
    const int active = dualMono ? 1 : numChannels;
    const bool wetOnly = wetMix.getTargetValue() > 0.5f && ! wetMix.isSmoothing();

    // keep a dry copy only while the Voicing switch is (or just was) off
    if (! wetOnly)
        for (int c = 0; c < active; ++c)
            std::copy (data[c], data[c] + n, dry[c].begin());

    // --- auto level: estimate the input level slowly (only while there is signal) so the network analyses roughly the
    //     level it was trained at. Only the analysis is scaled: the filter acts on the real signal, so dynamics stay intact.
    const float sr = static_cast<float> (currentSampleRate);
    const bool autoOn = apvts.getRawParameterValue ("autoLevel")->load() > 0.5f && specLoaded;
    if (autoOn)
    {
        float pw = 0.0f;
        for (int c = 0; c < active; ++c)
        {
            double sum = 0.0;
            for (int i = 0; i < n; ++i)
                sum += static_cast<double> (data[c][i]) * data[c][i];
            pw = juce::jmax (pw, static_cast<float> (sum / n));
        }
        if (pw > 1.0e-7f) // above about -70 dBFS
        {
            if (levelPower < 0.0f)
                levelPower = pw;
            else
                levelPower += (pw - levelPower) * (1.0f - std::exp (-static_cast<float> (n) / (1.0f * sr)));
        }
        float wanted = 1.0f;
        if (levelPower > 0.0f)
        {
            const float refPower = std::pow (10.0f, spec.referenceRmsDb / 10.0f);
            wanted = juce::jlimit (0.0631f, 63.1f, std::sqrt (refPower / levelPower)); // +-24 dB .. +36 dB
        }
        autoGain += (wanted - autoGain) * (1.0f - std::exp (-static_cast<float> (n) / (0.5f * sr)));
    }
    else
        autoGain = 1.0f;

    // Intensity of the learned tone change. Playing the target guitar's own selected pickup through itself changes nothing.
    float intensity = apvts.getRawParameterValue ("intensity")->load() * 0.01f;
    if (specLoaded && ! autoSrc && ! slotsOn && spec.sources[static_cast<size_t> (source)].cell == spec.targetCells[static_cast<size_t> (target)])
        intensity = 0.0f;

    float* wk[2] = { work[0].data(), work[1].data() };
    for (int c = 0; c < active; ++c)
        std::copy (data[c], data[c] + n, wk[c]);

    if (specLoaded && (intensity > 0.001f || learningSlot >= 0)) // the engine also runs while a slot is being learned
    {
        if (engineIdle)
        {
            for (int c = 0; c < 2; ++c)
                stream[c].reset(); // history is stale after being idle
            engineIdle = false;
        }
        for (int c = 0; c < active; ++c)
        {
            stream[c].setIntensity (intensity);
            stream[c].setAnalysisGain (autoGain);
            stream[c].process (wk[c], wk[c], n);
        }
        if (autoSrc || slotsOn)
        {
            const auto& w = stream[0].autoWeights();
            for (size_t i = 0; i < w.size() && i < autoWeightsUi.size(); ++i)
                autoWeightsUi[i].store (static_cast<float> (w[i]), std::memory_order_relaxed);
        }
        if (slotsOn)
            for (int k = 0; k < kNumSlots; ++k)
                slotWeightUi[(size_t) k].store (static_cast<float> (stream[0].slotWeights()[(size_t) k]), std::memory_order_relaxed);
        if (learningSlot >= 0)
            learnSecondsUi.store (static_cast<float> (stream[0].learnSeconds()), std::memory_order_relaxed);
    }
    else
        engineIdle = true;

    knobFir.process (wk, active, n);

    // guitar switch: ramp the wet sound down (phase 1), switch at the end of the slice, ramp it up again (phase 2)
    if (fadePhase != 0)
    {
        const float from = fadePhase == 1 ? 1.0f : 0.0f, to = 1.0f - from;
        for (int c = 0; c < active; ++c)
            for (int i = 0; i < n; ++i)
                wk[c][i] *= from + (to - from) * (static_cast<float> (i + 1) / static_cast<float> (n));
        if (fadePhase == 1)
        {
            switchEngineTo (selectedGuitar());
            fadePhase = 2;
        }
        else
            fadePhase = 0;
    }

    // wet / dry (Voicing switch), then write back
    for (int i = 0; i < n; ++i)
    {
        const float mix = wetOnly ? 1.0f : wetMix.getNextValue();
        for (int c = 0; c < active; ++c)
            data[c][i] = wetOnly ? wk[c][i] : dry[c][static_cast<size_t> (i)] + mix * (wk[c][i] - dry[c][static_cast<size_t> (i)]);
    }
    if (dualMono)
        std::copy (data[0], data[0] + n, data[1]);
}

juce::AudioProcessorEditor* FourRealAudioProcessor::createEditor()
{
    return new FourRealAudioEditor (*this);
}

void FourRealAudioProcessor::requestLearn (int slot)
{
    if (slot >= 0 && slot < kNumSlots)
        learnRequest.store (slot + 1);
}

void FourRealAudioProcessor::requestStopLearn()
{
    learnRequest.store (-1);
}

void FourRealAudioProcessor::clearSlot (int slot)
{
    if (slot < 0 || slot >= kNumSlots)
        return;
    {
        const juce::SpinLock::ScopedLockType lock (slotsLock);
        slotsShared.valid[slot] = false;
    }
    slotValidUi[(size_t) slot].store (false);
    slotsToAudio.store (true);
}

// Audio thread: start / stop of a calibration, and finishing it by itself after 8 s of loud playing.
void FourRealAudioProcessor::handleLearning()
{
    fourreal::SpecStream* const stream = activeStreams();
    const int req = learnRequest.exchange (0);
    auto finish = [this, stream]
    {
        float feat[64] = {};
        const int k = learningSlot;
        learningSlot = -1;
        learningSlotUi.store (-1);
        if (k >= 0 && stream[0].finishLearn (feat, 64))
        {
            slotsAudio.valid[k] = true;
            std::copy (feat, feat + stream[0].featureSize(), slotsAudio.feat[k]);
            appliedCell[k] = -1; // applySlots() sends it to the streams together with the mapped cell
            publishPending = true;
            slotValidUi[(size_t) k].store (true);
        }
    };
    if (req > 0)
    {
        if (learningSlot >= 0)
            finish();
        learningSlot = req - 1;
        learningSlotUi.store (learningSlot);
        learnSecondsUi.store (0.0f);
        stream[0].beginLearn();
    }
    else if (req == -1 && learningSlot >= 0)
        finish();
    else if (learningSlot >= 0 && stream[0].learnSeconds() >= 8.0)
        finish();
}

// Audio thread: take over what the UI changed, publish a finished calibration, keep the mapped cells up to date.
void FourRealAudioProcessor::applySlots()
{
    if (slotsToAudio.load())
    {
        const juce::SpinLock::ScopedTryLockType lock (slotsLock);
        if (lock.isLocked())
        {
            for (int k = 0; k < kNumSlots; ++k)
            {
                slotsAudio.valid[k] = slotsShared.valid[k];
                std::copy (slotsShared.feat[k], slotsShared.feat[k] + 64, slotsAudio.feat[k]);
                appliedCell[k] = -1;
            }
            slotsToAudio.store (false);
            publishPending = false; // slotsShared is newer than anything pending
        }
    }
    if (publishPending)
    {
        const juce::SpinLock::ScopedTryLockType lock (slotsLock);
        if (lock.isLocked())
        {
            for (int k = 0; k < kNumSlots; ++k)
            {
                slotsShared.valid[k] = slotsAudio.valid[k];
                std::copy (slotsAudio.feat[k], slotsAudio.feat[k] + 64, slotsShared.feat[k]);
            }
            publishPending = false;
        }
    }
    fourreal::SpecStream* const stream = activeStreams();
    for (int k = 0; k < kNumSlots; ++k)
    {
        const int cell = static_cast<int> (apvts.getRawParameterValue ("slotCell" + juce::String (k))->load() + 0.5f);
        if (cell != appliedCell[k])
        {
            for (int c = 0; c < 2; ++c)
                stream[c].setSlot (k, slotsAudio.feat[k], cell, slotsAudio.valid[k]);
            appliedCell[k] = cell;
        }
    }
}

void FourRealAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
    {
        while (auto* old = xml->getChildByName ("SLOTS"))
            xml->removeChildElement (old, true);
        auto* e = xml->createNewChildElement ("SLOTS");
        const int n = juce::jmin (64, library().reference().autoSource.nMask);
        const juce::SpinLock::ScopedLockType lock (slotsLock);
        for (int k = 0; k < kNumSlots; ++k)
        {
            if (! slotsShared.valid[k] || n == 0)
                continue;
            juce::StringArray values;
            for (int i = 0; i < n; ++i)
                values.add (juce::String (slotsShared.feat[k][i], 4));
            e->setAttribute ("slot" + juce::String (k), values.joinIntoString (" "));
        }
        copyXmlToBinary (*xml, destData);
    }
}

void FourRealAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            SlotData loaded;
            const int n = juce::jmin (64, library().reference().autoSource.nMask);
            if (auto* e = xml->getChildByName ("SLOTS"))
            {
                for (int k = 0; k < kNumSlots; ++k)
                {
                    const auto tokens = juce::StringArray::fromTokens (e->getStringAttribute ("slot" + juce::String (k)), " ", "");
                    if (n > 0 && tokens.size() == n)
                    {
                        for (int i = 0; i < n; ++i)
                            loaded.feat[k][i] = tokens[i].getFloatValue();
                        loaded.valid[k] = true;
                    }
                }
                xml->removeChildElement (e, true);
            }
            {
                const juce::SpinLock::ScopedLockType lock (slotsLock);
                slotsShared = loaded;
            }
            for (int k = 0; k < kNumSlots; ++k)
                slotValidUi[(size_t) k].store (loaded.valid[k]);
            slotsToAudio.store (true);
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
        }
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FourRealAudioProcessor();
}

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <SpmData.h>

#include <cstring>

namespace
{
    // Plugin variant, chosen by the compile definition FOURREAL_MODEL_<CODE> (see CMakeLists.txt).
    const char* variantCode()
    {
#if defined(FOURREAL_MODEL_LP)
        return "LP";
#elif defined(FOURREAL_MODEL_335)
        return "335";
#elif defined(FOURREAL_MODEL_SG)
        return "SG";
#elif defined(FOURREAL_MODEL_PRS)
        return "PRS";
#elif defined(FOURREAL_MODEL_RIC)
        return "RIC";
#elif defined(FOURREAL_MODEL_STR)
        return "STR";
#elif defined(FOURREAL_MODEL_TCR)
        return "TCR";
#elif defined(FOURREAL_MODEL_FLC)
        return "FLC";
#else
        return "LP"; // default for IDE builds without a variant definition
#endif
    }

    juce::String volId (size_t i) { return "vol" + juce::String ((int) i); }
    juce::String toneId (size_t i) { return "tone" + juce::String ((int) i); }

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
}

const fourreal::GuitarModel& FourRealAudioProcessor::targetModel()
{
    static const fourreal::GuitarModel* model = fourreal::findModelByCode (variantCode());
    jassert (model != nullptr);
    return *model;
}

const juce::String FourRealAudioProcessor::getName() const
{
    return juce::String ("4real-audio ") + variantCode();
}

FourRealAudioProcessor::FourRealAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    {
        std::string error;
        specLoaded = fourreal::SpecModel::load (SpmData::model_spm, static_cast<size_t> (SpmData::model_spmSize), spec, error);
        jassert (specLoaded); // the embedded .spm must always parse
        if (specLoaded)
            for (auto& st : stream)
                st.prepare (spec, 48000.0);
    }

    const auto& target = targetModel();
    voicingParamIds = { "voicing", "keepLevel", "srcGuitar", "srcPos", "tgtSel" };
    for (size_t i = 0; i < target.volumes.size(); ++i)
        voicingParamIds.add (volId (i));
    for (size_t i = 0; i < target.tones.size(); ++i)
        voicingParamIds.add (toneId (i));

    for (const auto& id : voicingParamIds)
        apvts.addParameterListener (id, this);
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
    if (autoSourceAvailable())
        sourceNames.add ("Auto (listens to my guitar)"); // last entry, so saved projects keep their guitar choice
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "srcGuitar", 1 }, "Source guitar", sourceNames, 0));
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "srcPos", 1 }, "Source pickup position",
        StringArray { "Position 1", "Position 2", "Position 3", "Position 4" }, 0)); // at most 4 recorded positions per guitar

    const auto& target = targetModel();
    StringArray targetPositions;
    for (const auto& p : target.positions)
        targetPositions.add (p.name);
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "tgtSel", 1 }, "Pickup selector", targetPositions, 0));

    auto knobParam = [&params] (const juce::String& id, const juce::String& name)
    {
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 10.0f, 0.1f), 10.0f));
    };
    for (size_t i = 0; i < target.volumes.size(); ++i)
        knobParam (volId (i), target.volumes[i].label);
    for (size_t i = 0; i < target.tones.size(); ++i)
        knobParam (toneId (i), target.tones[i].label);

    return { params.begin(), params.end() };
}

bool FourRealAudioProcessor::autoSourceAvailable()
{
    static const bool available = []
    {
        fourreal::SpecModel m;
        std::string error;
        return fourreal::SpecModel::load (SpmData::model_spm, static_cast<size_t> (SpmData::model_spmSize), m, error)
               && m.autoSource.present && m.autoSource.enabled;
    }();
    return available;
}

juce::String FourRealAudioProcessor::sourceLabel (int index) const
{
    if (! specLoaded || index < 0 || index >= spec.numSources)
        return {};
    const auto& s = spec.sources[static_cast<size_t> (index)];
    const auto* g = fourreal::findModelByCode (s.code);
    if (g == nullptr)
        return s.cell;
    const int pos = juce::jlimit (0, (int) g->positions.size() - 1, s.position);
    return juce::String (g->displayName) + " " + juce::String (g->positions[static_cast<size_t> (pos)].name).toLowerCase();
}

int FourRealAudioProcessor::sourceIndexFor (int guitar, int position) const
{
    const auto& models = fourreal::allModels();
    const auto& g = models[static_cast<size_t> (juce::jlimit (0, (int) models.size() - 1, guitar))];
    const int pos = juce::jlimit (0, (int) g.positions.size() - 1, position);
    const int idx = specLoaded ? spec.findSource (g.code, pos) : -1;
    jassert (idx >= 0);
    return idx >= 0 ? idx : 0;
}

std::vector<float> FourRealAudioProcessor::designKnobTaps() const
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    using namespace fourreal;

    const int tgtSel = static_cast<int> (get ("tgtSel") + 0.5f);

    // volume / tone knobs of the target guitar: physical model, "knobs now" relative to "knobs full"
    // (the model was trained with the knobs full, so at full knobs this is the identity).
    const auto grid = makeFrequencyGrid (currentSampleRate);
    const auto& targetDef = targetModel();
    ModelSettings full, now;
    full.position = now.position = juce::jlimit (0, (int) targetDef.positions.size() - 1, tgtSel);
    for (size_t i = 0; i < targetDef.volumes.size(); ++i)
    {
        full.volumes.push_back (10.0);
        now.volumes.push_back (get (volId (i).toRawUTF8()));
    }
    for (size_t i = 0; i < targetDef.tones.size(); ++i)
    {
        full.tones.push_back (10.0);
        now.tones.push_back (get (toneId (i).toRawUTF8()));
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

    if (specLoaded)
        for (auto& st : stream)
        {
            st.prepare (spec, sampleRate);
            st.reset();
        }
    activeTarget = activeSource = -1;
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

// n <= kSlice samples, in place.
void FourRealAudioProcessor::processSlice (float* const* data, int numChannels, int n)
{
    // Which (target position, source cell) pair is selected right now.
    const int target = juce::jlimit (0, juce::jmax (0, spec.numTargets - 1),
                                     static_cast<int> (apvts.getRawParameterValue ("tgtSel")->load() + 0.5f));
    const int guitarChoice = static_cast<int> (apvts.getRawParameterValue ("srcGuitar")->load() + 0.5f);
    const bool autoSrc = specLoaded && autoSourceAvailable() && guitarChoice >= (int) fourreal::allModels().size();
    const int source = autoSrc ? 0 : sourceIndexFor (guitarChoice, static_cast<int> (apvts.getRawParameterValue ("srcPos")->load() + 0.5f));
    if (specLoaded && (target != activeTarget || source != activeSource || autoSrc != activeAuto))
    {
        for (auto& st : stream)
        {
            st.setCondition (target, source);
            st.setAutoSource (autoSrc);
        }
        activeTarget = target;
        activeSource = source;
        activeAuto = autoSrc;
        autoActiveUi.store (autoSrc);
    }

    // Voicing off and settled: nothing to compute (the streams are cleared when it is switched back on).
    if (wetMix.getTargetValue() < 0.5f && ! wetMix.isSmoothing())
    {
        bypassed = true;
        return;
    }
    if (bypassed)
    {
        bypassed = false;
        if (specLoaded)
            for (auto& st : stream)
                st.reset();
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
    if (specLoaded && ! autoSrc && spec.sources[static_cast<size_t> (source)].cell == spec.targetCells[static_cast<size_t> (target)])
        intensity = 0.0f;

    float* wk[2] = { work[0].data(), work[1].data() };
    for (int c = 0; c < active; ++c)
        std::copy (data[c], data[c] + n, wk[c]);

    if (specLoaded && intensity > 0.001f)
    {
        if (engineIdle)
        {
            for (auto& st : stream)
                st.reset(); // history is stale after being idle
            engineIdle = false;
        }
        for (int c = 0; c < active; ++c)
        {
            stream[c].setIntensity (intensity);
            stream[c].setAnalysisGain (autoGain);
            stream[c].process (wk[c], wk[c], n);
        }
        if (autoSrc)
        {
            const auto& w = stream[0].autoWeights();
            for (size_t i = 0; i < w.size() && i < autoWeightsUi.size(); ++i)
                autoWeightsUi[i].store (static_cast<float> (w[i]), std::memory_order_relaxed);
        }
    }
    else
        engineIdle = true;

    knobFir.process (wk, active, n);

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

void FourRealAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void FourRealAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FourRealAudioProcessor();
}

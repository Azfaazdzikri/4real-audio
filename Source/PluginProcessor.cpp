#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <NgmData.h>

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
        ngmLoaded = fourreal::NgmModel::load (NgmData::model_ngm, static_cast<size_t> (NgmData::model_ngmSize), ngm, error);
        jassert (ngmLoaded); // the embedded .ngm must always parse
        if (ngmLoaded)
            for (auto& st : stream)
                st.prepare (ngm);
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
    // How much of the network's residual is added on top of the per-pair EQ (0 = EQ only, 100 = full network).
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "neural", 1 }, "Neural amount", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 25.0f,
        AudioParameterFloatAttributes().withLabel ("%")));

    StringArray sourceNames;
    for (const auto& m : fourreal::allModels())
        sourceNames.add (m.displayName);
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

int FourRealAudioProcessor::sourceIndexFor (int guitar, int position) const
{
    const auto& models = fourreal::allModels();
    const auto& g = models[static_cast<size_t> (juce::jlimit (0, (int) models.size() - 1, guitar))];
    const int pos = juce::jlimit (0, (int) g.positions.size() - 1, position);
    const int idx = ngmLoaded ? ngm.findSource (g.code, pos) : -1;
    jassert (idx >= 0);
    return idx >= 0 ? idx : 0;
}

FourRealAudioProcessor::Taps FourRealAudioProcessor::designTaps() const
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    using namespace fourreal;

    Taps taps;
    const int tgtSel = static_cast<int> (get ("tgtSel") + 0.5f);
    const int target = juce::jlimit (0, juce::jmax (0, ngm.numTargets - 1), tgtSel);

    // 1) per-pair EQ from the .ngm
    if (ngmLoaded)
        taps.eq = designEqTaps (ngm, target, sourceIndexFor (static_cast<int> (get ("srcGuitar") + 0.5f),
                                                              static_cast<int> (get ("srcPos") + 0.5f)),
                                currentSampleRate);
    else
        taps.eq = identityTaps();

    // 2) volume / tone knobs of the target guitar: physical model, "knobs now" relative to "knobs full"
    //    (the network was trained with the knobs full, so at full knobs this is the identity).
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
    taps.knobs = designVoicingTaps (modelResponse (grid, targetDef, full), modelResponse (grid, targetDef, now),
                                    grid, currentSampleRate, get ("keepLevel") > 0.5f);
    return taps;
}

void FourRealAudioProcessor::parameterChanged (const juce::String&, float)
{
    triggerAsyncUpdate(); // coalesces bursts of knob movements; runs on the message thread
}

void FourRealAudioProcessor::handleAsyncUpdate()
{
    auto taps = designTaps();
    {
        const juce::SpinLock::ScopedLockType lock (tapsLock);
        pendingTaps = std::move (taps);
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

    eqFir.prepare (2, fourreal::kVoicingTaps, kSlice);
    knobFir.prepare (2, fourreal::kVoicingTaps, kSlice);
    for (int c = 0; c < 2; ++c)
    {
        work[c].assign (static_cast<size_t> (kSlice), 0.0f);
        dry[c].assign (static_cast<size_t> (kSlice), 0.0f);
        pre[c].assign (static_cast<size_t> (kSlice), 0.0f);
    }
    prevAmount = 0.0f;
    netIdle = true;

    if (ngmLoaded)
        for (auto& st : stream)
            st.reset();
    activeTarget = activeSource = -1;
    levelPower = -1.0f;
    autoGain = 1.0f;

    {
        const juce::SpinLock::ScopedLockType lock (tapsLock);
        pendingTaps = designTaps();
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
            eqFir.setTaps (pendingTaps.eq.data());
            knobFir.setTaps (pendingTaps.knobs.data());
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
    const int target = juce::jlimit (0, juce::jmax (0, ngm.numTargets - 1),
                                     static_cast<int> (apvts.getRawParameterValue ("tgtSel")->load() + 0.5f));
    const int source = sourceIndexFor (static_cast<int> (apvts.getRawParameterValue ("srcGuitar")->load() + 0.5f),
                                       static_cast<int> (apvts.getRawParameterValue ("srcPos")->load() + 0.5f));
    if (ngmLoaded && (target != activeTarget || source != activeSource))
    {
        for (auto& st : stream)
            st.setCondition (target, source, activeTarget < 0);
        activeTarget = target;
        activeSource = source;
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
        if (ngmLoaded)
            for (auto& st : stream)
                st.reset();
    }

    // A guitar is usually mono: when both channels are identical, run the network once.
    const bool dualMono = numChannels == 2
        && std::memcmp (data[0], data[1], sizeof (float) * static_cast<size_t> (n)) == 0;
    const int active = dualMono ? 1 : numChannels;
    const bool wetOnly = wetMix.getTargetValue() > 0.5f && ! wetMix.isSmoothing();

    // keep a dry copy only while the Voicing switch is (or just was) off
    if (! wetOnly)
        for (int c = 0; c < active; ++c)
            std::copy (data[c], data[c] + n, dry[c].begin());

    // --- auto level: estimate the input level slowly (only while there is signal), and bring it to the level
    //     the network was trained at; the gain is undone after the network, so dynamics stay intact.
    const float sr = static_cast<float> (currentSampleRate);
    const bool autoOn = apvts.getRawParameterValue ("autoLevel")->load() > 0.5f && ngmLoaded;
    float gStart = autoGain, gEnd = 1.0f;
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
            const float refPower = std::pow (10.0f, ngm.referenceRmsDb / 10.0f);
            wanted = juce::jlimit (0.0631f, 63.1f, std::sqrt (refPower / levelPower)); // +-24 dB .. +36 dB
        }
        autoGain += (wanted - autoGain) * (1.0f - std::exp (-static_cast<float> (n) / (0.5f * sr)));
        gEnd = autoGain;
    }
    else
    {
        autoGain = 1.0f;
        gEnd = 1.0f;
    }

    float* wk[2] = { work[0].data(), work[1].data() };
    for (int c = 0; c < active; ++c)
        for (int i = 0; i < n; ++i)
            wk[c][i] = data[c][i] * (gStart + (gEnd - gStart) * static_cast<float> (i) / static_cast<float> (n));

    eqFir.process (wk, active, n);

    // Neural residual on top of the EQ, blended by the "Neural amount" knob. At 0 % the network is not run at all.
    const float amount = apvts.getRawParameterValue ("neural")->load() * 0.01f;
    if (ngmLoaded && (amount > 0.001f || prevAmount > 0.001f))
    {
        if (netIdle)
        {
            for (auto& st : stream)
                st.reset(); // the streams' history is stale after being idle
            netIdle = false;
        }
        for (int c = 0; c < active; ++c)
        {
            std::copy (wk[c], wk[c] + n, pre[c].begin());
            stream[c].process (wk[c], wk[c], n);
            for (int i = 0; i < n; ++i)
            {
                const float a = prevAmount + (amount - prevAmount) * static_cast<float> (i + 1) / static_cast<float> (n);
                const float x = pre[c][static_cast<size_t> (i)];
                wk[c][i] = x + a * (wk[c][i] - x);
            }
        }
    }
    else
        netIdle = true;
    prevAmount = amount;

    for (int c = 0; c < active; ++c)
        for (int i = 0; i < n; ++i)
            wk[c][i] /= (gStart + (gEnd - gStart) * static_cast<float> (i) / static_cast<float> (n));

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

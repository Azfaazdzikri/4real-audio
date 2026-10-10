#include "PluginProcessor.h"
#include "PluginEditor.h"

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

    StringArray sourceNames;
    for (const auto& m : fourreal::allModels())
        sourceNames.add (m.displayName);
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "srcGuitar", 1 }, "Source guitar", sourceNames, 0));
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "srcPos", 1 }, "Source pickup position",
        StringArray { "Position 1 (bridge)", "Position 2", "Position 3", "Position 4", "Position 5 (neck)" }, 0));

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

std::vector<float> FourRealAudioProcessor::designTaps() const
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    if (get ("voicing") < 0.5f)
        return fourreal::identityTaps();

    using namespace fourreal;
    const auto grid = makeFrequencyGrid (currentSampleRate);

    const auto& models = allModels();
    const auto& sourceModel = models[static_cast<size_t> (juce::jlimit (0, (int) models.size() - 1,
                                                                        static_cast<int> (get ("srcGuitar") + 0.5f)))];
    ModelSettings source;
    source.position = juce::jlimit (0, (int) sourceModel.positions.size() - 1,
                                    static_cast<int> (get ("srcPos") + 0.5f));

    const auto& targetDef = targetModel();
    ModelSettings target;
    target.position = juce::jlimit (0, (int) targetDef.positions.size() - 1,
                                    static_cast<int> (get ("tgtSel") + 0.5f));
    for (size_t i = 0; i < targetDef.volumes.size(); ++i)
        target.volumes.push_back (get (volId (i).toRawUTF8()));
    for (size_t i = 0; i < targetDef.tones.size(); ++i)
        target.tones.push_back (get (toneId (i).toRawUTF8()));

    return designVoicingTaps (modelResponse (grid, sourceModel, source),
                              modelResponse (grid, targetDef, target),
                              grid, currentSampleRate, get ("keepLevel") > 0.5f);
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

    inputGain.reset (sampleRate, 0.02);
    outputGain.reset (sampleRate, 0.02);
    inputGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("inputGain")->load()));
    outputGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("outputGain")->load()));

    voicing.prepare (2, fourreal::kVoicingTaps, 1024);
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
            voicing.setTaps (pendingTaps.data());
            tapsReady.store (false);
        }
    }

    inputGain.setTargetValue (
        juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("inputGain")->load()));
    outputGain.setTargetValue (
        juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("outputGain")->load()));

    applySmoothedGain (buffer, inputGain);

    // Source-guitar -> target-guitar voicing (physical model; neural stage comes later).
    voicing.process (buffer.getArrayOfWritePointers(), juce::jmin (buffer.getNumChannels(), 2),
                     buffer.getNumSamples());

    applySmoothedGain (buffer, outputGain);

    outputPeak.store (peakOf (buffer));
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

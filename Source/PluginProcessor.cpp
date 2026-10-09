#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Parameters that change the voicing filter.
    const char* const voicingParamIds[] = { "voicing", "keepLevel", "srcGuitar", "srcPos", "tgtSel",
                                            "volNeck", "volBridge", "toneNeck", "toneBridge" };

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

FourRealAudioProcessor::FourRealAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (auto* id : voicingParamIds)
        apvts.addParameterListener (id, this);
}

FourRealAudioProcessor::~FourRealAudioProcessor()
{
    cancelPendingUpdate();
    for (auto* id : voicingParamIds)
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

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "srcGuitar", 1 }, "Source guitar", StringArray { "Strat HSS", "Strat SSS" }, 0));
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "srcPos", 1 }, "Source pickup position",
        StringArray { "1 Bridge", "2 Bridge+Mid", "3 Mid", "4 Mid+Neck", "5 Neck" }, 0));
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "tgtSel", 1 }, "Les Paul selector", StringArray { "Neck", "Both", "Bridge" }, 2));

    auto knobParam = [&params] (const char* id, const char* name)
    {
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 10.0f, 0.1f), 10.0f));
    };
    knobParam ("volNeck", "Volume neck");
    knobParam ("volBridge", "Volume bridge");
    knobParam ("toneNeck", "Tone neck");
    knobParam ("toneBridge", "Tone bridge");

    return { params.begin(), params.end() };
}

std::vector<float> FourRealAudioProcessor::designTaps() const
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    if (get ("voicing") < 0.5f)
        return fourreal::identityTaps();

    using namespace fourreal;
    const auto grid = makeFrequencyGrid (currentSampleRate);

    StratParams source;
    source.position = static_cast<int> (get ("srcPos") + 0.5f) + 1;
    const auto kind = get ("srcGuitar") < 0.5f ? StratKind::HSS : StratKind::SSS;

    LesPaulParams target;
    target.selector = static_cast<LesPaulSelector> (static_cast<int> (get ("tgtSel") + 0.5f));
    target.volNeck = get ("volNeck");
    target.volBridge = get ("volBridge");
    target.toneNeck = get ("toneNeck");
    target.toneBridge = get ("toneBridge");

    return designVoicingTaps (stratResponse (grid, kind, source),
                              lesPaulResponse (grid, target),
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

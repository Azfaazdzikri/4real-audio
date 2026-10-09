#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const juce::StringArray guitarChoices { "Stratocaster (single coil)",
                                            "Jazzmaster",
                                            "Les Paul (humbucker)" };

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
}

juce::AudioProcessorValueTreeState::ParameterLayout FourRealAudioProcessor::createLayout()
{
    using namespace juce;

    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "inputGain", 1 }, "Input",
        NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "outputGain", 1 }, "Output",
        NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    // Stored for later; not used by the DSP yet.
    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "source", 1 }, "Source guitar", guitarChoices, 0));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "target", 1 }, "Target guitar", guitarChoices, 2));

    return { params.begin(), params.end() };
}

void FourRealAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    inputGain.reset (sampleRate, 0.02);
    outputGain.reset (sampleRate, 0.02);

    inputGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("inputGain")->load()));
    outputGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("outputGain")->load()));
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

    inputGain.setTargetValue (
        juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("inputGain")->load()));
    outputGain.setTargetValue (
        juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("outputGain")->load()));

    applySmoothedGain (buffer, inputGain);

    // ---------------------------------------------------------------------
    // TODO: neural-network guitar transform goes here (source -> target).
    // For now the signal passes through clean.
    // ---------------------------------------------------------------------

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

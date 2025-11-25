/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"

namespace
{
    // Feedback block: how the old delay sample is fed back
    inline float applyFeedback(float previousDelaySample, float feedbackAmount)
    {
        return previousDelaySample * feedbackAmount;
    }

    // Mix block: blend dry and wet
    inline float applyMix(float dry, float wet, float mixAmount)
    {
        return dry * (1.0f - mixAmount) + wet * mixAmount;
    }

    // Gain block: final output gain
    inline float applyGain(float sample, float gain)
    {
        return sample * gain;
    }
}

//==============================================================================
MagicGUIAudioProcessor::MagicGUIAudioProcessor():MagicProcessor(),
    apvts(*this, nullptr, "PARAMS", createParameterLayout())
{
            FOLEYS_SET_SOURCE_PATH (__FILE__);
            magicState.setGuiValueTree(BinaryData::magic_xml, BinaryData::magic_xmlSize);

            timeParam = apvts.getRawParameterValue("TIME");
            feedbackParam = apvts.getRawParameterValue("FEEDBACK");
            mixParam = apvts.getRawParameterValue("MIX");
            gainParam = apvts.getRawParameterValue("GAIN");
}

MagicGUIAudioProcessor::~MagicGUIAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout
MagicGUIAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "TIME", "Time",
        juce::NormalisableRange<float>(10.0f, 800.0f, 1.0f), 100.0f)); // ms

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "FEEDBACK", "Feedback",
        juce::NormalisableRange<float>(0.0f, 0.95f, 0.01f), 0.4f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "MIX", "Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "GAIN", "Gain",
        juce::NormalisableRange<float>(-24.0f, 6.0f, 0.1f), 0.0f)); // dB

    return { params.begin(), params.end() };
}

//==============================================================================
const juce::String MagicGUIAudioProcessor::getName() const
{
    return JucePlugin_Name;
}



double MagicGUIAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}



//==============================================================================
void MagicGUIAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
    spec.numChannels = (juce::uint32)getTotalNumOutputChannels();

    int delayMilliseconds = 200;
    auto delaySamples = (int)std::round(sampleRate * delayMilliseconds / 1000.0);
    delayBuffer.setSize(2, delaySamples);
    delayBuffer.clear();
    delayBufferPos = 0;
}

void MagicGUIAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

void MagicGUIAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear any extra output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // ===== Parameter block (read once per block) =====
    const float feedback = *feedbackParam;
    const float mix = *mixParam;
    const float gainDb = gainParam->load();   // read atomic safely
    const float outGain = juce::Decibels::decibelsToGain(gainDb);



    const int delayBufferSize = delayBuffer.getNumSamples();

    // ===== Per-channel processing =====
    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        float* channelData = buffer.getWritePointer(channel);

        // Local copy of write position for this channel
        int delayPos = delayBufferPos;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float drySample = channelData[i];

            // ---- Feedback block ----
            const float rawDelaySample = delayBuffer.getSample(channel, delayPos);
            const float feedbackSample = applyFeedback(rawDelaySample, feedback);

            // Write new value into delay buffer (current input + feedback)
            const float newDelaySample = drySample + feedbackSample;
            delayBuffer.setSample(channel, delayPos, newDelaySample);

            // Advance circular buffer position
            ++delayPos;
            if (delayPos == delayBufferSize)
                delayPos = 0;

            // The "wet" signal we output is the feedback sample
            const float wetSample = feedbackSample;

            // ---- Mix block ----
            float outSample = applyMix(drySample, wetSample, mix);

            // ---- Gain block ----
            outSample = applyGain(outSample, outGain);

            channelData[i] = outSample;
        }
    }

    // Update shared delayBufferPos once per block
    delayBufferPos += buffer.getNumSamples();
    if (delayBufferPos >= delayBufferSize)
        delayBufferPos -= delayBufferSize;
}







//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MagicGUIAudioProcessor();
}

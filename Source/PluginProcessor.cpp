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

            bypassParam = apvts.getRawParameterValue("BYPASS");
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

    juce::NormalisableRange<float> timeRange{ 10.0f, 1000.0f, 0.0f, 0.5f };
    //                        start  end     interval  skew
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "TIME", "Time",
        timeRange,
        100.0f));

    //params.push_back(std::make_unique<juce::AudioParameterFloat>(
    //    "TIME", "Time", juce::NormalisableRange<float>(1.0f, 2000.0f, 20.0f), 50.0f)); // ms

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "FEEDBACK", "Feedback",
        juce::NormalisableRange<float>(0.0f, 0.99f, 0.01f), 0.4f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "MIX", "Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.05f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "GAIN", "Gain",
        juce::NormalisableRange<float>(-10.0f, 30.0f, 1.0f), 0.0f)); // dB

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "BYPASS", "Bypass",
        false));

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

    const float maxDelayMs = 1000.0f;
    const int   maxDelaySamples = (int)std::ceil(sampleRate * maxDelayMs / 1000.0f);

    delayBuffer.setSize(getTotalNumOutputChannels(), maxDelaySamples);
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

    const float isBypassed = bypassParam->load();
    if (isBypassed < 0.5)
    {
        return; // passthrough
    }

    // ===== Parameter block (read once per block) =====
    const float timeMs = timeParam->load();        // TIME in ms
    const float feedback = feedbackParam->load();    // 0..0.95
    const float mix = mixParam->load();         // 0..1
    const float gainDb = gainParam->load();        // dB
    const float outGain = juce::Decibels::decibelsToGain(gainDb);

    const int delayBufferSize = delayBuffer.getNumSamples();
    const int numSamples = buffer.getNumSamples();
    const double sr = getSampleRate();

    // ===== compute two delay times (in samples) =====
    float d1SamplesF = timeMs * (float)sr * 0.001f;      // base delay
    float d2SamplesF = d1SamplesF * 1.6f;                 // second delay = T * 1.6

    const float maxIndex = (float)(delayBufferSize - 1);

    d1SamplesF = juce::jlimit(1.0f, maxIndex, d1SamplesF);
    d2SamplesF = juce::jlimit(1.0f, maxIndex, d2SamplesF);

    const int d1 = (int)d1SamplesF;
    const int d2 = (int)d2SamplesF;

    // ===== Per-channel processing =====
    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        float* channelData = buffer.getWritePointer(channel);

        // Local copy of write position for this channel
        int writePos = delayBufferPos;

        for (int i = 0; i < numSamples; ++i)
        {
            const float drySample = channelData[i];

            // ---- two read heads into the circular buffer ----
            int readPos1 = writePos - d1;
            if (readPos1 < 0)
                readPos1 += delayBufferSize;

            int readPos2 = writePos - d2;
            if (readPos2 < 0)
                readPos2 += delayBufferSize;

            const float delayed1 = delayBuffer.getSample(channel, readPos1);
            const float delayed2 = delayBuffer.getSample(channel, readPos2);

            // ---- Feedback block ----
            // average of both taps, then scaled by feedback
            const float tapsAverage = 0.5f * (delayed1 + delayed2);
            const float feedbackSample = applyFeedback(tapsAverage, feedback);

            // Write new value into delay buffer (current input + feedback)
            const float newDelaySample = drySample + feedbackSample;
            delayBuffer.setSample(channel, writePos, newDelaySample);

            // Advance circular buffer write position
            ++writePos;
            if (writePos >= delayBufferSize)
                writePos = 0;

            // Wet signal = average of both delayed taps
            const float wetSample = tapsAverage;

            // ---- Mix block ----
            float outSample = applyMix(drySample, wetSample, mix);

            // ---- Gain block ----
            outSample = applyGain(outSample, outGain);

            channelData[i] = outSample;
        }
    }

    // Update shared write position once per block
    delayBufferPos += numSamples;
    if (delayBufferPos >= delayBufferSize)
        delayBufferPos -= delayBufferSize;
}



//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MagicGUIAudioProcessor();
};

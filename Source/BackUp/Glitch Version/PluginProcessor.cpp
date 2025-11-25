/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"

namespace
{
    // How we combine taps into feedback
    inline float computeFeedback(float tap1, float tap2, float feedbackAmount)
    {
        const float avgTaps = 0.5f * (tap1 + tap2);
        return avgTaps * feedbackAmount;
    }

    // How we combine taps into the wet signal
    inline float computeWetSignal(float tap1, float tap2)
    {
        return 0.5f * (tap1 + tap2);
    }

    // How we mix dry + wet and apply output gain
    inline float applyMixAndGain(float dry,
        float wet,
        float mix,      // 0..1
        float outGain)  // linear
    {
        float out = dry * (1.0f - mix) + wet * mix;
        out *= outGain;
        return out;
    }
}

//==============================================================================
MagicGUIAudioProcessor::MagicGUIAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : foleys::MagicProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ), apvts(*this, nullptr, "PARAMS", createParameterLayout())
#endif
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
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
    spec.numChannels = (juce::uint32)getTotalNumOutputChannels();

    delayLine.prepare(spec);
    delayLine.reset();
}

void MagicGUIAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

float MagicGUIAudioProcessor::processEchoSample(int channel,
    float in,
    int tapA,
    int tapB,
    float feedbackAmount,
    float mix,
    float outGain)
{

    // Read two taps using popSample (correct API)
    float tap1 = delayLine.popSample(channel, (float)tapA);
    float tap2 = delayLine.popSample(channel, (float)tapB);

    // Feedback creation
    const float fb = computeFeedback(tap1, tap2, feedbackAmount);
    const float toDelay = in + fb;

    // Write into delay line
    delayLine.pushSample(channel, toDelay);

    // --- wet signal block ---
    const float wet = computeWetSignal(tap1, tap2);

    // --- mix + gain block ---
    return applyMixAndGain(in, wet, mix, outGain);

}


void MagicGUIAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels == 0)
        return;

    // ===== parameter block =====
    const float timeMs = *timeParam;
    const float feedback = *feedbackParam;
    const float mix = *mixParam;
    const float gainDb = gainParam->load();   // read atomic safely
    const float outGain = juce::Decibels::decibelsToGain(gainDb);

    // ===== delay time block (ms -> samples, golden ratio taps) =====
    const float baseSamples = timeMs * 0.001f * (float)currentSampleRate;

    float d1SamplesF = baseSamples;                                   // T
    float d2SamplesF = baseSamples * goldenRatio;                     // T *
    float d3SamplesF = baseSamples * goldenRatio * goldenRatio;      // T * 

    const float maxIndex = (float)(maxDelaySamples - 1);

    d1SamplesF = juce::jlimit(1.0f, maxIndex, d1SamplesF);
    d2SamplesF = juce::jlimit(1.0f, maxIndex, d2SamplesF);
    d3SamplesF = juce::jlimit(1.0f, maxIndex, d3SamplesF);

    const int d1 = (int)d1SamplesF;
    const int d2 = (int)d2SamplesF;
    const int d3 = (int)d3SamplesF;

    // ===== per-channel processing =====
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer(ch);

        // choose taps for this channel (mono vs stereo)
        int tapA = d1;
        int tapB = d2;

        if (numChannels > 1 && ch > 0)
        {
            // right / extra: and 
            tapA = d2;
            tapB = d3;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const float in = data[i];

            // *** main echo call - all logic hidden inside ***
            data[i] = processEchoSample(ch, in, tapA, tapB,
                feedback, mix, outGain);
        }
    }
}




//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MagicGUIAudioProcessor();
}

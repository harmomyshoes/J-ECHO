/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include <cmath>

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

    inline float softClipTanh(float x)
    {
        // Simple soft clip: smooth around ±1, no hard edge
        return std::tanh(x);
    }

    // Gain block: final output gain
    inline float applyGain(float sample, float gain)
    {
        // 1) Apply gain
        float y = sample * gain;

        // 2) Soft-clip with tanh
        y = softClipTanh(y);

        return y;
    }
}

//==============================================================================
MagicGUIAudioProcessor::MagicGUIAudioProcessor():MagicProcessor(),
    apvts(*this, nullptr, "PARAMS", createParameterLayout())
{
            FOLEYS_SET_SOURCE_PATH (__FILE__);
            magicState.setGuiValueTree(BinaryData::magic_xml, BinaryData::magic_xmlSize);

            bypassParam = apvts.getRawParameterValue("BYPASS");
            interpolateParam = apvts.getRawParameterValue("INTERPOLATION");
            timeParam_s = apvts.getRawParameterValue("TIME_S");
            timeParam_f = apvts.getRawParameterValue("TIME_F");
            feedbackParam = apvts.getRawParameterValue("FEEDBACK");
            mixParam = apvts.getRawParameterValue("MIX");
            gainParam = apvts.getRawParameterValue("GAIN");
            tap3Param = apvts.getRawParameterValue("TAP3");
            //timeParam = apvts.getRawParameterValue("TIME");
}

MagicGUIAudioProcessor::~MagicGUIAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout
MagicGUIAudioProcessor::createParameterLayout()
{

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    //params.push_back(std::make_unique<juce::AudioParameterFloat>(
    //"TIME", "Time", juce::NormalisableRange<float>(1.0f, 2000.0f, 20.0f), 50.0f)); // ms

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "TIME_S", "Time_S", juce::NormalisableRange<float>(0.0f, 200, 0.1f), 0.0f)); // ms

    juce::NormalisableRange<float> timeRange{ 1.0f, 1200.0f, 0.0f, 0.5f };
    //                        start  end     interval  skew
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "TIME_F", "Time_F",
        timeRange,
        300.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "TAP3", "Tap3 Multiplier",
        juce::NormalisableRange<float>(1.0f, 3.0f, 0.01f),
        1.618f));  // default = golden ratio

    //params.push_back(std::make_unique<juce::AudioParameterFloat>(
    //    "TIME", "Time", juce::NormalisableRange<float>(1.0f, 2000.0f, 20.0f), 50.0f)); // ms

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "FEEDBACK", "Feedback",
        juce::NormalisableRange<float>(0.0f, 1.01f, 0.01f), 0.4f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "MIX", "Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.05f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "GAIN", "Gain",
        juce::NormalisableRange<float>(-10.0f, 30.0f, 1.0f), 0.0f)); // dB

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "BYPASS", "Bypass",
        false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "INTERPOLATION", "Interpolation",
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

    const float maxDelayMs_s = 200.0f;
    const float maxDelayMs_f = 4000.0f;//The far higher due to the extra taps move range
    delayLine_s.prepare(sampleRate, maxDelayMs_s, getTotalNumOutputChannels());
    delayLine_f.prepare(sampleRate, maxDelayMs_f, getTotalNumOutputChannels());
    // Time smoothing: 0.05 seconds (50 ms) ramp time is a nice starting point
    timeMsSmoothed_s.reset(sampleRate, 0.10); // rampTimeSeconds
    timeMsSmoothed_f.reset(sampleRate, 0.10); // rampTimeSeconds
    tap3Smoothed.reset(sampleRate, 0.10f); // 10 ms ramp, same as time
    // Start the smoothed value at the current parameter value
    timeMsSmoothed_s.setCurrentAndTargetValue(timeParam_s->load());
    timeMsSmoothed_f.setCurrentAndTargetValue(timeParam_f->load());
    tap3Smoothed.setCurrentAndTargetValue(tap3Param->load());
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

    const int numSamples = buffer.getNumSamples();

    // Clear any extra output channels
    for (auto ch = totalNumInputChannels; ch < totalNumOutputChannels; ++ch)
        buffer.clear(ch, 0, numSamples);

    // ===== Bypass =====
    bool lastBypassState = false;
    bool bypass = (bypassParam->load() < 0.5f);

    if (bypass != lastBypassState)
    {
		// effect just turned OFF/just turned ON
		delayLine_f.reset();
        delayLine_s.reset();
        lastBypassState = bypass;
    }

    // now do the actual bypass logic
    if (bypass)
    {
        return;    // no processing
    }

    // ===== Parameter block (read once per block) =====
    const float timeMsTarget_s = timeParam_s->load();       // base delay time in ms
    const float timeMsTarget_f = timeParam_f->load();       // base delay time in ms
    const float feedback_s = 0.9;//hard setting for first delay line
    const float feedback_f = feedbackParam->load();   // 0..0.95
    const float mix = mixParam->load();        // 0..1
    const float gainDb = gainParam->load();       // dB
    const float outGain = juce::Decibels::decibelsToGain(gainDb);
    const bool  useInterp = (*interpolateParam > 0.5f);
    const float tap3Target = tap3Param->load();


    timeMsSmoothed_s.setTargetValue(timeMsTarget_s);
    timeMsSmoothed_f.setTargetValue(timeMsTarget_f);
    tap3Smoothed.setTargetValue(tap3Target);

    // ===== Per-sample / per-channel processing =====
    // We process sample-by-sample so the delayLine write index advances
    // once per sample (shared across all channels).
    for (int i = 0; i < numSamples; ++i)
    {
        const float timeMsSmoothedNow_s = timeMsSmoothed_s.getNextValue();
        const float timeMsSmoothedNow_f = timeMsSmoothed_f.getNextValue();
        const float tap3MultNow = tap3Smoothed.getNextValue();


        const float timeMsTap_s = timeMsSmoothedNow_s;
        const float timeMsTap_f_1 = timeMsSmoothedNow_f;
        // Second tap is 1.6x the first
        const float timeMsTap_f_2 = timeMsSmoothedNow_f * 1.618f; // JuceDelayLine will clamp to its maxDelay internally
        //const float timeMsTap_f_3 = timeMsSmoothedNow_f * 1.618f * 1.618f;
        // user-controlled tap 3
        const float timeMsTap_f_3 = timeMsSmoothedNow_f * tap3MultNow;

        const bool delayOff_s = (timeMsSmoothedNow_s < 1.0f);


        for (int channel = 0; channel < totalNumInputChannels; ++channel)
        {
            float* channelData = buffer.getWritePointer(channel);
            const float drySample = channelData[i];
            float out_s = 0.0f;

            //First delay line: short delay time
            if (!delayOff_s)
            {
                const float d_s = delayLine_s.readSampleMs(channel, timeMsTap_s, true);

                // feedback inside delay1
                float loopIn1 = drySample + applyFeedback(d_s, feedback_s);
                loopIn1 = juce::jlimit(-1.0f, 1.0f, loopIn1); // safety clip
                delayLine_s.writeSample(channel, loopIn1);

                out_s = 0.8 * d_s; // output of first delay
            }
            else
            {
                out_s = drySample;
            }

            // Second delay line: two taps
            // ---- Two taps from the delay line (in ms) ----
            {
                const float delayed_f_1 = delayLine_f.readSampleMs(channel, timeMsTap_f_1, useInterp); // integer delay
                const float delayed_f_2 = delayLine_f.readSampleMs(channel, timeMsTap_f_2, useInterp); // integer delay
                const float delayed_f_3 = delayLine_f.readSampleMs(channel, timeMsTap_f_3, useInterp); // integer delay

                const float out_f = 0.35f * (delayed_f_1 + delayed_f_2 + delayed_f_3);
                const float loopIn2 = out_s + applyFeedback(out_f, feedback_f);

                // Write to the output of the delay line
                float newDelaySample = juce::jlimit(-1.0f, 1.0f, loopIn2);
                delayLine_f.writeSample(channel, newDelaySample);

                // Wet signal = average of both delayed taps + first delay line output
                float wetSample = out_s;
                if (delayOff_s)      // if short delay is off, don't double-count dry here
                    wetSample = 0.0f;
                wetSample += out_f;

                // ---- Mix block ----
                float outSample = applyMix(drySample, wetSample, mix);

                // ---- Gain block ----
                outSample = applyGain(outSample, outGain);

                channelData[i] = outSample;
            }
        }

        // After all channels for this sample: advance write index by 1
        delayLine_s.advance();
        delayLine_f.advance();
    }
}



//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MagicGUIAudioProcessor();
};

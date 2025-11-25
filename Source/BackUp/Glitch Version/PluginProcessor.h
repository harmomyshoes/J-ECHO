/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
*/
class MagicGUIAudioProcessor  : public foleys::MagicProcessor
{
public:
    //==============================================================================
    MagicGUIAudioProcessor();
    ~MagicGUIAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorValueTreeState::ParameterLayout
        MagicGUIAudioProcessor::createParameterLayout();

    //==============================================================================
    const juce::String getName() const override;

    double getTailLengthSeconds() const override;
    float MagicGUIAudioProcessor::processEchoSample(int channel,
        float in,
        int tapA,
        int tapB,
        float feedbackAmount,
        float mix,
        float outGain);


private:
    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* timeParam = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* gainParam = nullptr;

    static constexpr int maxDelaySamples = 480000; // ~2.5s at 192 kHz
    static constexpr float goldenRatio = 1.62f;
    double currentSampleRate = 48000.0;

    juce::dsp::DelayLine<float,
        juce::dsp::DelayLineInterpolationTypes::Linear> delayLine{ maxDelaySamples };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MagicGUIAudioProcessor)
};

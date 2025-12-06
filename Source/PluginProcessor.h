/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "JuceDelayLine.h"

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



private:
    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* timeParam_s = nullptr;
    std::atomic<float>* timeParam_f = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* gainParam = nullptr;
    std::atomic<float>* bypassParam = nullptr; // bool params are exposed as float [0,1]
    std::atomic<float>* interpolateParam = nullptr;
    std::atomic<float>* tap3Param = nullptr;

    //std::atomic<float>* timeParam = nullptr;

    JuceDelayLine delayLine_s;
    JuceDelayLine delayLine_f;

    juce::LinearSmoothedValue<float> timeMsSmoothed_s;
    juce::LinearSmoothedValue<float> timeMsSmoothed_f;
    juce::LinearSmoothedValue<float> tap3Smoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MagicGUIAudioProcessor)
};

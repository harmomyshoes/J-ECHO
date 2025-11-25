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



private:
    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;


    std::atomic<float>* timeParam = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* gainParam = nullptr;

    int delayBufferPos = 0;
    juce::AudioBuffer<float> delayBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MagicGUIAudioProcessor)
};

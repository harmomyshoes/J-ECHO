/*
  ==============================================================================

    JuceDelayLine.h
    Created: 29 Nov 2025 3:59:34pm
    Author:  Xie

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>


// A "self-contained" multi-channel delay line
// implemented on top of juce::AudioBuffer<float>.
class JuceDelayLine
{
public:
    JuceDelayLine() = default;

    // Prepare the delay line.
    // - sampleRate: host sample rate
    // - maxDelayMs: maximum delay time in milliseconds
    // - numChannels: number of channels to store
    void prepare(double sampleRate, float maxDelayMs, int numChannels)
    {
        jassert(sampleRate > 0);
        jassert(maxDelayMs > 0);
        jassert(numChannels > 0);

        sr = sampleRate;
        maxDelay = maxDelayMs;

        const int maxDelaySamples = (int)std::ceil(sr * maxDelay * 0.001f);

        buffer.setSize(numChannels, maxDelaySamples);
        buffer.clear();

        bufferLength = maxDelaySamples;
        writeIndex = 0;
    }

    // Clear contents
    void reset()
    {
        buffer.clear();
        writeIndex = 0;
    }

    // Write one sample into the delay line for a given channel.
    // (Call this once per channel per sample.)
    void writeSample(int channel, float x)
    {
        jassert(juce::isPositiveAndBelow(channel, buffer.getNumChannels()));
        jassert(bufferLength > 0);

        buffer.setSample(channel, writeIndex, x);
    }


    // Read a delayed sample for a given channel, using delay time in ms.
    // interpolate = true -> linear interpolation for fractional delays.
    float readSampleMs(int channel, float delayTimeMs, bool interpolate) const
    {
        jassert(juce::isPositiveAndBelow(channel, buffer.getNumChannels()));
        jassert(bufferLength > 0);

        // Clamp delay time to [0, maxDelay]
        delayTimeMs = juce::jlimit(0.0f, maxDelay, delayTimeMs);

        const float delaySamplesFloat = delayTimeMs * 0.001f * (float)sr;
        const int   delaySamplesInt = (int)delaySamplesFloat;

        // Base read index (integer)
        int readIndex = writeIndex - delaySamplesInt;
        while (readIndex < 0)
            readIndex += bufferLength;

        if (!interpolate)
        {
            // Integer delay only
            return buffer.getSample(channel, readIndex);
        }
        else
        {
            // Fractional delay (linear interpolation between two samples)
            const float frac = delaySamplesFloat - (float)delaySamplesInt;

            int readIndex2 = readIndex - 1;
            while (readIndex2 < 0)
                readIndex2 += bufferLength;

            const float y0 = buffer.getSample(channel, readIndex);
            const float y1 = buffer.getSample(channel, readIndex2);

            // lerp: y = y0 * (1 - frac) + y1 * frac
            return y0 + frac * (y1 - y0);
        }
    }

    // Advance the write index by 1 sample (call once per processed sample).
    void advance()
    {
        if (++writeIndex >= bufferLength)
            writeIndex = 0;
    }

    int getBufferLength() const noexcept { return bufferLength; }
    int getNumChannels() const noexcept { return buffer.getNumChannels(); }

private:
    juce::AudioBuffer<float> buffer;
    int    bufferLength = 0;
    int    writeIndex = 0;
    double sr = 44100.0;
    float  maxDelay = 1000.0f; // ms
};

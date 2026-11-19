#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "ChoirEngine.h"

class ChoirAudioProcessor : public juce::AudioProcessor
{
public:
    ChoirAudioProcessor();
    ~ChoirAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Choir"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    static constexpr int chunkSize = 512;

    ChoirEngine engine;
    juce::dsp::Reverb reverb;

    std::atomic<float>* pVoices     = nullptr;
    std::atomic<float>* pDetune     = nullptr;
    std::atomic<float>* pTiming     = nullptr;
    std::atomic<float>* pVibDepth   = nullptr;
    std::atomic<float>* pVibRate    = nullptr;
    std::atomic<float>* pWidth      = nullptr;
    std::atomic<float>* pReverbSize = nullptr;
    std::atomic<float>* pReverbMix  = nullptr;
    std::atomic<float>* pMix        = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoirAudioProcessor)
};

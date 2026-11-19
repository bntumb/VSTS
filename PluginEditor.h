#pragma once

#include "PluginProcessor.h"

class ChoirAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit ChoirAudioProcessorEditor (ChoirAudioProcessor&);
    ~ChoirAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void addKnob (const juce::String& paramId, const juce::String& text);

    ChoirAudioProcessor& processor;
    std::vector<std::unique_ptr<Knob>> knobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoirAudioProcessorEditor)
};

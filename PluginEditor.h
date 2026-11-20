#pragma once

#include "PluginProcessor.h"

class ChoirAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  private juce::Timer
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
    void timerCallback() override;

    ChoirAudioProcessor& processor;
    std::vector<std::unique_ptr<Knob>> knobs;

    juce::ComboBox keyBox, scaleBox, rangeBox;
    juce::ToggleButton midiButton { "MIDI chord mode" };
    juce::Label pitchLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAtt, scaleAtt, rangeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   midiAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoirAudioProcessorEditor)
};

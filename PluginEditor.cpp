#include "PluginEditor.h"

ChoirAudioProcessorEditor::ChoirAudioProcessorEditor (ChoirAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    addKnob ("voices",     "Voices");
    addKnob ("detune",     "Detune");
    addKnob ("timing",     "Timing");
    addKnob ("vibDepth",   "Vibrato");
    addKnob ("vibRate",    "Vib Rate");
    addKnob ("width",      "Width");
    addKnob ("reverbSize", "Hall Size");
    addKnob ("reverbMix",  "Hall Mix");
    addKnob ("mix",        "Dry/Wet");

    setSize (600, 320);
}

void ChoirAudioProcessorEditor::addKnob (const juce::String& paramId, const juce::String& text)
{
    auto k = std::make_unique<Knob>();

    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    k->label.setText (text, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, paramId, k->slider);

    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);
    knobs.push_back (std::move (k));
}

void ChoirAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1d2a));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("CHOIR", getLocalBounds().removeFromTop (40), juce::Justification::centred);
}

void ChoirAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().withTrimmedTop (40).reduced (10);
    const int cols = 5;
    const int rows = 2;
    const int cw = area.getWidth() / cols;
    const int ch = area.getHeight() / rows;

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const int row = (int) i / cols;
        const int col = (int) i % cols;
        // centre the shorter second row
        const int rowCount = row == 0 ? cols : (int) knobs.size() - cols;
        const int xOffset  = (cols - rowCount) * cw / 2;

        auto cell = juce::Rectangle<int> (area.getX() + xOffset + col * cw,
                                          area.getY() + row * ch, cw, ch).reduced (4);
        knobs[i]->label.setBounds (cell.removeFromTop (20));
        knobs[i]->slider.setBounds (cell);
    }
}

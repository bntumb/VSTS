#include "PluginEditor.h"

static juce::String noteName (float midi)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int n = (int) std::lround (midi);
    return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
}

ChoirAudioProcessorEditor::ChoirAudioProcessorEditor (ChoirAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    keyBox.addItemList ({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
    scaleBox.addItemList ({ "Major", "Minor", "Dorian", "Mixolydian" }, 1);
    addAndMakeVisible (keyBox);
    addAndMakeVisible (scaleBox);
    addAndMakeVisible (midiButton);
    keyAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "key", keyBox);
    scaleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "scale", scaleBox);
    midiAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>   (p.apvts, "midi", midiButton);

    pitchLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (pitchLabel);

    addKnob ("tune",   "Tune");
    addKnob ("glide",  "Glide");
    addKnob ("lead",   "Lead");
    addKnob ("spread", "Spread");
    addKnob ("width",  "Width");
    addKnob ("out",    "Output");
    addKnob ("iv1", "V1 Step");  addKnob ("iv2", "V2 Step");
    addKnob ("iv3", "V3 Step");  addKnob ("iv4", "V4 Step");
    addKnob ("reverbSize", "Hall Size");
    addKnob ("reverbMix",  "Hall Mix");
    addKnob ("lv1", "V1 Level"); addKnob ("lv2", "V2 Level");
    addKnob ("lv3", "V3 Level"); addKnob ("lv4", "V4 Level");

    setSize (720, 480);
    startTimerHz (20);
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

void ChoirAudioProcessorEditor::timerCallback()
{
    const float in = processor.getInputMidi(), lead = processor.getLeadMidi();
    pitchLabel.setText (in < 0.0f ? juce::String ("no pitch")
                                  : "sing " + noteName (in) + "  ->  " + noteName (lead),
                        juce::dontSendNotification);
}

void ChoirAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1d2a));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("CHOIR  harmonizer", getLocalBounds().removeFromTop (40), juce::Justification::centred);
}

void ChoirAudioProcessorEditor::resized()
{
    auto top = getLocalBounds().withTrimmedTop (40).removeFromTop (36).reduced (10, 4);
    keyBox.setBounds (top.removeFromLeft (70));
    top.removeFromLeft (8);
    scaleBox.setBounds (top.removeFromLeft (120));
    top.removeFromLeft (12);
    midiButton.setBounds (top.removeFromLeft (150));
    pitchLabel.setBounds (top);

    auto area = getLocalBounds().withTrimmedTop (84).reduced (10);
    const int cols = 6;
    const int rows = 3;
    const int cw = area.getWidth() / cols;
    const int ch = area.getHeight() / rows;
    const int total = (int) knobs.size();

    for (int i = 0; i < total; ++i)
    {
        const int row = i / cols;
        const int col = i % cols;
        const int rowCount = std::min (cols, total - row * cols);
        const int xOffset  = (cols - rowCount) * cw / 2;

        auto cell = juce::Rectangle<int> (area.getX() + xOffset + col * cw,
                                          area.getY() + row * ch, cw, ch).reduced (4);
        knobs[(size_t) i]->label.setBounds (cell.removeFromTop (20));
        knobs[(size_t) i]->slider.setBounds (cell);
    }
}

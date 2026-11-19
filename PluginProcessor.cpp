#include "PluginProcessor.h"
#include "PluginEditor.h"

ChoirAudioProcessor::ChoirAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout ChoirAudioProcessor::createLayout()
{
    using namespace juce;
    using Range = NormalisableRange<float>;
    AudioProcessorValueTreeState::ParameterLayout l;

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key",
            StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale",
            StringArray { "Major", "Minor", "Dorian", "Mixolydian" }, 0));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "midi", 1 }, "MIDI Chord Mode", false));

    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "tune", 1 },  "Tune",       Range (0.0f, 1.0f, 0.001f), 1.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "glide", 1 }, "Glide (ms)", Range (1.0f, 200.0f, 0.1f, 0.4f), 12.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "lead", 1 },  "Lead",       Range (0.0f, 1.0f, 0.001f), 1.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "spread", 1 },"Spread (cents)", Range (0.0f, 30.0f, 0.1f), 5.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "width", 1 }, "Width",      Range (0.0f, 1.0f, 0.001f), 0.7f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "out", 1 },   "Output (dB)",Range (-24.0f, 6.0f, 0.1f), -3.0f));

    const int   defIv[4] = { 2, 4, -3, -7 };
    for (int k = 0; k < 4; ++k)
    {
        l.add (std::make_unique<AudioParameterInt>   (ParameterID { "iv" + String (k + 1), 1 },
                "Voice " + String (k + 1) + " Step", -14, 14, defIv[k]));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "lv" + String (k + 1), 1 },
                "Voice " + String (k + 1) + " Level", Range (0.0f, 1.0f, 0.001f), 0.6f));
    }

    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "reverbSize", 1 }, "Hall Size", Range (0.0f, 1.0f, 0.001f), 0.8f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "reverbMix", 1 },  "Hall Mix",  Range (0.0f, 1.0f, 0.001f), 0.3f));
    return l;
}

bool ChoirAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void ChoirAudioProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    setLatencySamples (engine.latencySamples());
    heldNotes.fill (false);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) chunkSize, 2 };
    reverb.prepare (spec);
    reverb.reset();
}

void ChoirAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // --- MIDI: track which notes are held (block-accurate) ---
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            heldNotes[(size_t) msg.getNoteNumber()] = true;
        else if (msg.isNoteOff())
            heldNotes[(size_t) msg.getNoteNumber()] = false;
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            heldNotes.fill (false);
    }

    const int numIn      = getTotalNumInputChannels();
    const int numSamples = buffer.getNumSamples();

    auto val = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    HarmonizerEngine::Params p;
    p.key         = (int) val ("key");
    p.scale       = (int) val ("scale");
    p.midiMode    = val ("midi") > 0.5f;
    p.tune        = val ("tune");
    p.glideMs     = val ("glide");
    p.leadLevel   = val ("lead");
    p.spreadCents = val ("spread");
    p.width       = val ("width");
    for (int k = 0; k < 4; ++k)
    {
        p.interval[k] = (int) val (("iv" + juce::String (k + 1)).toRawUTF8());
        p.level[k]    = val (("lv" + juce::String (k + 1)).toRawUTF8());
    }

    const float outGain = juce::Decibels::decibelsToGain (val ("out"));
    const float rvMix   = val ("reverbMix");

    juce::dsp::Reverb::Parameters rp;
    rp.roomSize   = val ("reverbSize");
    rp.damping    = 0.45f;
    rp.width      = 1.0f;
    rp.freezeMode = 0.0f;
    rp.wetLevel   = 0.5f * rvMix;
    rp.dryLevel   = 0.5f * (1.0f - rvMix);
    reverb.setParameters (rp);

    const float* inL = buffer.getReadPointer (0);
    const float* inR = numIn > 1 ? buffer.getReadPointer (1) : inL;
    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getWritePointer (1);

    std::array<float, chunkSize> mono, wetL, wetR;

    for (int offset = 0; offset < numSamples; offset += chunkSize)
    {
        const int n = std::min (chunkSize, numSamples - offset);

        for (int i = 0; i < n; ++i)
            mono[(size_t) i] = 0.5f * (inL[offset + i] + inR[offset + i]);

        engine.process (mono.data(), wetL.data(), wetR.data(), n, p, heldNotes.data());

        float* ptrs[2] = { wetL.data(), wetR.data() };
        juce::AudioBuffer<float> wetBuf (ptrs, 2, n);
        juce::dsp::AudioBlock<float> block (wetBuf);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        reverb.process (ctx);

        // inL/inR may alias outL/outR, so write only after the whole chunk was read above.
        for (int i = 0; i < n; ++i)
        {
            outL[offset + i] = wetL[(size_t) i] * outGain;
            outR[offset + i] = wetR[(size_t) i] * outGain;
        }
    }
}

juce::AudioProcessorEditor* ChoirAudioProcessor::createEditor()
{
    return new ChoirAudioProcessorEditor (*this);
}

void ChoirAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ChoirAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChoirAudioProcessor();
}

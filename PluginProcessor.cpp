#include "PluginProcessor.h"
#include "PluginEditor.h"

ChoirAudioProcessor::ChoirAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pVoices     = apvts.getRawParameterValue ("voices");
    pDetune     = apvts.getRawParameterValue ("detune");
    pTiming     = apvts.getRawParameterValue ("timing");
    pVibDepth   = apvts.getRawParameterValue ("vibDepth");
    pVibRate    = apvts.getRawParameterValue ("vibRate");
    pWidth      = apvts.getRawParameterValue ("width");
    pReverbSize = apvts.getRawParameterValue ("reverbSize");
    pReverbMix  = apvts.getRawParameterValue ("reverbMix");
    pMix        = apvts.getRawParameterValue ("mix");
}

juce::AudioProcessorValueTreeState::ParameterLayout ChoirAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    l.add (std::make_unique<AudioParameterInt>   (ParameterID { "voices", 1 },    "Voices",       1, ChoirEngine::maxVoices, 16));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "detune", 1 },    "Detune (cents)",   NormalisableRange<float> (0.0f, 50.0f, 0.1f),  15.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "timing", 1 },    "Timing (ms)",      NormalisableRange<float> (0.0f, 80.0f, 0.1f),  35.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "vibDepth", 1 },  "Vibrato (cents)",  NormalisableRange<float> (0.0f, 30.0f, 0.1f),  10.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "vibRate", 1 },   "Vibrato Rate (Hz)",NormalisableRange<float> (3.0f, 7.0f, 0.01f),   5.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "width", 1 },     "Width",            NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.8f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "reverbSize", 1 },"Reverb Size",      NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.8f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "reverbMix", 1 }, "Reverb Mix",       NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 },       "Dry/Wet",          NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.85f));
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

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) chunkSize, 2 };
    reverb.prepare (spec);
    reverb.reset();
}

void ChoirAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn      = getTotalNumInputChannels();
    const int numSamples = buffer.getNumSamples();

    for (int ch = numIn; ch < getTotalNumOutputChannels(); ++ch)
        if (numIn == 0) buffer.clear (ch, 0, numSamples);

    ChoirEngine::Params p;
    p.numVoices    = (int) pVoices->load();
    p.detuneCents  = pDetune->load();
    p.timingMs     = pTiming->load();
    p.vibratoCents = pVibDepth->load();
    p.vibratoRate  = pVibRate->load();
    p.width        = pWidth->load();

    const float rvMix = pReverbMix->load();
    juce::dsp::Reverb::Parameters rp;
    rp.roomSize   = pReverbSize->load();
    rp.damping    = 0.45f;
    rp.width      = 1.0f;
    rp.freezeMode = 0.0f;
    rp.wetLevel   = 0.5f * rvMix;
    rp.dryLevel   = 0.5f * (1.0f - rvMix);
    reverb.setParameters (rp);

    const float mix = pMix->load();

    const float* inL = buffer.getReadPointer (0);
    const float* inR = numIn > 1 ? buffer.getReadPointer (1) : inL;
    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getWritePointer (1);

    std::array<float, chunkSize> dryL, dryR, mono, wetL, wetR;

    for (int offset = 0; offset < numSamples; offset += chunkSize)
    {
        const int n = std::min (chunkSize, numSamples - offset);

        for (int i = 0; i < n; ++i)
        {
            dryL[(size_t) i] = inL[offset + i];
            dryR[(size_t) i] = inR[offset + i];
            mono[(size_t) i] = 0.5f * (dryL[(size_t) i] + dryR[(size_t) i]);
        }

        engine.process (mono.data(), wetL.data(), wetR.data(), n, p);

        float* ptrs[2] = { wetL.data(), wetR.data() };
        juce::AudioBuffer<float> wetBuf (ptrs, 2, n);
        juce::dsp::AudioBlock<float> block (wetBuf);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        reverb.process (ctx);

        for (int i = 0; i < n; ++i)
        {
            outL[offset + i] = dryL[(size_t) i] * (1.0f - mix) + wetL[(size_t) i] * mix;
            outR[offset + i] = dryR[(size_t) i] * (1.0f - mix) + wetR[(size_t) i] * mix;
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

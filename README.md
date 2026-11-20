# Choir – vocal harmonizer (Bon Iver style)

Tracks your voice pitch, hard-tunes it to a key, and stacks 4 formant-preserving harmony voices
on top, then adds a bright plate reverb.

## Using it
- Put it on a **mono vocal** track as an effect. Sing (or play) a clear, steady vocal.
- Set **Key** and **Scale** to match your song. The top bar shows `sing A3 -> A3` (what it hears -> note it snaps to).
- **Tune** 1.0 = robotic hard-tune (the Bon Iver / "Woods" sound). Lower it for a natural feel.
- **Glide** = how fast pitch snaps (low = robotic).
- **V1-V4 Step** = how many scale steps each harmony sits from the lead (+2 = third, +4 = fifth, -7 = octave down).
  **V1-V4 Level** sets how loud each is.
- **MIDI chord mode**: hold notes on a MIDI keyboard and the harmonies sing exactly those notes.
  (Needs MIDI routed to the plugin; easy in Reaper, harder in some DAWs. Use Key/Scale mode otherwise.)
- **Voice Range** sets latency and tracking speed: Low voice (65 Hz+) 40 ms, Mid (100 Hz+) 26 ms, High (150 Hz+) 17 ms.
  Pick the highest range that still covers your lowest sung note. The DAW compensates the latency automatically.
- **Air** adds a high-shelf lift (5.5 kHz) for a crisper top end. **Hall Size** 0..1 = reverb time ~0.8 s .. 5 s.

## Build on GitHub (no installs)
Repo root must contain CMakeLists.txt, the .h/.cpp files and the `.github/workflows` folder.
Push -> Actions tab -> download the **Choir-VST3-Windows** artifact -> copy `Choir.vst3` to
`C:\Program Files\Common Files\VST3\`.

## Build locally (Windows)
    cmake -B build -A x64
    cmake --build build --config Release
Output: build/Choir_artefacts/Release/VST3/Choir.vst3

JUCE licence: free for open-source / under its revenue threshold; check juce.com/juce-licence.

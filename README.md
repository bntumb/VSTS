# Choir – JUCE vocal-to-choir plugin

Turns one voice into an ensemble of up to 32 virtual singers (random detune, entry timing,
vibrato, drift, pan, level, tone) plus a hall reverb.

## Build (Windows)
1. Install Visual Studio 2022 (Desktop development with C++) and CMake.
2. In this folder:
   cmake -B build -G "Visual Studio 17 2022"
   cmake --build build --config Release
3. Find `Choir.vst3` under `build/Choir_artefacts/Release/VST3/` and copy it to
   `C:\Program Files\Common Files\VST3\` (or set COPY_PLUGIN_AFTER_BUILD TRUE and run as admin).
4. Rescan plugins in your DAW. Put it on a vocal track.

The first configure downloads JUCE (needs internet). JUCE licence: free for open-source /
under its revenue threshold; check juce.com/juce-licence before selling.

## No Visual Studio? Let GitHub build it
1. Create a free GitHub account and a new repository.
2. Upload everything in this folder (including the hidden `.github` folder) to it.
3. Open the repo's **Actions** tab -> "Build Windows VST3" -> wait ~10 minutes.
4. Open the finished run and download the **Choir-VST3-Windows** artifact (a zip).
5. Unzip it and copy `Choir.vst3` to `C:\Program Files\Common Files\VST3\`, then rescan in your DAW.

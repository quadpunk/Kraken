# kr@ken

A VST3 / standalone synth that **listens to a sample, hunts down a synth patch that sounds like it,
and lets you reshape that patch by dragging the tentacles of a black & white kraken.**

## Build

### On GitHub (easiest)
1. Push this folder to a GitHub repo.
2. The workflow in `.github/workflows/build.yml` builds Windows, macOS (universal) and Linux.
3. Download the artifact from the Actions tab (`kr-at-ken-<OS>`), the plugin is in `VST3/kr@ken.vst3`.

### Locally
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```
Output: `build/Kraken_artefacts/...`  (VST3 + Standalone).
Needs CMake >= 3.22, a C++17 compiler and git (JUCE is fetched automatically).

## Use
1. Drop a sample onto the window (or **LOAD SAMPLE**). The analyzer runs in the background (~5-20 s).
2. When done, the best patch is loaded. **PLAY SAMPLE / PLAY PATCH** let you A/B at the detected root note.
3. Drag the white tips of the 8 tentacles. Each tentacle controls two parameters (x / y):
   OSC A, OSC B, LOW+DIRT, FILTER, FILTER ENV, AMP A/D, AMP S/R, DRIVE.
4. The 6 blobs ("kin") are alternative patches. **MUTATE** spawns new relatives of the current patch.
5. Double-click a tip = reset that arm. Mouse wheel = level.
All 17 parameters are normal host parameters, so they can be automated.

## How it works
`Source/SynthEngine.h` is a small subtractive synth (2 morphing oscillators + sub + noise, drive,
resonant LP filter, 2 envelopes). The analyzer (`Source/Analyzer.cpp`) resamples the sample to 22.05 kHz,
trims it, estimates pitch (YIN), then runs a (mu, lambda) evolution strategy that renders
candidate patches offline and compares log-spectrograms (512 + 2048 FFT) and the RMS envelope.

## Licence note
JUCE is AGPLv3 / commercial. If you plan to sell closed-source, you need a JUCE licence.

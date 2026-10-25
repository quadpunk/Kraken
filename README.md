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
1. Drop a sample onto the window (or **LOAD SAMPLE**). The analyzer runs in the background.
2. When done, the best patch is loaded. **PLAY SAMPLE / PLAY PATCH** let you A/B at the detected root note.
3. Eight tentacles = eight modules. Drag a white tip (or click anywhere in its tide pool) to set its two main
   parameters (x / y). The **inspector** under the kraken shows every parameter of the selected module
   (75 in total) as small sliders.
   When a sample is loaded the kraken drags a little ship labelled SAMPLE down into its jaws, and when the
   patch is ready it spits up a pile of floating wreckage with a PATCH sign.
4. The 6 blobs ("kin") are alternative patches. **MUTATE** spawns new relatives of the current patch.
5. Double-click a tip = reset that module, double-click a slider = reset that parameter. Mouse wheel = level.
All parameters are normal host parameters, so they can be automated.

## The engine (Source/SynthEngine.h)
| Arm | Contents |
|---|---|
| OSC A | morph oscillator (sine/tri/saw/pulse+PWM), coarse/fine, 1-5 voice unison, FM input from B |
| OSC B | morph oscillator, coarse/fine, self-feedback, ring-mod with A, analog drift |
| HARMONIC | 1-32 partial additive oscillator: spectral tilt, odd/even, inharmonicity (bells), formant peak, octave |
| NOISE + SUB | sub oscillator, coloured noise, tonal (pitched) noise, transient burst |
| FILTER | multimode LP/BP/HP morph, 12/24 dB, drive, key/vel tracking, filter env amount, pitch-tracking comb (plucked strings, metal), 3-formant vowel filter |
| AMP + PITCH | ADSR + hold, velocity sensitivity, pitch envelope (kicks, zaps) |
| FILT ENV + LFO | filter ADSR, LFO (sine/tri/square/S&H morph) to vibrato, filter and tremolo, with fade-in |
| FX | distortion, wavefolder, chorus, stereo delay, reverb |

## How the analysis works (Source/Analyzer.cpp)
1. Resample to 22.05 kHz, trim silence, estimate pitch (YIN); several pitch candidates (incl. octave/fifth
   variants) are tried and the one the archetypes match best wins.
2. 32 archetype patches (`Source/Presets.h`: leads, basses, plucks, keys, pads, brass, organ, bells, vowels,
   winds, drums ...) plus mutants are rendered and compared with the sample. The comparison uses a 48-band
   log spectrogram at two time resolutions plus the loudness envelope.
3. An evolution strategy (half of the offspring mutate sparse parameter subsets) refines the best seeds,
   followed by a parallel coordinate-search polish. Alternatives become the "kin" blobs.

## Known limits
* Pitch can be wrong for slow-attack, heavily detuned or inharmonic sounds (the patch then plays at a wrong root).
* Sounds that need things the engine lacks (real acoustic instruments, vocals with consonants, complex
  evolving textures) will only be approximated. Analysis uses at most the first 1.2 s of the sample.
* Analysis time depends on your CPU cores (it uses up to 8).

## Licence note
JUCE is AGPLv3 / commercial. If you plan to sell closed-source, you need a JUCE licence.

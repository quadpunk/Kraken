# Changes in this version

**Engine** - `Patch::tune` (hidden fine tune), two new FX-arm parameters `tone` (soft low-pass, 1 = open) and `low`
(low shelf, 0.5 = flat), distortion and wavefolder now run 2x oversampled (less aliasing = less metallic edge).
Fixed 11-sample FX latency. Old saved states load fine (new parameters default to neutral).

**Analyzer** - rewritten, JUCE-free core (`AnalyzerCore.cpp`) - see README. Main fixes: analysis at 44.1 kHz instead of
22.05 kHz, 25 Hz lower limit (was 50), cents no longer discarded, harmonicity / brightness / bass terms, 232 seeds.

**UI/processor** - preset browser, save, recent, export/import, WAV export, host programs.

**Measured (synthetic targets, tests/):** LSD acid_e2 8.4 -> 4.4 dB, acid_c2 4.5 -> 3.9, sub 45 Hz 2.3 -> 1.2 (and correct
pitch), lead 3.1 -> 2.9; fm/pluck about equal; pad slightly worse at reduced effort. Not listening-tested.
GUI/preset code was written without a JUCE install and has not been compiled: build via the GitHub workflow.
Tests: `tests/engine_check.cpp`, `gen_targets.cpp`, `run_new.cpp`, `eval.cpp`, `calibrate.cpp` (plain g++).

**Sample editor** - waveform with draggable start/stop handles, 4 s analysis cap, region preview with playhead, ANALYSE button; long samples are no longer analysed automatically. (UI code not compiled here, like the rest of the GUI.)

**Licensing / names** - MIT LICENSE, THIRD_PARTY_NOTICES.md, SPDX headers; patch names that echoed commercial products renamed (e.g. Ladder Bass, String Machine, Sub Boom, Spiky Keys, 8-bit Triangle, Twin Saw ...). Fixed 4 factory parameter values that were outside 0..1. SysEx (.syx) export/import (kr@ken format, not DX7).

**Compare view** - zoomed sample region and live-rendered patch drawn under each other on one time axis (level-matched). 4 s render costs about 18 ms on average (49 ms worst of the factory patches), refreshed at most every 150 ms.

**Licensing** - source MIT, compiled binaries AGPL-3.0 (BINARY_LICENSE.md); CI adds LICENSE, LICENSE-AGPL-3.0.txt (downloaded from gnu.org if not in the repo), BINARY_LICENSE.md and THIRD_PARTY_NOTICES.md to every build artifact.

**UI redesign (grayscale)** - kraken moved 16 px up (drawing itself untouched), compact preset row clear of the horns, patch info + output meter + voice indicators, sonar rings / depth ruler / corner brackets behind the kraken, numbered tide pools, titled inspector panel with scale ticks, labelled sample-editor and KIN panels. Minimum window width is now 960 px.

**Analysis round 2 (tested on a real 808-style cowbell sample)** - the previous version picked a 4x octave root (750 Hz instead
of 187.5 Hz) and produced a broadband patch (loss 2.52, 17 dB spectral distance). New: partial-tuned oscillator seeds,
measured amp-envelope seeds, modulation (LFO) detection and seeds, loss breakdown / tuning hooks (`tests/probe.cpp`,
`-DKRAKEN_TUNING_ENV`). Same sample now: loss 0.26 at default effort (root 54 / 187.5 Hz, 82 % match heuristic).
Search results vary between random seeds (e.g. 0.45-0.76 loss on a bell), so single runs of a few tenths are not significant.
Measured at low effort on synthetic targets (loss, lower is better): wobble bass 0.74 -> 0.47, lead 0.36 -> 0.27, cowbell 2.5 -> 0.26;
about equal for FM piano, strings, organ, riser, brass; pad perhaps slightly worse (0.56 -> 0.60, within noise).
Known weak: bells with inharmonic partials, noisy snares, vowels (fine harmonic structure).

**Analysis round 3 (tested on the TR-909 sample set, kept local - not part of this repository)** - noise-based seeds (filtered noise
with optional tone) for snares / hats / claps / cymbals; root-note candidates from the strongest partials (YIN finds nothing on
snares); spurious energy the target does not have (>15 dB over) is now weighted fully in the loss (a 72 Hz ghost tone in a snare
patch used to be almost free); when two pitch hypotheses are close the best two each get a full search at 70 % effort and the
lower final loss wins (about 1.4x the analysis time, but cowbell root 187 Hz vs 560 Hz no longer depends on luck);
samples shorter than 6 ms are accepted (rim shots).
Independent spectral distance (dB, lower is better, effort 0.25, single runs): kicks 3.6 / 4.6, snares 5.4-7.5 (was 12.6-14.8),
hand clap 9.4, hats 7.3-8.0, toms 5.5-9.1, crash 5.8, ride 9.0, rim 8.2, cowbell 5.0 (was 17.1 before round 2),
wobble bass 5.7 (was 10.7), lead 2.5, vowel 10.4, bell 9.8.
Known weak: organ (about 11-12 dB), vowels, bells, claps with several bursts, samples that contain more than one event.

**Analysis round 4 (100 single-shot synth sounds, kept local)** - 23 of them analysed at low effort: spectral distance 2.7-11.8 dB,
median about 7 dB (best: a filtered saw bass 2.7 dB; worst: samples whose filter and level change in several stages, 11-12 dB).
Fixes: a consistent YIN estimate now removes sub-octave root candidates (a 133 Hz sound was analysed as 66 Hz and got a
fundamental it does not have); dead tails (digital silence / below -60 dB) are cut from the target; an abrupt gate-off in the
sample is used as note-off time (one sample: loss 1.04 -> 0.79). The engine has a single ADSR per envelope, so sounds with a
stepped or multi-stage level/filter curve stay hard.

**Engine: sustain fall + filter sustain fall (79 parameters)** - `AMP_FALL` ("sus fall", amp group) and `FE_FALL` ("f sus fall",
filter-env group): while a note is held the sustain level keeps sinking (time per -20 dB from 20 s to 0.05 s, 0 = hold forever),
which also gives stepped envelopes (fast drop to a level, then a slow fade). Verified numerically (fall 0.5 = -20 dB per second).
The analyzer measures the slope of the held part and sets it; on 4 test samples this gave no measurable change in loss yet.
Parameters were inserted into their groups, so index positions after them moved: patches / `.krkbank` / `.syx` written by earlier
versions are converted automatically (`upgradeV1Params`), but DAW sessions saved with an earlier plugin build map their automation
and parameter values by index and will come out wrong - re-load the patches.

**Animation** - the head breathes slowly and the arms follow it; the tentacles sway more but their tips stay put (drag handles
unchanged); when a sample is analysed the arms reach for the ship, coil around the hull and drag it down past the kraken into
the dark water (murk and bubble trail); the patch / debris part is unchanged. (UI code is not compiled here.)

**Engine round 2 (86 parameters)** - burst envelope (`AMP_BURSTS`, `BURST_T`), second LFO (`L2_*`: rate, shape, vibrato, filter, tremolo),
inspector shows up to 16 cells (two rows of 8) for the filter-env + LFO arm. Verified numerically: three bursts 12 ms apart then a
tail; LFO2 vibrato of +-2 semitones gives 390-496 Hz. Parameters were inserted again, so patches / banks / `.syx` from earlier versions
are upgraded automatically (v1 -> v3 and v2 -> v3, checked parameter by parameter), but DAW sessions saved with an older plugin build
will map values wrongly - reload the patches.
The analyzer detects clap-like burst patterns (noise-like samples only) and seeds noise patches with them; on a 909 hand clap this did
not change the result measurably (9.2 vs 9.4 dB); the search can use LFO2 and bursts on its own. No regression on lead, wobble, snare.

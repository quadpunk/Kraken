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

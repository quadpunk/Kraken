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

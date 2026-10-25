# Kraken analyzer improvements

This version updates `Source/Analyzer.cpp` while preserving the existing synth engine and plugin architecture.

## Changes
- Replaced box averaging / linear interpolation resampling with windowed-sinc resampling.
- Added a 4096-point spectral representation to improve low-frequency and harmonic resolution.
- Increased spectrogram frame density to improve temporal sensitivity.
- Added spectral-flux loss to better reward matching attacks and moving timbres.
- Added target-envelope-based note-off estimation, with a conservative fallback for samples without a clear release.
- Changed the evolution strategy to prioritize oscillator, filter and envelope parameters early, then broaden the search toward harmonics, noise and effects.
- Updated the heuristic match score calibration to reflect the new loss weights. It remains a heuristic, not a perceptual percentage.

## Notes
- The changes intentionally do not rewrite the synth engine.
- Pitch-envelope loss is not included in this iteration; adding a robust pitch tracker should be a separate step so that noisy and inharmonic samples are not penalized incorrectly.
- The denser, multi-resolution analysis costs more CPU per candidate. Test analysis time and audio quality on a representative sample set before shipping.
- Build with the project's existing CMake/JUCE setup and run an A/B test against the original analyzer.

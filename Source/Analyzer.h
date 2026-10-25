#pragma once

#include "Common.h"
#include "SynthEngine.h"

namespace kraken
{
struct AnalysisResult
{
    bool ok = false;
    Patch best;
    std::vector<Patch> kin;   // alternative patches (index 0 = best)
    float match = 0.0f;       // 0..100
    float freq = 220.0f;
    int midiNote = 57;
};

// Background thread: sample -> pitch -> evolutionary search for a patch
// whose rendered multi-resolution spectrum, envelope and spectral motion resemble
// the sample. The analyzer estimates a likely note-off point from the target envelope.
class Analyzer : public juce::Thread
{
public:
    Analyzer() : juce::Thread ("kr@ken analyzer") {}
    ~Analyzer() override { stopThread (10000); }

    void analyse (std::vector<float> mono, double sampleRate);

    std::function<void (float)> onProgress;            // called from analysis thread
    std::function<void (AnalysisResult)> onFinished;   // called from analysis thread

private:
    void run() override;

    std::vector<float> input;
    double inputSr = 44100.0;
};

} // namespace kraken

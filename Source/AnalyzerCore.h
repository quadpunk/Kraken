// kr@ken analysis core: sample -> pitch -> evolutionary patch search.
// Deliberately free of JUCE so it can be unit-tested and benchmarked offline (see tests/).
#pragma once

#include "SynthEngine.h"

#include <functional>
#include <vector>

namespace kraken
{
struct AnalysisResult
{
    bool ok = false;
    Patch best;
    std::vector<Patch> kin;   // alternative patches (index 0 = best)
    float match = 0.0f;       // 0..100, heuristic confidence
    float freq = 220.0f;      // detected fundamental (Hz)
    int midiNote = 57;        // root note; the patch carries the remaining cents in Patch::tune
    float loss = 0.0f;        // raw loss of the best patch (lower = closer), for diagnostics
};

struct AnalysisOptions
{
    float effort = 1.0f;       // scales generations / population (1 = shipping default)
    unsigned seed = 0xBEEF;
    int maxThreads = 8;
};

AnalysisResult runAnalysis (std::vector<float> mono, double sampleRate,
                            const std::function<bool()>& shouldExit,
                            const std::function<void (float)>& progress,
                            const AnalysisOptions& options = {});

} // namespace kraken

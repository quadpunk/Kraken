// SPDX-License-Identifier: MIT
#include "Analyzer.h"

namespace kraken
{
void Analyzer::analyse (std::vector<float> mono, double sampleRate, const AnalysisOptions& options)
{
    stopThread (10000);
    opts = options;
    input = std::move (mono);
    inputSr = sampleRate;
    startThread();
}

void Analyzer::run()
{
    juce::ScopedNoDenormals noDenormals;

    auto result = runAnalysis (std::move (input), inputSr,
                               [this] { return threadShouldExit(); },
                               [this] (float p) { if (onProgress) onProgress (p); },
                               opts);
    input.clear();
    if (! threadShouldExit() && onFinished) onFinished (std::move (result));
}

} // namespace kraken

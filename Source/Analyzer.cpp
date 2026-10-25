#include "Analyzer.h"

namespace kraken
{
void Analyzer::analyse (std::vector<float> mono, double sampleRate)
{
    stopThread (10000);
    input = std::move (mono);
    inputSr = sampleRate;
    startThread();
}

void Analyzer::run()
{
    juce::ScopedNoDenormals noDenormals;

    auto result = runAnalysis (std::move (input), inputSr,
                               [this] { return threadShouldExit(); },
                               [this] (float p) { if (onProgress) onProgress (p); });
    input.clear();
    if (! threadShouldExit() && onFinished) onFinished (std::move (result));
}

} // namespace kraken

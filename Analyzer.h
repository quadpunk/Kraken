// SPDX-License-Identifier: MIT
#pragma once

#include "Common.h"
#include "SynthEngine.h"
#include "AnalyzerCore.h"

namespace kraken
{
// Background thread wrapper around runAnalysis(): sample -> pitch -> evolutionary search for a patch whose
// rendered multi-resolution spectrum, envelope, brightness, bass weight and harmonicity resemble the sample.
class Analyzer : public juce::Thread
{
public:
    Analyzer() : juce::Thread ("kr@ken analyzer") {}
    ~Analyzer() override { stopThread (10000); }

    void analyse (std::vector<float> mono, double sampleRate, const AnalysisOptions& options = {});

    std::function<void (float)> onProgress;            // called from analysis thread
    std::function<void (AnalysisResult)> onFinished;   // called from analysis thread

private:
    void run() override;

    std::vector<float> input;
    double inputSr = 44100.0;
    AnalysisOptions opts;
};

} // namespace kraken

// Runs the ORIGINAL analyzer (unchanged sources in tests/old/Source) through the JUCE shim.
#include "old/Source/Analyzer.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
using namespace kraken;
int main(int argc, char** argv)
{
    FILE* f = fopen(argv[1], "rb"); std::vector<float> x; float v; while (fread(&v,4,1,f)==1) x.push_back(v); fclose(f);
    Analyzer a; AnalysisResult res; a.onFinished = [&](AnalysisResult r){ res = r; };
    auto t0 = std::chrono::steady_clock::now();
    a.analyse(x, 44100.0);
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    FILE* g = fopen(argv[2], "w");
    fprintf(g, "%d %.6f %.6f %d %.5f %.3f\n", res.midiNote, 0.0, res.best.level, res.ok, 0.0, res.match);
    for (float p : res.best.p) fprintf(g, "%.6f ", p); fprintf(g, "\n"); fclose(g);
    printf("OLD %s: ok=%d note=%d freq=%.2f match=%.0f%% time=%.1fs\n", argv[1], res.ok, res.midiNote, res.freq, res.match, sec);
}

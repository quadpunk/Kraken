// Renders every factory patch at its root note, checks sanity, equalises loudness and prints a diversity report.
// Writes tools/factory_levels.json. Build: g++ -O2 -std=c++17 calibrate.cpp -o calibrate
#include "../Source/SynthEngine.h"
#include "../Source/FactoryPresets.h"
#include <cstdio>
#include <map>
#include <string>
using namespace kraken;
struct Feat { std::string name; float v[6]; };
static float rmsOf(const std::vector<float>& x, int a, int b){ double e=0; for(int i=a;i<b;++i) e+=(double)x[i]*x[i]; return (float)std::sqrt(e/std::max(1,b-a)); }
int main()
{
    const double sr = 44100.0; std::vector<Feat> feats; std::map<std::string,float> levels; int bad = 0;
    for (const auto& d : factoryDefs())
    {
        Patch p = factoryPatch(d); p.level = 0.5f;               // gain 1
        const float f0 = 440.0f * std::pow(2.0f, (d.root - 69) / 12.0f);
        std::vector<float> o; const int N = (int)(2.0 * sr), hold = (int)(1.2 * sr);
        renderOffline(p, sr, f0, N, hold, o);
        float pk = 0; bool nan = false; for (float v : o) { if (!std::isfinite(v)) nan = true; pk = std::max(pk, std::fabs(v)); }
        const float r = rmsOf(o, 0, (int)(1.0*sr)); const float rLate = rmsOf(o, (int)(0.8*sr), (int)(1.2*sr));
        if (nan || pk < 1e-4f) { printf("BAD %-22s nan=%d peak=%g\n", d.name, nan, pk); ++bad; levels[d.name] = 0.5f; continue; }
        // target: RMS over first second ~ -20 dBFS, but peak below 0.8 (percussive / short sounds are peak-limited)
        float gRms = 0.10f / std::max(r, 1e-6f), gPk = 0.8f / pk;
        float g = std::min(gRms, gPk);
        float lvl = std::sqrt(std::min(std::max(g / 4.0f, 0.03f), 1.0f));
        levels[d.name] = lvl;
        // diversity features: spectral centroid(log), brightness, tail ratio, attack time, flatness proxy
        float zc = 0; for (size_t i = 1; i < o.size(); ++i) if (o[i-1] < 0 && o[i] >= 0) zc++;
        int att = 0; for (; att < N; ++att) if (std::fabs(o[att]) > 0.5f * pk) break;
        Feat ft; ft.name = d.name; ft.v[0] = std::log2(zc / 2.0f + 1.0f); ft.v[1] = 10*std::log10(r/(rLate+1e-6f)+1e-3f) / 20.f; ft.v[2] = (float)att / sr * 10.f;
        // brightness: energy of first difference vs signal
        double ed = 0, es = 0; for (size_t i = 1; i < o.size(); ++i) { double dd = o[i]-o[i-1]; ed += dd*dd; es += (double)o[i]*o[i]; }
        ft.v[3] = (float)(0.5 * std::log10(ed / (es + 1e-12) + 1e-6)); ft.v[4] = std::min(1.f, r / pk) * 2.f; ft.v[5] = d.root / 40.0f;
        feats.push_back(ft);
        if (g == gPk && gPk < gRms * 0.5f) printf("note %-22s is peak-limited (crest %.1f)\n", d.name, pk / r);
    }
    // near duplicates
    int dup = 0;
    for (size_t i = 0; i < feats.size(); ++i) for (size_t j = i + 1; j < feats.size(); ++j) {
        float dd = 0; for (int k = 0; k < 6; ++k) dd += std::pow(feats[i].v[k] - feats[j].v[k], 2);
        if (std::sqrt(dd) < 0.12f) { printf("similar: %s ~ %s (%.3f)\n", feats[i].name.c_str(), feats[j].name.c_str(), std::sqrt(dd)); ++dup; } }
    FILE* f = fopen("../tools/factory_levels.json", "w"); fprintf(f, "{\n"); size_t n = 0;
    for (auto& kv : levels) fprintf(f, "  \"%s\": %.4f%s\n", kv.first.c_str(), kv.second, ++n < levels.size() ? "," : ""); fprintf(f, "}\n"); fclose(f);
    printf("patches %zu, bad %d, near-duplicates %d\n", factoryDefs().size(), bad, dup);
}

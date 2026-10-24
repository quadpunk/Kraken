// kr@ken synth engine. Header-only so the exact same code is used for
// real-time playback and for the offline renders done by the analyzer.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace kraken
{
constexpr int kNumParams = 16;
constexpr int kNumArms = 8; // each arm = 2 parameters (x / y)
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

//==============================================================================
struct Patch
{
    std::array<float, kNumParams> p {}; // all normalised 0..1
    float level = 0.7f;
};

inline Patch defaultPatch()
{
    Patch d;
    d.p = { 0.66f, 0.30f, 0.66f, 0.50f, 0.00f, 0.00f, 0.70f, 0.20f,
            0.60f, 0.50f, 0.05f, 0.50f, 0.70f, 0.30f, 0.10f, 0.20f };
    d.level = 0.7f;
    return d;
}

inline const char* paramName (int i)
{
    static const char* n[kNumParams] = {
        "OSC A shape", "A-B mix", "OSC B shape", "OSC B detune",
        "Sub", "Noise", "Cutoff", "Resonance",
        "Filter env amount", "Filter env decay", "Amp attack", "Amp decay",
        "Amp sustain", "Amp release", "Drive", "Spread" };
    return n[i];
}

inline const char* armName (int a)
{
    static const char* n[kNumArms] = { "OSC A", "OSC B", "LOW + DIRT", "FILTER",
                                       "FILTER ENV", "AMP A / D", "AMP S / R", "DRIVE" };
    return n[a];
}
inline const char* armAxisX (int a)
{
    static const char* n[kNumArms] = { "shape", "shape", "sub", "cutoff", "amount", "attack", "sustain", "drive" };
    return n[a];
}
inline const char* armAxisY (int a)
{
    static const char* n[kNumArms] = { "A>B mix", "detune", "noise", "reso", "decay", "decay", "release", "spread" };
    return n[a];
}

//==============================================================================
inline float expMap (float n, float lo, float hi) { return lo * std::pow (hi / lo, n); }

struct EnvParams
{
    float attInc = 0.01f, decCoef = 0.999f, sus = 0.7f, relCoef = 0.999f;
};

inline EnvParams makeEnv (float a, float d, float s, float r, double sr)
{
    EnvParams e;
    e.attInc = 1.0f / (std::max (a, 0.0005f) * (float) sr);
    e.decCoef = std::exp (-4.6f / (std::max (d, 0.005f) * (float) sr));
    e.sus = s;
    e.relCoef = std::exp (-4.6f / (std::max (r, 0.005f) * (float) sr));
    return e;
}

// Patch -> physical values
struct Derived
{
    float shapeA = 0, shapeB = 0, gA = 1, gB = 0, detuneRatioB = 1, sub = 0, noise = 0;
    float cutoff = 1000, res = 0, fenvOct = 0, drive = 1, driveNorm = 1, spreadRatio = 1, gain = 1;
    EnvParams amp, flt;

    Derived() = default;
    Derived (const Patch& pt, double sr)
    {
        const auto& p = pt.p;
        shapeA = p[0];
        const float m = p[1];
        gA = std::cos (m * kPi * 0.5f);
        gB = std::sin (m * kPi * 0.5f);
        shapeB = p[2];
        const float dn = (p[3] - 0.5f) * 2.0f;
        const float cents = dn * dn * dn * 1200.0f;
        detuneRatioB = std::pow (2.0f, cents / 1200.0f);
        sub = p[4];
        noise = p[5] * p[5];
        cutoff = expMap (p[6], 40.0f, 18000.0f);
        res = p[7];
        fenvOct = (p[8] * 2.0f - 1.0f) * 6.0f;
        const float fd = expMap (p[9], 0.02f, 3.0f);
        const float aA = expMap (p[10], 0.001f, 2.0f);
        const float aD = expMap (p[11], 0.01f, 3.0f);
        const float aR = expMap (p[13], 0.01f, 3.0f);
        amp = makeEnv (aA, aD, p[12], aR, sr);
        flt = makeEnv (0.001f, fd, 0.0f, fd, sr);
        drive = 1.0f + p[14] * 9.0f;
        driveNorm = 1.0f / std::tanh (drive);
        const float sp = p[15] * p[15] * 30.0f;
        spreadRatio = std::pow (2.0f, sp / 1200.0f);
        gain = pt.level * pt.level * 2.0f;
    }
};

//==============================================================================
inline float polyBlep (float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

struct Osc
{
    float phase = 0.0f;

    static float wave (int k, float p, float dt)
    {
        switch (k)
        {
            case 0: return std::sin (kTwoPi * p);
            case 1: return 2.0f * std::abs (2.0f * p - 1.0f) - 1.0f;
            case 2: return 2.0f * p - 1.0f - polyBlep (p, dt);
            default:
            {
                float p2 = p + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
                return (p < 0.5f ? 1.0f : -1.0f) + polyBlep (p, dt) - polyBlep (p2, dt);
            }
        }
    }

    // shape: 0 sine, 1/3 triangle, 2/3 saw, 1 square
    float tick (float dt, float shape)
    {
        dt = std::min (dt, 0.45f);
        const float s = std::min (std::max (shape, 0.0f), 0.9999f) * 3.0f;
        const int i = (int) s;
        const float f = s - (float) i;
        const float out = wave (i, phase, dt) * (1.0f - f) + wave (i + 1, phase, dt) * f;
        phase += dt;
        if (phase >= 1.0f) phase -= 1.0f;
        return out;
    }
};

struct SVF // TPT state variable filter, lowpass output
{
    float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0;
    void reset() { ic1 = ic2 = 0; }
    void set (float cutoff, float res, float sr)
    {
        const float g = std::tan (kPi * cutoff / sr);
        const float k = 2.0f - 1.9f * res;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    float process (float v0)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return v2;
    }
};

struct Env
{
    enum Stage { Idle, Att, Dec, Sus, Rel };
    Stage stage = Idle;
    float level = 0.0f;

    void noteOn() { stage = Att; }
    void noteOff() { if (stage != Idle) stage = Rel; }
    float tick (const EnvParams& e)
    {
        switch (stage)
        {
            case Idle: break;
            case Att: level += e.attInc; if (level >= 1.0f) { level = 1.0f; stage = Dec; } break;
            case Dec: level = e.sus + (level - e.sus) * e.decCoef;
                      if (level - e.sus < 1.0e-4f) { level = e.sus; stage = Sus; } break;
            case Sus: level = e.sus; break;
            case Rel: level *= e.relCoef; if (level < 1.0e-4f) { level = 0.0f; stage = Idle; } break;
        }
        return level;
    }
};

//==============================================================================
struct Voice
{
    int note = -1;
    uint32_t age = 0;
    float freq = 440.0f, vel = 1.0f;
    double sr = 44100.0;
    Osc a1, a2, b;
    float subPhase = 0.0f;
    SVF filter;
    Env amp, fl;
    uint32_t rng = 22222u;
    int ctr = 0;

    bool active() const { return amp.stage != Env::Idle; }

    void noteOn (int n, float v, float f, double s, uint32_t seed)
    {
        note = n; vel = v; freq = f; sr = s; rng = seed | 1u;
        a1.phase = 0.0f; a2.phase = 0.37f; b.phase = 0.0f; subPhase = 0.0f;
        filter.reset();
        fl.level = 0.0f;
        amp.noteOn(); fl.noteOn();
        ctr = 0;
    }
    void noteOff() { amp.noteOff(); fl.noteOff(); note = -1; }

    float tick (const Derived& d)
    {
        const float dt = freq / (float) sr;
        const float sA = 0.5f * (a1.tick (dt * d.spreadRatio, d.shapeA) + a2.tick (dt / d.spreadRatio, d.shapeA));
        const float sB = b.tick (dt * d.detuneRatioB, d.shapeB);
        subPhase += dt * 0.5f; if (subPhase >= 1.0f) subPhase -= 1.0f;
        const float sb = std::sin (kTwoPi * subPhase);
        rng = rng * 1664525u + 1013904223u;
        const float nz = (float) (rng >> 8) * (1.0f / 8388608.0f) - 1.0f;

        float x = (d.gA * sA + d.gB * sB) * 0.6f + d.sub * sb * 0.5f + d.noise * nz * 0.5f;
        x = std::tanh (d.drive * x) * d.driveNorm;

        const float fe = fl.tick (d.flt);
        const float ae = amp.tick (d.amp);
        if (ctr-- <= 0)
        {
            ctr = 7;
            float fc = d.cutoff * std::exp2 (d.fenvOct * fe);
            fc = std::min (std::max (fc, 20.0f), 0.45f * (float) sr);
            filter.set (fc, d.res, (float) sr);
        }
        const float y = filter.process (x);
        if (amp.stage == Env::Sus && amp.level < 1.0e-4f) amp.stage = Env::Idle;
        return y * ae * vel * d.gain;
    }
};

//==============================================================================
class Synth
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& v : voices) v = Voice();
        d = Derived (defaultPatch(), sr);
    }
    void setPatch (const Patch& p) { d = Derived (p, sr); }

    void noteOn (int note, float vel)
    {
        Voice* v = nullptr;
        for (auto& x : voices) if (! x.active()) { v = &x; break; }
        if (v == nullptr)
        {
            v = &voices[0];
            for (auto& x : voices) if (x.age < v->age) v = &x;
        }
        ++counter;
        v->noteOn (note, vel, 440.0f * std::pow (2.0f, (float) (note - 69) / 12.0f), sr, counter * 2654435761u);
        v->age = counter;
    }
    void noteOff (int note) { for (auto& v : voices) if (v.active() && v.note == note) v.noteOff(); }
    void allOff() { for (auto& v : voices) if (v.active()) v.noteOff(); }

    void render (float* out, int n)
    {
        for (auto& v : voices)
            if (v.active())
                for (int i = 0; i < n; ++i) out[i] += v.tick (d);
    }

private:
    std::array<Voice, 8> voices;
    Derived d;
    double sr = 44100.0;
    uint32_t counter = 0;
};

//==============================================================================
// Deterministic single-note render, used by the analyzer.
inline void renderOffline (const Patch& patch, double sr, float freq, int numSamples, int holdSamples,
                           std::vector<float>& out)
{
    const Derived d (patch, sr);
    Voice v;
    v.noteOn (60, 1.0f, freq, sr, 12345u);
    out.resize ((size_t) numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        if (i == holdSamples) v.noteOff();
        out[(size_t) i] = v.tick (d);
    }
}

} // namespace kraken

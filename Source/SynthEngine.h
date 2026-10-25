// SPDX-License-Identifier: MIT
// kr@ken synth engine (v2). Header-only: the exact same code renders live audio
// and the offline candidates the analyzer evaluates.
//
// Per voice:  OSC A (morph + unison + FM in) | OSC B (morph, feedback, ring) |
//             HARMONIC (additive / inharmonic / formant) | SUB | NOISE (colour, tonal, transient)
//             -> drive -> multimode 12/24 dB filter -> pitch-tracking COMB -> VOWEL formants
//             -> amp (ADSR + hold, velocity, tremolo).  Mod: filter ADSR, pitch env, LFO, drift.
// Master FX:  distortion + wavefold (2x oversampled) -> chorus -> delay -> reverb -> tone / low shelf.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace kraken
{
//==============================================================================
// Parameter indices (all normalised 0..1)
namespace P
{
enum : int
{
    // ARM 0: OSC A
    A_SHAPE, A_LEVEL, A_PW, A_COARSE, A_FINE, A_UNI, A_UNIDET, A_FM,
    // ARM 1: OSC B
    B_SHAPE, B_LEVEL, B_PW, B_COARSE, B_FINE, B_RING, B_FB, B_DRIFT,
    // ARM 2: HARMONIC (additive)
    C_TILT, C_LEVEL, C_ODDEVEN, C_COUNT, C_INHARM, C_FORM, C_FORMQ, C_OCT,
    // ARM 3: NOISE + SUB
    SUB_LEVEL, NOISE_LEVEL, NOISE_COLOR, SUB_SHAPE, NOISE_TONAL, NOISE_TRANS, NOISE_TDEC, SUB_OCT,
    // ARM 4: FILTER (+ comb, vowel)
    F_CUTOFF, F_RES, F_MODE, F_SLOPE, F_KEYTRK, F_ENVAMT, F_DRIVE, F_VEL,
    COMB_MIX, COMB_TUNE, COMB_FB, VOWEL_MIX, VOWEL_POS, VOWEL_Q,
    // ARM 5: AMP + PITCH ENV
    AMP_A, AMP_D, AMP_S, AMP_R, PITCH_AMT, PITCH_DEC, AMP_VEL, AMP_HOLD,
    // ARM 6: FILTER ENV + LFO
    FE_A, FE_D, FE_S, FE_R, LFO_RATE, LFO_VIB, LFO_FILT, LFO_TREM, LFO_SHAPE, LFO_FADE,
    // ARM 7: FX
    DIST_AMT, DIST_FOLD, CHO_MIX, CHO_RATE, CHO_DEPTH, DLY_TIME, DLY_FB, DLY_MIX, REV_SIZE, REV_DAMP, REV_MIX,
    OUT_TONE, OUT_LOW,
    COUNT
};
} // namespace P

constexpr int kNumParams = P::COUNT;
constexpr int kNumArms = 8;
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

static_assert (kNumParams == 77, "parameter table out of sync");

constexpr int kArmFirst[kNumArms] = { P::A_SHAPE, P::B_SHAPE, P::C_TILT, P::SUB_LEVEL,
                                      P::F_CUTOFF, P::AMP_A, P::FE_A, P::DIST_AMT };
constexpr int kArmCount[kNumArms] = { 8, 8, 8, 8, 14, 8, 10, 13 };

inline int armOf (int param)
{
    for (int a = kNumArms - 1; a >= 0; --a)
        if (param >= kArmFirst[a]) return a;
    return 0;
}

inline const char* armName (int a)
{
    static const char* n[kNumArms] = { "OSC A", "OSC B", "HARMONIC", "NOISE + SUB",
                                       "FILTER", "AMP + PITCH", "FILT ENV + LFO", "FX" };
    return n[a];
}

inline const char* paramShort (int i)
{
    static const char* n[kNumParams] = {
        "shape", "level", "pulse w", "coarse", "fine", "unison", "uni det", "fm",
        "shape", "level", "pulse w", "coarse", "fine", "ring", "feedback", "drift",
        "tilt", "level", "odd/even", "partials", "inharm", "formant", "form pk", "octave",
        "sub", "noise", "color", "sub shape", "tonal", "transient", "trans dec", "sub oct",
        "cutoff", "reso", "mode", "slope", "keytrack", "env amt", "drive", "vel",
        "comb", "comb tune", "comb fb", "vowel", "vowel pos", "vowel q",
        "attack", "decay", "sustain", "release", "pitch env", "pitch dec", "vel sens", "hold",
        "f attack", "f decay", "f sustain", "f release", "lfo rate", "vibrato", "lfo filter", "tremolo", "lfo shape", "lfo fade",
        "distort", "fold", "chorus", "cho rate", "cho depth", "dly time", "dly fb", "delay", "rev size", "rev damp", "reverb",
        "tone", "low" };
    return n[i];
}

//==============================================================================
struct Patch
{
    std::array<float, kNumParams> p {};
    float level = 0.7f;
    float tune = 0.0f;   // semitones (-0.5..0.5), applied on note-on; set by the analyzer so the patch is in tune at its root note
};

inline Patch defaultPatch()
{
    static const float d[kNumParams] = {
        .66f, .60f, .5f, .5f, .5f, 0.f, .3f, 0.f,                                   // A
        .66f, 0.f, .5f, .5f, .52f, 0.f, 0.f, .05f,                                  // B
        .3f, 0.f, .5f, .6f, 0.f, .4f, 0.f, .5f,                                     // C
        0.f, 0.f, .5f, 0.f, 0.f, 0.f, .2f, .3f,                                     // noise+sub
        .75f, .15f, 0.f, 0.f, .3f, .55f, 0.f, .2f, 0.f, .5f, .5f, 0.f, .3f, .4f,    // filter
        .03f, .5f, .7f, .3f, .5f, .3f, .5f, 0.f,                                    // amp
        0.f, .5f, .3f, .3f, .45f, 0.f, 0.f, 0.f, 0.f, .2f,                          // mod
        0.f, 0.f, 0.f, .3f, .4f, .5f, .3f, 0.f, .5f, .5f, 0.f,                     // fx
        1.f, .5f };                                                                 // output tone (open) / low shelf (flat)
    Patch pt;
    for (int i = 0; i < kNumParams; ++i) pt.p[(size_t) i] = d[i];
    pt.level = 0.7f;
    pt.tune = 0.0f;
    return pt;
}

//==============================================================================
inline float expMap (float n, float lo, float hi) { return lo * std::pow (hi / lo, n); }
inline float clamp01 (float v) { return std::min (1.0f, std::max (0.0f, v)); }

// fast sine, phase in cycles [0,1)
inline float fastSin (float ph)
{
    const float x = (0.5f - ph) * kTwoPi;
    const float y = 1.27323954f * x - 0.405284735f * x * std::abs (x);
    return 0.225f * (y * std::abs (y) - y) + y;
}

struct EnvParams
{
    float attInc = 0.01f, decCoef = 0.999f, sus = 0.7f, relCoef = 0.999f;
    int holdSamples = 0;
};

inline EnvParams makeEnv (float a, float d, float s, float r, double sr, int hold = 0)
{
    EnvParams e;
    e.attInc = 1.0f / (std::max (a, 0.0005f) * (float) sr);
    e.decCoef = std::exp (-4.6f / (std::max (d, 0.005f) * (float) sr));
    e.sus = s;
    e.relCoef = std::exp (-4.6f / (std::max (r, 0.005f) * (float) sr));
    e.holdSamples = hold;
    return e;
}

//==============================================================================
// Patch -> physical values
struct Derived
{
    double sr = 44100.0;
    // osc A
    float shapeA = 0, levA = 0, pwA = 0.5f, ratioA = 1, uniDet = 0, fmAmt = 0;
    int uniN = 1;
    // osc B
    float shapeB = 0, levB = 0, pwB = 0.5f, ratioB = 1, ringMix = 0, fbB = 0, drift = 0;
    // harmonic
    float levC = 0, cSlope = 1, cEven = 1, cInharm = 0, cFormHz = 1000, cFormPeak = 0, cRatio = 1;
    int cCount = 8;
    std::array<float, 7> cSig {};
    // sub / noise
    float sub = 0, subShape = 0, subRatio = 0.5f;
    float noise = 0, noiseColor = 0.5f, noiseTonal = 0, noiseTrans = 0, noiseDecCoef = 0.999f;
    // filter
    float cutoff = 1000, res = 0, mode = 0, slope = 0, keyTrk = 0, fenvOct = 0, fdrive = 0, fvel = 0;
    float combMix = 0, combRatio = 1, combFb = 0, vowelMix = 0, vowelPos = 0, vowelQ = 0;
    // envelopes / modulation
    EnvParams amp, flt;
    float pitchAmt = 0, pitchDecCoef8 = 0.9f, ampVel = 0;
    float lfoRate = 1, lfoVib = 0, lfoFilt = 0, lfoTrem = 0, lfoShape = 0, lfoFade = 0;
    // fx
    float distAmt = 0, foldAmt = 0, choMix = 0, choRate = 1, choDepth = 0;
    float dlyTime = 0.3f, dlyFb = 0, dlyMix = 0, revSize = 0.5f, revDamp = 0.5f, revMix = 0;
    float toneCoef = 1.0f, lowCoef = 0.02f, lowGain = 0.0f;   // output tone (2 x one-pole LP) and low shelf
    bool toneOn = false, lowOn = false;
    float tuneRatio = 1.0f;
    float gain = 1;

    Derived() = default;
    Derived (const Patch& pt, double sampleRate)
    {
        using namespace P;
        sr = sampleRate;
        const auto& p = pt.p;

        shapeA = p[A_SHAPE]; levA = p[A_LEVEL]; pwA = 0.05f + 0.9f * p[A_PW];
        ratioA = std::exp2 ((std::round ((p[A_COARSE] - 0.5f) * 48.0f) + (p[A_FINE] - 0.5f) * 2.0f) / 12.0f);
        uniN = 1 + (int) (p[A_UNI] * 4.999f);
        uniDet = p[A_UNIDET] * p[A_UNIDET] * 50.0f;
        fmAmt = p[A_FM] * p[A_FM] * 1.5f;

        shapeB = p[B_SHAPE]; levB = p[B_LEVEL]; pwB = 0.05f + 0.9f * p[B_PW];
        ratioB = std::exp2 ((std::round ((p[B_COARSE] - 0.5f) * 48.0f) + (p[B_FINE] - 0.5f) * 2.0f) / 12.0f);
        ringMix = p[B_RING]; fbB = p[B_FB] * 0.35f; drift = p[B_DRIFT];

        levC = p[C_LEVEL];
        cSlope = 3.0f * (1.0f - p[C_TILT]);
        cEven = p[C_ODDEVEN] < 0.5f ? 2.0f * p[C_ODDEVEN] : 1.0f + (p[C_ODDEVEN] - 0.5f) * 2.0f;
        cCount = 1 + (int) (p[C_COUNT] * 31.999f);
        cInharm = p[C_INHARM] * p[C_INHARM] * 0.01f;
        cFormHz = expMap (p[C_FORM], 200.0f, 5000.0f);
        cFormPeak = p[C_FORMQ] * 12.0f;
        cRatio = std::exp2 (std::round ((p[C_OCT] - 0.5f) * 4.0f));
        cSig = { cSlope, cEven, (float) cCount, cInharm, cFormPeak, cFormHz, cRatio };

        sub = p[SUB_LEVEL]; subShape = p[SUB_SHAPE]; subRatio = p[SUB_OCT] < 0.5f ? 0.5f : 0.25f;
        noise = p[NOISE_LEVEL] * p[NOISE_LEVEL]; noiseColor = p[NOISE_COLOR];
        noiseTonal = p[NOISE_TONAL]; noiseTrans = p[NOISE_TRANS];
        noiseDecCoef = std::exp (-4.6f / (expMap (p[NOISE_TDEC], 0.003f, 0.6f) * (float) sr));

        cutoff = expMap (p[F_CUTOFF], 25.0f, 18000.0f);
        res = p[F_RES]; mode = p[F_MODE]; slope = p[F_SLOPE]; keyTrk = p[F_KEYTRK];
        fenvOct = (p[F_ENVAMT] * 2.0f - 1.0f) * 7.0f;
        fdrive = p[F_DRIVE]; fvel = p[F_VEL];
        combMix = p[COMB_MIX]; combRatio = std::exp2 ((p[COMB_TUNE] - 0.5f) * 2.0f); combFb = p[COMB_FB] * 0.995f;
        vowelMix = p[VOWEL_MIX]; vowelPos = p[VOWEL_POS]; vowelQ = p[VOWEL_Q];

        amp = makeEnv (expMap (p[AMP_A], 0.0008f, 5.0f), expMap (p[AMP_D], 0.005f, 8.0f), p[AMP_S],
                       expMap (p[AMP_R], 0.005f, 8.0f), sr, (int) (p[AMP_HOLD] * p[AMP_HOLD] * (float) sr));
        flt = makeEnv (expMap (p[FE_A], 0.0008f, 5.0f), expMap (p[FE_D], 0.005f, 8.0f), p[FE_S],
                       expMap (p[FE_R], 0.005f, 8.0f), sr);
        const float xp = p[PITCH_AMT] * 2.0f - 1.0f;
        pitchAmt = xp * std::abs (xp) * 48.0f;
        pitchDecCoef8 = std::exp (-4.6f * 8.0f / (expMap (p[PITCH_DEC], 0.004f, 1.5f) * (float) sr));
        ampVel = p[AMP_VEL];

        lfoRate = expMap (p[LFO_RATE], 0.1f, 30.0f);
        lfoVib = p[LFO_VIB] * p[LFO_VIB] * 4.0f;
        lfoFilt = p[LFO_FILT] * 3.0f;
        lfoTrem = p[LFO_TREM];
        lfoShape = p[LFO_SHAPE];
        lfoFade = p[LFO_FADE] * p[LFO_FADE] * 3.0f;

        distAmt = p[DIST_AMT]; foldAmt = p[DIST_FOLD];
        choMix = p[CHO_MIX]; choRate = expMap (p[CHO_RATE], 0.1f, 6.0f); choDepth = p[CHO_DEPTH];
        dlyTime = expMap (p[DLY_TIME], 0.03f, 1.0f); dlyFb = p[DLY_FB] * 0.9f; dlyMix = p[DLY_MIX];
        revSize = p[REV_SIZE]; revDamp = p[REV_DAMP]; revMix = p[REV_MIX];

        const float toneHz = expMap (p[OUT_TONE], 500.0f, 20000.0f);
        toneOn = p[OUT_TONE] < 0.985f;
        toneCoef = 1.0f - std::exp (-kTwoPi * std::min (toneHz, 0.45f * (float) sr) / (float) sr);
        const float lowDb = (p[OUT_LOW] - 0.5f) * 2.0f * 9.0f;
        lowOn = std::abs (lowDb) > 0.15f;
        lowGain = std::pow (10.0f, lowDb / 20.0f) - 1.0f;
        lowCoef = 1.0f - std::exp (-kTwoPi * 150.0f / (float) sr);
        tuneRatio = std::exp2 (pt.tune / 12.0f);

        gain = pt.level * pt.level * 4.0f;
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

    static float wave (int k, float p, float dt, float pw)
    {
        switch (k)
        {
            case 0: return fastSin (p);
            case 1: return 2.0f * std::abs (2.0f * p - 1.0f) - 1.0f;
            case 2: return 2.0f * p - 1.0f - polyBlep (p, dt);
            default:
            {
                float p2 = p - pw; if (p2 < 0.0f) p2 += 1.0f;
                return (p < pw ? 1.0f : -1.0f) + polyBlep (p, dt) - polyBlep (p2, dt) - (2.0f * pw - 1.0f);
            }
        }
    }

    // shape: 0 sine, 1/3 triangle, 2/3 saw, 1 pulse(pw).  pm = phase modulation in cycles
    float tick (float dt, float shape, float pw, float pm)
    {
        dt = std::min (dt, 0.45f);
        float ph = phase + pm;
        ph -= std::floor (ph);
        const float s = std::min (std::max (shape, 0.0f), 0.9999f) * 3.0f;
        const int i = (int) s;
        const float f = s - (float) i;
        float out = wave (i, ph, dt, pw);
        if (f > 1.0e-4f) out += (wave (i + 1, ph, dt, pw) - out) * f;
        phase += dt;
        if (phase >= 1.0f) phase -= 1.0f;
        return out;
    }
};

struct SVF // TPT state variable filter
{
    float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1;
    void reset() { ic1 = ic2 = 0; }
    void setK (float cutoff, float kk, float sr)
    {
        cutoff = std::min (std::max (cutoff, 15.0f), 0.45f * sr);
        k = kk;
        const float g = std::tan (kPi * cutoff / sr);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void set (float cutoff, float res, float sr) { setK (cutoff, 2.0f - 1.95f * res, sr); }
    void process (float v0, float& lp, float& bp, float& hp)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }
};

struct Env
{
    enum Stage { Idle, Att, Hold, Dec, Sus, Rel };
    Stage stage = Idle;
    float level = 0.0f;
    int holdCount = 0;

    void noteOn (const EnvParams& e) { stage = Att; holdCount = e.holdSamples; }
    void noteOff() { if (stage != Idle) stage = Rel; }
    float tick (const EnvParams& e)
    {
        switch (stage)
        {
            case Idle: break;
            case Att: level += e.attInc;
                      if (level >= 1.0f) { level = 1.0f; stage = holdCount > 0 ? Hold : Dec; } break;
            case Hold: level = 1.0f; if (--holdCount <= 0) stage = Dec; break;
            case Dec: level = e.sus + (level - e.sus) * e.decCoef;
                      if (level - e.sus < 1.0e-4f) { level = e.sus; stage = Sus; } break;
            case Sus: level = e.sus; break;
            case Rel: level *= e.relCoef; if (level < 1.0e-4f) { level = 0.0f; stage = Idle; } break;
        }
        return level;
    }
};

inline void vowelFormants (float pos, float* f)
{
    static const float V[5][3] = { { 800, 1150, 2900 }, { 500, 1750, 2600 }, { 300, 2250, 3000 },
                                   { 450, 800, 2830 }, { 325, 700, 2700 } };
    const float s = std::min (std::max (pos, 0.0f), 0.9999f) * 4.0f;
    const int i = (int) s;
    const float fr = s - (float) i;
    for (int k = 0; k < 3; ++k) f[k] = V[i][k] + (V[i + 1][k] - V[i][k]) * fr;
}

//==============================================================================
constexpr int kMaxPartials = 32;
constexpr int kCombSize = 8192;

struct Voice
{
    int note = -1;
    uint32_t age = 0;
    float freq = 440.0f, vel = 1.0f;
    double sr = 44100.0;
    float invSr = 1.0f / 44100.0f;

    std::array<Osc, 5> uni;
    std::array<float, 5> uniMul { 1, 1, 1, 1, 1 };
    float uniNorm = 1.0f;
    Osc b;
    float bOut = 0.0f;

    std::array<float, kMaxPartials> cPhase {}, hAmp {}, hRatio {};
    int hN = 0;
    std::array<float, 7> cSigLast {};

    float subPhase = 0.0f;
    uint32_t rng = 1u;
    float nLp = 0.0f, nPrev = 0.0f, nenv = 1.0f;
    SVF nBand, f1a, f1b;
    std::array<SVF, 3> fv;
    std::array<float, 3> fvAmp { 1.0f, 0.6f, 0.3f };

    std::array<float, kCombSize> combBuf {};
    int combW = 0;
    float combLp = 0.0f, combD = 100.0f;

    Env amp, fl;
    float lfoPhase = 0, lfoSH = 0, lfoFadeG = 0, lfoAmp = 1, pe = 1;
    float driftState = 0, driftAlpha = 0.002f, driftScale = 500.0f, driftMulA = 1, driftMulB = 1;
    float pitchMul = 1.0f;
    int ctr = 0;

    bool active() const { return amp.stage != Env::Idle; }

    float rnd() { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) * (1.0f / 8388608.0f) - 1.0f; }
    float rnd01() { return 0.5f * (rnd() + 1.0f); }

    //--------------------------------------------------------------------------
    void buildPartials (const Derived& d)
    {
        cSigLast = d.cSig;
        const float f0 = freq * d.cRatio;
        const int maxH = (int) (0.45f * (float) sr / std::max (f0, 1.0f));
        const int want = std::min (std::min (d.cCount, maxH), kMaxPartials);
        int n = 0;
        float sumSq = 0.0f;
        for (int h = 1; h <= want; ++h)
        {
            const float ratio = (float) h * std::sqrt (1.0f + d.cInharm * (float) (h * h));
            const float fr = f0 * ratio;
            if (fr > 0.46f * (float) sr) break;
            float a = std::exp (-d.cSlope * std::log ((float) h));
            if ((h & 1) == 0) a *= d.cEven;
            if (d.cFormPeak > 0.001f)
            {
                const float z = std::log (fr / d.cFormHz) / 0.3f;
                a *= 1.0f + d.cFormPeak * std::exp (-0.5f * z * z);
            }
            hRatio[(size_t) n] = ratio;
            hAmp[(size_t) n] = a;
            sumSq += a * a;
            ++n;
        }
        hN = n;
        const float norm = sumSq > 1.0e-9f ? 1.0f / std::sqrt (sumSq) : 0.0f;
        for (int i = 0; i < n; ++i) hAmp[(size_t) i] *= norm;
    }

    void prepare (const Derived& d)
    {
        if (d.cSig != cSigLast) buildPartials (d);
    }

    void noteOn (int n, float v, float f, double s, uint32_t seed, const Derived& d)
    {
        note = n; vel = v; freq = f; sr = s; invSr = (float) (1.0 / s); rng = seed | 1u;
        for (auto& o : uni) o.phase = rnd01();
        b.phase = 0.0f; bOut = 0.0f;
        cPhase.fill (0.0f);
        subPhase = 0.0f;
        nLp = nPrev = 0.0f; nenv = 1.0f;
        nBand.reset(); f1a.reset(); f1b.reset();
        for (auto& x : fv) x.reset();
        combBuf.fill (0.0f); combW = 0; combLp = 0.0f;
        fl.level = 0.0f;
        amp.noteOn (d.amp);
        fl.noteOn (d.flt);
        lfoPhase = 0.0f; lfoSH = rnd(); lfoFadeG = 0.0f; lfoAmp = 1.0f; pe = 1.0f;
        driftState = 0.0f;
        driftAlpha = 100.0f / (float) sr;
        driftScale = 500.0f * std::sqrt (0.0021f / driftAlpha);
        driftMulA = driftMulB = 1.0f;
        pitchMul = 1.0f;
        ctr = 0;
        buildPartials (d);
    }
    void noteOff() { amp.noteOff(); fl.noteOff(); note = -1; }

    //--------------------------------------------------------------------------
    float lfoWave (float shape, float ph) const
    {
        const float s = std::min (std::max (shape, 0.0f), 0.9999f) * 3.0f;
        const int i = (int) s;
        const float f = s - (float) i;
        auto w = [&] (int k) -> float
        {
            switch (k)
            {
                case 0: return fastSin (ph);
                case 1: return 1.0f - 4.0f * std::abs (ph - 0.5f);
                case 2: return ph < 0.5f ? 1.0f : -1.0f;
                default: return lfoSH;
            }
        };
        return w (i) * (1.0f - f) + w (i + 1) * f;
    }

    void control (const Derived& d)
    {
        const float cdt = 8.0f * invSr;

        lfoPhase += d.lfoRate * cdt;
        if (lfoPhase >= 1.0f) { lfoPhase -= std::floor (lfoPhase); lfoSH = rnd(); }
        lfoFadeG = d.lfoFade > 0.001f ? std::min (1.0f, lfoFadeG + cdt / d.lfoFade) : 1.0f;
        const float lfo = lfoWave (d.lfoShape, lfoPhase) * lfoFadeG;
        lfoAmp = 1.0f - d.lfoTrem * 0.5f * (1.0f - lfo);

        pe *= d.pitchDecCoef8;
        driftState += driftAlpha * (rnd() - driftState);
        const float cents = driftState * d.drift * driftScale;
        driftMulA = std::exp2 (cents * 0.7f / 1200.0f);
        driftMulB = std::exp2 (-cents / 1200.0f);

        pitchMul = std::exp2 ((d.pitchAmt * pe + d.lfoVib * lfo) * (1.0f / 12.0f));
        const float f = freq * pitchMul;

        const int n = d.uniN;
        uniNorm = 1.0f / std::sqrt ((float) n);
        for (int k = 0; k < n; ++k)
        {
            const float off = n > 1 ? ((float) k / (float) (n - 1)) * 2.0f - 1.0f : 0.0f;
            uniMul[(size_t) k] = std::exp2 (off * d.uniDet / 1200.0f);
        }

        float fc = d.cutoff * std::pow (std::max (f, 1.0f) / 261.63f, d.keyTrk);
        fc *= std::exp2 (d.fenvOct * fl.level + d.lfoFilt * lfo + d.fvel * (vel - 1.0f) * 4.0f);
        f1a.set (fc, d.res, (float) sr);
        f1b.set (fc, d.res, (float) sr);

        if (d.combMix > 0.002f)
            combD = std::min (std::max ((float) sr / (std::max (f, 1.0f) * d.combRatio), 2.0f), (float) (kCombSize - 3));

        if (d.vowelMix > 0.002f)
        {
            float ff[3];
            vowelFormants (d.vowelPos, ff);
            const float kq = 1.2f - 1.1f * d.vowelQ;
            for (int i = 0; i < 3; ++i) fv[(size_t) i].setK (ff[i], kq, (float) sr);
        }

        if (d.noise > 0.002f && d.noiseTonal > 0.002f)
            nBand.setK (f, 0.15f, (float) sr);
    }

    //--------------------------------------------------------------------------
    float tick (const Derived& d)
    {
        if (ctr-- <= 0) { ctr = 7; control (d); }

        const float baseDt = freq * pitchMul * invSr;

        // --- oscillators -------------------------------------------------------
        if (d.levB > 0.0005f || d.fmAmt > 0.001f || d.ringMix > 0.001f)
            bOut = b.tick (baseDt * d.ratioB * driftMulB, d.shapeB, d.pwB, d.fbB * bOut);
        else
            bOut = 0.0f;

        float sA = 0.0f;
        if (d.levA > 0.0005f || d.ringMix > 0.001f)
        {
            const float pm = d.fmAmt * bOut;
            const float dtA = baseDt * d.ratioA * driftMulA;
            for (int k = 0; k < d.uniN; ++k)
                sA += uni[(size_t) k].tick (dtA * uniMul[(size_t) k], d.shapeA, d.pwA, pm);
            sA *= uniNorm;
        }

        float x = sA * d.levA + bOut * d.levB + sA * bOut * d.ringMix;

        if (d.levC > 0.002f && hN > 0)
        {
            const float dtC = baseDt * d.cRatio * driftMulA;
            float s = 0.0f;
            for (int i = 0; i < hN; ++i)
            {
                float& ph = cPhase[(size_t) i];
                ph += dtC * hRatio[(size_t) i];
                ph -= (float) (int) ph;
                s += hAmp[(size_t) i] * fastSin (ph);
            }
            x += s * d.levC;
        }

        if (d.sub > 0.002f)
        {
            subPhase += baseDt * d.subRatio;
            if (subPhase >= 1.0f) subPhase -= 1.0f;
            const float s = fastSin (subPhase);
            x += d.sub * (s + d.subShape * (std::tanh (4.0f * s) * 0.9f - s)) * 0.8f;
        }

        if (d.noise > 0.002f)
        {
            const float w = rnd();
            float y;
            if (d.noiseColor < 0.5f)
            {
                const float t = d.noiseColor * 2.0f;
                const float a = 0.02f + 0.98f * t * t;
                nLp += a * (w - nLp);
                y = nLp * std::sqrt ((2.0f - a) / a);
            }
            else
            {
                const float t = (d.noiseColor - 0.5f) * 2.0f;
                y = w + t * ((w - nPrev) * 0.7071f - w);
            }
            nPrev = w;
            if (d.noiseTonal > 0.002f)
            {
                float lp, bp, hp;
                nBand.process (y, lp, bp, hp);
                y += d.noiseTonal * (bp * nBand.k * 3.0f - y);
            }
            nenv *= d.noiseDecCoef;
            x += d.noise * y * ((1.0f - d.noiseTrans) + d.noiseTrans * nenv) * 0.7f;
        }

        x *= 0.4f;

        // --- filter -------------------------------------------------------------
        if (d.fdrive > 0.002f) x = std::tanh (2.5f * (1.0f + 7.0f * d.fdrive) * x) * 0.4f;

        float lp, bp, hp;
        f1a.process (x, lp, bp, hp);
        const float bpk = bp * f1a.k;
        float y = d.mode < 0.5f ? lp + (bpk - lp) * (d.mode * 2.0f) : bpk + (hp - bpk) * ((d.mode - 0.5f) * 2.0f);
        if (d.slope > 0.01f)
        {
            f1b.process (y, lp, bp, hp);
            const float bpk2 = bp * f1b.k;
            const float y2 = d.mode < 0.5f ? lp + (bpk2 - lp) * (d.mode * 2.0f) : bpk2 + (hp - bpk2) * ((d.mode - 0.5f) * 2.0f);
            y += d.slope * (y2 - y);
        }
        const float feTick = fl.tick (d.flt);
        (void) feTick;

        // --- comb (pitch tracking resonator) ---------------------------------------
        if (d.combMix > 0.002f)
        {
            float rp = (float) combW - combD;
            if (rp < 0.0f) rp += (float) kCombSize;
            const int i0 = (int) rp;
            const float fr = rp - (float) i0;
            const float a0 = combBuf[(size_t) (i0 & (kCombSize - 1))];
            const float a1 = combBuf[(size_t) ((i0 + 1) & (kCombSize - 1))];
            const float delayed = a0 + (a1 - a0) * fr;
            combLp += 0.5f * (delayed - combLp);
            const float yc = y + d.combFb * combLp;
            combBuf[(size_t) combW] = yc;
            combW = (combW + 1) & (kCombSize - 1);
            y += d.combMix * (yc * (1.0f - 0.6f * d.combFb) - y);
        }

        // --- vowel formants -----------------------------------------------------------
        if (d.vowelMix > 0.002f)
        {
            float o = 0.0f;
            for (int i = 0; i < 3; ++i)
            {
                fv[(size_t) i].process (y, lp, bp, hp);
                o += fvAmp[(size_t) i] * bp * fv[(size_t) i].k;
            }
            y += d.vowelMix * (o * 3.0f - y);
        }

        // --- amp ----------------------------------------------------------------------
        const float ae = amp.tick (d.amp);
        if (amp.stage == Env::Sus && amp.level < 1.0e-4f) amp.stage = Env::Idle;
        const float velGain = 1.0f - d.ampVel * (1.0f - vel);
        y *= ae * velGain * lfoAmp * d.gain;
        return std::min (std::max (y, -4.0f), 4.0f);
    }
};

//==============================================================================
// FX
struct DelayLine
{
    std::vector<float> buf;
    int mask = 0, w = 0;
    void init (int minSize)
    {
        int n = 16;
        while (n < minSize) n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1; w = 0;
    }
    void write (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    float read (float delay) const
    {
        float rp = (float) w - delay;
        while (rp < 0.0f) rp += (float) (mask + 1);
        const int i0 = (int) rp;
        const float fr = rp - (float) i0;
        const float a = buf[(size_t) (i0 & mask)], b = buf[(size_t) ((i0 + 1) & mask)];
        return a + (b - a) * fr;
    }
};

struct RevComb
{
    std::vector<float> b; int i = 0; float store = 0.0f;
    void init (int n) { b.assign ((size_t) n, 0.0f); i = 0; store = 0.0f; }
    float process (float in, float fb, float damp)
    {
        const float o = b[(size_t) i];
        store = o * (1.0f - damp) + store * damp;
        b[(size_t) i] = in + store * fb;
        if (++i >= (int) b.size()) i = 0;
        return o;
    }
};

struct RevAllpass
{
    std::vector<float> b; int i = 0;
    void init (int n) { b.assign ((size_t) n, 0.0f); i = 0; }
    float process (float in)
    {
        const float o = b[(size_t) i];
        const float out = -in + o;
        b[(size_t) i] = in + o * 0.5f;
        if (++i >= (int) b.size()) i = 0;
        return out;
    }
};

// 2x oversampler (23-tap Blackman-windowed halfband FIR). Constant latency of kFxLatency samples
// at the base rate. Used so that saturation / wavefolding does not alias into harsh, metallic overtones.
constexpr int kFxLatency = 11;

struct Oversampler2x
{
    float h[6] {};
    std::array<float, 16> xin {};
    std::array<float, 32> vout {};
    int wi = 0, wv = 0;

    Oversampler2x()
    {
        for (int i = 0; i < 6; ++i)
        {
            const int k = 2 * i + 1;
            const float ideal = (i % 2 == 0 ? 1.0f : -1.0f) / (kPi * (float) k);
            const float a = kPi * (float) k / 12.0f;
            const float win = 0.42f + 0.5f * std::cos (a) + 0.08f * std::cos (2.0f * a);
            h[i] = ideal * win;
        }
    }
    void reset() { xin.fill (0.0f); vout.fill (0.0f); wi = wv = 0; }

    // push one base-rate sample, get two 2x-rate samples (time ordered: e then o)
    void up (float x, float& e, float& o)
    {
        xin[(size_t) (wi & 15)] = x;
        auto at = [&] (int back) { return xin[(size_t) ((wi - back) & 15)]; };   // x[n'-back]
        e = at (6);
        float s = 0.0f;
        for (int i = 0; i < 6; ++i) s += h[i] * (at (6 + i) + at (5 - i));
        o = 2.0f * s;
        ++wi;
    }
    // push two 2x-rate samples, get one base-rate sample
    float down (float e, float o)
    {
        vout[(size_t) (wv & 31)] = e; ++wv;
        vout[(size_t) (wv & 31)] = o; ++wv;
        auto get = [&] (int k) { return vout[(size_t) ((wv - 1 - k) & 31)]; };
        float s = 0.5f * get (11);
        for (int i = 0; i < 6; ++i) s += h[i] * (get (10 - 2 * i) + get (12 + 2 * i));
        return s;
    }
};

struct FxChannel
{
    int ch = 0;
    double sr = 44100.0;
    float distAmt = 0, foldAmt = 0, choMix = 0, choRate = 1, choDepthMs = 0;
    float dlyTarget = 10000, dlyFb = 0, dlyMix = 0, revFb = 0.8f, revDamp = 0.3f, revMix = 0;
    DelayLine chorusLine, delayLine;
    float choPhase = 0, dlyTimeSm = -1.0f, dlyLp = 0;
    std::array<RevComb, 8> combs;
    std::array<RevAllpass, 4> aps;
    Oversampler2x os;
    float toneCoef = 1.0f, lowCoef = 0.02f, lowGain = 0.0f, tone1 = 0.0f, tone2 = 0.0f, lowLp = 0.0f;
    bool toneOn = false, lowOn = false;

    void prepare (double s, int channel)
    {
        sr = s; ch = channel;
        os.reset(); tone1 = tone2 = lowLp = 0.0f;
        choPhase = 0.25f * (float) ch;
        dlyLp = 0.0f; dlyTimeSm = -1.0f;
        chorusLine.init ((int) (0.06 * sr) + 8);
        delayLine.init ((int) (1.3 * sr) + 8);
        static const int ct[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const int at[4] = { 556, 441, 341, 225 };
        const double sc = sr / 44100.0;
        const int spread = ch * 23;
        for (int i = 0; i < 8; ++i) combs[(size_t) i].init (std::max (8, (int) ((ct[i] + spread) * sc)));
        for (int i = 0; i < 4; ++i) aps[(size_t) i].init (std::max (8, (int) ((at[i] + spread) * sc)));
    }

    void setParams (const Derived& d)
    {
        distAmt = d.distAmt; foldAmt = d.foldAmt;
        choMix = d.choMix; choRate = d.choRate; choDepthMs = 6.0f * d.choDepth;
        dlyTarget = d.dlyTime * (ch == 0 ? 1.0f : 1.12f) * (float) sr;
        dlyFb = d.dlyFb; dlyMix = d.dlyMix;
        revFb = 0.7f + 0.28f * d.revSize;
        revDamp = 0.05f + 0.85f * d.revDamp;
        revMix = d.revMix;
        toneCoef = d.toneCoef; toneOn = d.toneOn; lowCoef = d.lowCoef; lowGain = d.lowGain; lowOn = d.lowOn;
        if (dlyTimeSm < 0.0f) dlyTimeSm = dlyTarget;
    }

    float process (float x)
    {
        {
            // saturation + wavefolder run at 2x the sample rate (always through the oversampler so that the
            // latency never changes when the effect is switched on or off)
            const bool doDist = distAmt > 0.002f, doFold = foldAmt > 0.002f;
            const float drv = 1.0f + distAmt * 30.0f;
            const float wet = std::min (1.0f, distAmt * 4.0f);
            const float g = 3.0f * (1.0f + foldAmt * 6.0f);
            float e, o;
            os.up (x, e, o);
            if (doDist)
            {
                e += wet * (std::tanh (3.0f * drv * e) * (1.0f / 3.0f) - e);
                o += wet * (std::tanh (3.0f * drv * o) * (1.0f / 3.0f) - o);
            }
            if (doFold)
            {
                e += foldAmt * (std::sin (g * e) * (1.0f / 3.0f) - e);
                o += foldAmt * (std::sin (g * o) * (1.0f / 3.0f) - o);
            }
            x = os.down (e, o);
        }
        if (choMix > 0.002f)
        {
            chorusLine.write (x);
            choPhase += choRate / (float) sr;
            if (choPhase >= 1.0f) choPhase -= 1.0f;
            const float dms = 14.0f + choDepthMs * fastSin (choPhase);
            const float wet = chorusLine.read (dms * 0.001f * (float) sr);
            x = x * (1.0f - 0.5f * choMix) + wet * 0.7f * choMix;
        }
        if (dlyMix > 0.002f)
        {
            dlyTimeSm += 0.002f * (dlyTarget - dlyTimeSm);
            const float d = delayLine.read (std::max (1.0f, dlyTimeSm));
            dlyLp += 0.45f * (d - dlyLp);
            delayLine.write (x + dlyFb * dlyLp);
            x += dlyMix * d;
        }
        if (revMix > 0.002f)
        {
            const float in = x * 0.015f;
            float s = 0.0f;
            for (auto& c : combs) s += c.process (in, revFb, revDamp);
            for (auto& a : aps) s = a.process (s);
            x = x * (1.0f - 0.4f * revMix) + s * 3.0f * revMix;
        }
        if (lowOn)
        {
            lowLp += lowCoef * (x - lowLp);
            x += lowGain * lowLp;
        }
        if (toneOn)
        {
            tone1 += toneCoef * (x - tone1);
            tone2 += toneCoef * (tone1 - tone2);
            x = tone2;
        }
        return x;
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
        fx[0].prepare (sr, 0);
        fx[1].prepare (sr, 1);
        setPatch (defaultPatch());
    }

    void setPatch (const Patch& p)
    {
        d = Derived (p, sr);
        fx[0].setParams (d);
        fx[1].setParams (d);
    }

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
        v->noteOn (note, vel, 440.0f * std::pow (2.0f, (float) (note - 69) / 12.0f) * d.tuneRatio, sr, counter * 2654435761u, d);
        v->age = counter;
    }
    void noteOff (int note) { for (auto& v : voices) if (v.active() && v.note == note) v.noteOff(); }
    void allOff() { for (auto& v : voices) if (v.active()) v.noteOff(); }

    // Overwrites l[0..n) and r[0..n) (r may be null for mono)
    void render (float* l, float* r, int n)
    {
        int done = 0;
        while (done < n)
        {
            const int c = std::min (256, n - done);
            float bus[256];
            for (int i = 0; i < c; ++i) bus[i] = 0.0f;
            for (auto& v : voices)
            {
                if (! v.active()) continue;
                v.prepare (d);
                for (int i = 0; i < c; ++i) bus[i] += v.tick (d);
            }
            for (int i = 0; i < c; ++i)
            {
                l[done + i] = fx[0].process (bus[i]);
                if (r != nullptr) r[done + i] = fx[1].process (bus[i]);
            }
            done += c;
        }
    }

private:
    std::array<Voice, 8> voices;
    std::array<FxChannel, 2> fx;
    Derived d;
    double sr = 44100.0;
    uint32_t counter = 0;
};

//==============================================================================
// Deterministic single-note render (voice + FX), used by the analyzer.
inline void renderOffline (const Patch& patch, double sr, float freq, int numSamples, int holdSamples,
                           std::vector<float>& out)
{
    thread_local Voice v;
    thread_local FxChannel fx;
    v = Voice();
    const Derived d (patch, sr);
    fx.prepare (sr, 0);
    fx.setParams (d);
    v.noteOn (60, 1.0f, freq * d.tuneRatio, sr, 12345u, d);
    out.resize ((size_t) numSamples);
    // the FX chain has a fixed latency (oversampler); skip it so the render lines up with the voice
    for (int t = 0; t < numSamples + kFxLatency; ++t)
    {
        if (t == holdSamples) v.noteOff();
        const float y = fx.process (v.tick (d));
        if (t >= kFxLatency) out[(size_t) (t - kFxLatency)] = y;
    }
}

} // namespace kraken

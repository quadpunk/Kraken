// SPDX-License-Identifier: MIT
// Archetype patches used as starting points by the analyzer.
// Values are normalised 0..1; unspecified parameters keep their defaults.
#pragma once

#include "SynthEngine.h"

namespace kraken
{
struct Set { int idx; float v; };

inline Patch mk (std::initializer_list<Set> s)
{
    Patch p = defaultPatch();
    for (const auto& x : s) p.p[(size_t) x.idx] = x.v;
    return p;
}

inline std::vector<Patch> archetypes()
{
    using namespace P;
    std::vector<Patch> v;
    v.push_back (defaultPatch());

    // --- leads ---
    v.push_back (mk ({ {A_SHAPE,.67f},{A_LEVEL,.8f},{A_UNI,.5f},{A_UNIDET,.4f},{F_CUTOFF,.75f},{F_RES,.25f},{F_ENVAMT,.65f},
                       {FE_D,.5f},{FE_S,.4f},{AMP_A,.02f},{AMP_S,.8f},{LFO_VIB,.2f},{LFO_RATE,.6f},{LFO_FADE,.5f},
                       {DLY_MIX,.2f},{DLY_FB,.3f},{REV_MIX,.15f} }));
    v.push_back (mk ({ {A_SHAPE,1.f},{A_PW,.5f},{A_LEVEL,.8f},{F_CUTOFF,.85f},{F_RES,.1f},{AMP_A,.01f},{AMP_S,.9f},{AMP_R,.15f} }));
    v.push_back (mk ({ {A_SHAPE,.67f},{A_LEVEL,.7f},{A_FM,.5f},{B_SHAPE,0.f},{B_COARSE,.65f},{B_LEVEL,0.f},{DIST_FOLD,.5f},
                       {F_CUTOFF,.85f},{AMP_S,.9f},{AMP_R,.2f} }));
    // --- basses ---
    v.push_back (mk ({ {A_SHAPE,.05f},{A_LEVEL,.8f},{SUB_LEVEL,.5f},{F_CUTOFF,.45f},{AMP_A,.01f},{AMP_S,.9f},{AMP_R,.15f} }));
    v.push_back (mk ({ {A_SHAPE,.67f},{A_LEVEL,.8f},{B_SHAPE,1.f},{B_LEVEL,.4f},{B_COARSE,.25f},{SUB_LEVEL,.3f},{F_CUTOFF,.5f},
                       {F_RES,.35f},{F_ENVAMT,.7f},{FE_D,.35f},{FE_S,.1f},{AMP_S,.8f},{AMP_R,.1f} }));
    v.push_back (mk ({ {A_SHAPE,0.f},{A_LEVEL,.8f},{B_SHAPE,0.f},{B_LEVEL,0.f},{A_FM,.35f},{AMP_D,.45f},{AMP_S,.5f},{AMP_R,.15f} }));
    v.push_back (mk ({ {A_SHAPE,.67f},{A_LEVEL,.8f},{B_SHAPE,1.f},{B_LEVEL,.5f},{B_COARSE,.25f},{F_CUTOFF,.4f},{F_RES,.6f},
                       {F_SLOPE,1.f},{LFO_RATE,.45f},{LFO_FILT,.65f},{AMP_S,.9f},{AMP_R,.1f},{DIST_AMT,.3f} }));
    v.push_back (mk ({ {A_SHAPE,.67f},{A_LEVEL,.8f},{F_CUTOFF,.35f},{F_RES,.8f},{F_SLOPE,1.f},{F_ENVAMT,.85f},{FE_D,.45f},
                       {FE_S,0.f},{AMP_D,.5f},{AMP_S,.8f},{AMP_R,.1f} }));
    // --- plucks / keys ---
    v.push_back (mk ({ {A_LEVEL,.3f},{NOISE_LEVEL,.8f},{NOISE_TRANS,1.f},{NOISE_TDEC,.2f},{COMB_MIX,1.f},{COMB_FB,.95f},
                       {F_CUTOFF,.75f},{AMP_A,0.f},{AMP_S,.9f},{AMP_R,.4f} }));
    v.push_back (mk ({ {A_SHAPE,.67f},{A_LEVEL,.8f},{B_SHAPE,.67f},{B_LEVEL,.4f},{B_FINE,.55f},{F_CUTOFF,.45f},{F_ENVAMT,.8f},
                       {FE_D,.4f},{FE_S,0.f},{AMP_A,0.f},{AMP_D,.5f},{AMP_S,0.f},{AMP_R,.3f} }));
    v.push_back (mk ({ {A_SHAPE,0.f},{A_LEVEL,.8f},{B_SHAPE,0.f},{B_LEVEL,0.f},{B_COARSE,.75f},{A_FM,.3f},{AMP_A,0.f},{AMP_D,.75f},
                       {AMP_S,0.f},{AMP_R,.6f},{CHO_MIX,.3f} }));
    v.push_back (mk ({ {A_SHAPE,0.f},{A_LEVEL,.8f},{B_SHAPE,0.f},{B_LEVEL,0.f},{B_COARSE,.92f},{A_FM,.22f},{AMP_A,0.f},{AMP_D,.65f},
                       {AMP_S,.1f},{CHO_MIX,.4f},{CHO_RATE,.3f} }));
    // --- pads / strings / brass ---
    v.push_back (mk ({ {A_SHAPE,.67f},{A_LEVEL,.6f},{A_UNI,.75f},{A_UNIDET,.5f},{B_SHAPE,.33f},{B_LEVEL,.5f},{B_FINE,.52f},
                       {F_CUTOFF,.55f},{F_RES,.1f},{FE_A,.6f},{FE_D,.7f},{FE_S,.5f},{AMP_A,.75f},{AMP_D,.6f},{AMP_S,.8f},
                       {AMP_R,.75f},{CHO_MIX,.5f},{CHO_DEPTH,.5f},{REV_MIX,.4f},{REV_SIZE,.75f} }));
    v.push_back (mk ({ {A_SHAPE,.67f},{A_LEVEL,.7f},{A_UNI,.9f},{A_UNIDET,.5f},{F_CUTOFF,.65f},{AMP_A,.6f},{AMP_S,.9f},{AMP_R,.6f},
                       {LFO_VIB,.1f},{LFO_RATE,.55f},{LFO_FADE,.6f},{CHO_MIX,.6f},{CHO_DEPTH,.6f},{REV_MIX,.3f} }));
    v.push_back (mk ({ {A_SHAPE,.67f},{A_LEVEL,.8f},{A_UNI,.3f},{B_SHAPE,.67f},{B_LEVEL,.5f},{B_FINE,.52f},{F_CUTOFF,.45f},
                       {F_RES,.1f},{F_ENVAMT,.7f},{FE_A,.45f},{FE_D,.55f},{FE_S,.5f},{AMP_A,.45f},{AMP_S,.85f},{AMP_R,.25f} }));
    v.push_back (mk ({ {A_SHAPE,1.f},{A_PW,.35f},{A_LEVEL,.7f},{B_SHAPE,1.f},{B_PW,.65f},{B_LEVEL,.7f},{B_FINE,.52f},
                       {F_CUTOFF,.6f},{AMP_A,.6f},{AMP_S,.8f},{AMP_R,.6f},{CHO_MIX,.5f} }));
    v.push_back (mk ({ {A_SHAPE,0.f},{A_LEVEL,.8f},{B_SHAPE,0.f},{B_COARSE,.75f},{B_LEVEL,.3f},{AMP_A,.7f},{AMP_S,.9f},
                       {AMP_R,.7f},{REV_MIX,.5f} }));
    // --- organ / bell / metallic ---
    v.push_back (mk ({ {A_LEVEL,0.f},{C_LEVEL,.8f},{C_TILT,.45f},{C_COUNT,.5f},{AMP_A,.02f},{AMP_S,1.f},{AMP_R,.1f},
                       {LFO_TREM,.2f},{LFO_RATE,.7f},{CHO_MIX,.4f} }));
    v.push_back (mk ({ {A_LEVEL,0.f},{C_LEVEL,.8f},{C_TILT,.6f},{C_COUNT,.4f},{C_INHARM,.45f},{AMP_A,0.f},{AMP_D,.8f},
                       {AMP_S,0.f},{AMP_R,.8f},{REV_MIX,.35f},{REV_SIZE,.6f} }));
    v.push_back (mk ({ {A_SHAPE,0.f},{A_LEVEL,.5f},{B_SHAPE,0.f},{B_COARSE,.55f},{B_FINE,.6f},{B_LEVEL,0.f},{B_RING,.9f},
                       {AMP_D,.6f},{AMP_S,.1f} }));
    // --- voices / winds ---
    v.push_back (mk ({ {A_SHAPE,.67f},{A_LEVEL,.8f},{A_UNI,.4f},{VOWEL_MIX,1.f},{VOWEL_POS,.1f},{VOWEL_Q,.55f},{F_CUTOFF,.85f},
                       {AMP_A,.6f},{AMP_S,.9f},{AMP_R,.6f},{CHO_MIX,.4f},{REV_MIX,.4f},{LFO_VIB,.12f} }));
    v.push_back (mk ({ {A_SHAPE,.67f},{A_LEVEL,.8f},{A_UNI,.4f},{VOWEL_MIX,1.f},{VOWEL_POS,.6f},{VOWEL_Q,.55f},{F_CUTOFF,.85f},
                       {AMP_A,.6f},{AMP_S,.9f},{AMP_R,.6f},{CHO_MIX,.4f},{REV_MIX,.4f},{LFO_VIB,.12f} }));
    v.push_back (mk ({ {A_LEVEL,0.f},{C_LEVEL,.9f},{C_TILT,.4f},{C_FORM,.5f},{C_FORMQ,.8f},{AMP_A,.3f},{AMP_S,.9f} }));
    v.push_back (mk ({ {A_SHAPE,.15f},{A_LEVEL,.7f},{NOISE_LEVEL,.5f},{NOISE_TONAL,.7f},{F_CUTOFF,.6f},{AMP_A,.45f},{AMP_S,.9f},
                       {AMP_R,.3f},{LFO_VIB,.15f},{LFO_RATE,.6f},{LFO_FADE,.5f} }));
    v.push_back (mk ({ {A_SHAPE,1.f},{A_LEVEL,.8f},{F_CUTOFF,.55f},{F_RES,.1f},{AMP_A,.3f},{AMP_S,.9f},{AMP_R,.25f},{LFO_VIB,.1f} }));
    v.push_back (mk ({ {A_LEVEL,0.f},{NOISE_LEVEL,.8f},{NOISE_COLOR,.4f},{NOISE_TONAL,.4f},{F_CUTOFF,.55f},{F_RES,.4f},{F_MODE,.5f},
                       {FE_A,.8f},{AMP_A,.8f},{AMP_S,.9f},{AMP_R,.7f},{REV_MIX,.5f} }));
    // --- drums / percussion ---
    v.push_back (mk ({ {A_SHAPE,0.f},{A_LEVEL,.9f},{PITCH_AMT,.88f},{PITCH_DEC,.45f},{AMP_A,0.f},{AMP_D,.45f},{AMP_S,0.f},{AMP_R,.2f},
                       {NOISE_LEVEL,.5f},{NOISE_TRANS,1.f},{NOISE_TDEC,.1f},{F_CUTOFF,.6f},{DIST_AMT,.3f} }));
    v.push_back (mk ({ {A_SHAPE,.1f},{A_LEVEL,.8f},{PITCH_AMT,.78f},{PITCH_DEC,.55f},{AMP_A,0.f},{AMP_D,.55f},{AMP_S,0.f} }));
    v.push_back (mk ({ {A_SHAPE,.33f},{A_LEVEL,.4f},{NOISE_LEVEL,.85f},{NOISE_COLOR,.55f},{PITCH_AMT,.7f},{F_CUTOFF,.75f},
                       {AMP_A,0.f},{AMP_D,.5f},{AMP_S,0.f},{AMP_R,.3f},{REV_MIX,.1f} }));
    v.push_back (mk ({ {A_SHAPE,1.f},{A_LEVEL,.3f},{B_SHAPE,1.f},{B_LEVEL,.3f},{B_COARSE,.62f},{B_RING,.8f},{NOISE_LEVEL,.8f},
                       {NOISE_COLOR,1.f},{F_MODE,1.f},{F_CUTOFF,.8f},{AMP_A,0.f},{AMP_D,.3f},{AMP_S,0.f},{AMP_R,.2f} }));
    // --- guitar-ish ---
    v.push_back (mk ({ {A_SHAPE,.67f},{A_LEVEL,.7f},{A_UNI,.2f},{B_SHAPE,1.f},{B_LEVEL,.4f},{COMB_MIX,.5f},{COMB_FB,.9f},
                       {F_CUTOFF,.6f},{DIST_AMT,.7f},{AMP_A,0.f},{AMP_D,.75f},{AMP_S,.5f},{AMP_R,.25f},{REV_MIX,.15f} }));
    return v;
}

} // namespace kraken

#!/usr/bin/env python3
"""SPDX-License-Identifier: MIT

Generates Source/FactoryPresets.h (the 200 factory patches of kr@ken).

Patches are written in natural units (Hz, seconds, semitones, cents) and converted to the engine's
normalised 0..1 parameters, so they stay readable and easy to tweak.  After editing, run:

    python3 tools/gen_factory_presets.py            # writes Source/FactoryPresets.h
    tests/calibrate  (optional)                     # renders every patch, equalises loudness -> tools/factory_levels.json
    python3 tools/gen_factory_presets.py            # re-run to bake the calibrated levels in
"""
import json, math, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.join(HERE, '..', 'Source', 'SynthEngine.h')
OUT = os.path.join(HERE, '..', 'Source', 'FactoryPresets.h')
LEVELS = os.path.join(HERE, 'factory_levels.json')

# ---- parameter names straight from the engine ------------------------------------------------
src = open(ENGINE).read()
enum = re.search(r'enum : int\s*\{(.*?)\n\s*COUNT', src, re.S).group(1)
enum = re.sub(r'//.*', '', enum)
NAMES = [n.strip() for n in enum.split(',') if n.strip()]
assert len(NAMES) == 86, len(NAMES)

# ---- unit helpers ---------------------------------------------------------------------------
def _lg(v, lo, hi): return min(1.0, max(0.0, math.log(v / lo) / math.log(hi / lo)))
def cut(hz): return _lg(hz, 25.0, 18000.0)
def ea(s):   return _lg(s, 0.0008, 5.0)       # attack time
def ed(s):   return _lg(s, 0.005, 8.0)        # decay / release time
def pen(st):                                    # pitch envelope amount (start offset in semitones)
    x = math.copysign(math.sqrt(abs(st) / 48.0), st); return 0.5 + 0.5 * x
def pdec(s): return _lg(s, 0.004, 1.5)
def fo(octv): return 0.5 + octv / 14.0          # filter-env amount in octaves
def lfo(hz): return _lg(hz, 0.1, 30.0)
def dly(s):  return _lg(s, 0.03, 1.0)
def co(st):  return 0.5 + st / 48.0             # coarse tune in semitones
def fine(c): return 0.5 + c / 200.0
def uni(n):  return (n - 1) / 5.0 + 0.02
def udet(c): return math.sqrt(min(c, 50.0) / 50.0)
def vib(st): return math.sqrt(st / 4.0)
def hold(s): return math.sqrt(s)
def fdec(s): return ed(s)
SIN, TRI, SAW, PUL = 0.0, 0.33, 0.67, 1.0
LP, BP, HP = 0.0, 0.5, 1.0

BASE = dict(
    A_SHAPE=SAW, A_LEVEL=.7, A_PW=.5, A_COARSE=.5, A_FINE=.5, A_UNI=0, A_UNIDET=.3, A_FM=0,
    B_SHAPE=SAW, B_LEVEL=0, B_PW=.5, B_COARSE=.5, B_FINE=.52, B_RING=0, B_FB=0, B_DRIFT=.05,
    C_LEVEL=0, SUB_LEVEL=0, NOISE_LEVEL=0,
    F_CUTOFF=cut(8000), F_RES=.1, F_MODE=LP, F_SLOPE=0, F_KEYTRK=.3, F_ENVAMT=.5, F_DRIVE=0, F_VEL=.2,
    COMB_MIX=0, VOWEL_MIX=0,
    AMP_A=ea(.005), AMP_D=ed(.5), AMP_S=.8, AMP_R=ed(.25), PITCH_AMT=.5, AMP_VEL=.5, AMP_HOLD=0,
    FE_A=0, FE_D=ed(.4), FE_S=.3, FE_R=ed(.3),
    LFO_VIB=0, LFO_FILT=0, LFO_TREM=0, LFO_FADE=.2,
    DIST_AMT=0, DIST_FOLD=0, CHO_MIX=0, DLY_MIX=0, REV_MIX=0, OUT_TONE=1.0, OUT_LOW=.5)

PATCHES = []
def add(cat, name, root, **kw):
    for k in kw: assert k in NAMES, (name, k)
    d = dict(BASE); d.update(kw)
    PATCHES.append((cat, name, root, d))

# =============================================================================================
# ACID -- saw or pulse, 24 dB resonant LP, big filter envelope, short gate
# =============================================================================================
def acid(name, base, octv, fd, res, shape=SAW, slope=1.0, ad=.6, **kw):
    d = dict(A_SHAPE=shape, A_LEVEL=.8, F_CUTOFF=cut(base), F_RES=res, F_SLOPE=slope, F_ENVAMT=fo(octv),
             FE_D=fdec(fd), FE_S=0, AMP_A=ea(.003), AMP_D=ed(ad), AMP_S=.7, AMP_R=ed(.08), F_KEYTRK=.25, F_VEL=.5, AMP_VEL=.6)
    d.update(kw); add('Acid', name, 36, **d)
acid('Acid Classic', 220, 3.5, .30, .78)
acid('Acid Squelch', 170, 4.2, .22, .90)
acid('Acid Rubber', 300, 3.0, .45, .70, ad=1.0, SUB_LEVEL=.25)
acid('Acid Resonant', 140, 4.5, .26, .96)
acid('Acid Pulse', 240, 3.4, .28, .80, shape=PUL, A_PW=.35)
acid('Acid Drive', 200, 3.6, .30, .82, F_DRIVE=.45, DIST_AMT=.18)
acid('Acid Soft Roll', 260, 2.6, .55, .62, slope=.6, ad=1.2, SUB_LEVEL=.3, OUT_TONE=.8)
acid('Acid Long Slide', 150, 3.8, .70, .85, ad=1.8, AMP_S=.85, AMP_R=ed(.3))
acid('Acid Dark', 110, 3.0, .35, .84, SUB_LEVEL=.4, OUT_TONE=.62)
acid('Acid Bright', 420, 3.8, .20, .72)
acid('Acid Stab', 330, 3.2, .12, .75, ad=.25, AMP_S=0)
acid('Acid Wobble', 180, 2.8, .40, .80, LFO_FILT=.35, LFO_RATE=lfo(5.5), LFO_SHAPE=.1)
acid('Acid Fold', 210, 3.4, .30, .78, DIST_FOLD=.30, DIST_AMT=.10)
acid('Acid Smooth', 240, 2.4, .60, .55, slope=.5, ad=1.4, SUB_LEVEL=.35, OUT_TONE=.7, AMP_S=.8)
acid('Acid Twin', 190, 3.5, .32, .80, B_LEVEL=.6, B_COARSE=co(-12) , B_FINE=fine(6), SUB_LEVEL=.15)

# =============================================================================================
# SUB / DEEP BASS
# =============================================================================================
def sub(name, **kw):
    d = dict(A_SHAPE=SIN, A_LEVEL=.85, F_CUTOFF=cut(1200), AMP_A=ea(.004), AMP_D=ed(.8), AMP_S=.9, AMP_R=ed(.12), AMP_VEL=.4)
    d.update(kw); add('Sub Bass', name, 36, **d)
sub('Pure Sub')
sub('Warm Sub', A_SHAPE=.12, A_LEVEL=.8, F_CUTOFF=cut(700), SUB_LEVEL=.3, OUT_TONE=.7)
sub('Round Soft Bass', A_SHAPE=TRI, F_CUTOFF=cut(500), F_RES=.05, F_SLOPE=.5, OUT_TONE=.65)
sub('Deep Tri Bass', A_SHAPE=TRI, A_LEVEL=.8, SUB_LEVEL=.45, SUB_SHAPE=.1, F_CUTOFF=cut(900))
sub('Hollow Sub', A_SHAPE=SIN, SUB_LEVEL=.5, SUB_SHAPE=.5, A_COARSE=co(12), A_LEVEL=.35, F_CUTOFF=cut(600), F_RES=.3)
sub('Boom Sub', PITCH_AMT=pen(12), PITCH_DEC=pdec(.06), AMP_D=ed(1.2), AMP_S=.2, A_LEVEL=.9)
sub('Sub Boom Long', PITCH_AMT=pen(14), PITCH_DEC=pdec(.05), AMP_D=ed(2.2), AMP_S=0, AMP_R=ed(.3), DIST_AMT=.12, A_LEVEL=.9)
sub('Sub Boom Short', PITCH_AMT=pen(18), PITCH_DEC=pdec(.04), AMP_D=ed(.5), AMP_S=0, AMP_R=ed(.1), A_LEVEL=.9)
sub('Sub Pluck', AMP_D=ed(.35), AMP_S=0, PITCH_AMT=pen(5), PITCH_DEC=pdec(.03), NOISE_LEVEL=.2, NOISE_TRANS=1, NOISE_TDEC=.1)
sub('Dub Sub', A_SHAPE=.1, DLY_MIX=.25, DLY_TIME=dly(.375), DLY_FB=.5, REV_MIX=.12, F_CUTOFF=cut(500))
sub('Sine Octave', A_SHAPE=SIN, SUB_LEVEL=.6, SUB_SHAPE=0, A_LEVEL=.6, AMP_S=1.0)
sub('Saw Sub Filtered', A_SHAPE=SAW, A_LEVEL=.7, F_CUTOFF=cut(260), F_RES=.2, F_SLOPE=1.0, SUB_LEVEL=.4, F_ENVAMT=fo(1.5), FE_D=fdec(.3), FE_S=.2)
sub('Growl Sub', A_SHAPE=.2, SUB_LEVEL=.4, F_CUTOFF=cut(700), F_RES=.45, LFO_FILT=.25, LFO_RATE=lfo(3), DIST_AMT=.15)
sub('Tape Sub', A_SHAPE=.1, CHO_MIX=.2, CHO_DEPTH=.2, DIST_AMT=.08, OUT_TONE=.6, LFO_VIB=vib(.05), LFO_RATE=lfo(.6))

# =============================================================================================
# ANALOG BASS
# =============================================================================================
def bass(name, **kw):
    d = dict(A_SHAPE=SAW, A_LEVEL=.8, F_CUTOFF=cut(500), F_RES=.2, F_SLOPE=1.0, F_ENVAMT=fo(2.5), FE_D=fdec(.25), FE_S=.15,
             AMP_A=ea(.004), AMP_D=ed(.5), AMP_S=.8, AMP_R=ed(.1), F_KEYTRK=.4, F_VEL=.4)
    d.update(kw); add('Bass', name, 36, **d)
bass('Ladder Bass', B_LEVEL=.7, B_FINE=fine(8), SUB_LEVEL=.25, F_CUTOFF=cut(380), F_RES=.3, F_ENVAMT=fo(3))
bass('Fat Saw Bass', A_UNI=uni(3), A_UNIDET=udet(8), SUB_LEVEL=.3, F_CUTOFF=cut(550))
bass('Pulse Bass', A_SHAPE=PUL, A_PW=.3, F_CUTOFF=cut(420), F_ENVAMT=fo(2.0), SUB_LEVEL=.2)
bass('Mono Funk', F_CUTOFF=cut(300), F_RES=.35, F_ENVAMT=fo(3.2), FE_D=fdec(.12), FE_S=0, AMP_D=ed(.3), AMP_S=.5)
bass('Rubber Bass', A_SHAPE=TRI, B_SHAPE=SAW, B_LEVEL=.5, B_COARSE=co(12), F_CUTOFF=cut(350), F_RES=.5, F_ENVAMT=fo(3.5), FE_D=fdec(.18), FE_S=0)
bass('Square Bass', A_SHAPE=PUL, A_PW=.5, F_CUTOFF=cut(600), F_SLOPE=.5, F_ENVAMT=fo(1.5), SUB_LEVEL=.3)
bass('Wide Unison Bass', A_UNI=uni(5), A_UNIDET=udet(14), F_CUTOFF=cut(450), SUB_LEVEL=.4, CHO_MIX=.25)
bass('PWM Bass', A_SHAPE=PUL, A_PW=.2, B_SHAPE=PUL, B_PW=.8, B_LEVEL=.6, B_FINE=fine(5), F_CUTOFF=cut(520), LFO_RATE=lfo(.8))
bass('Electro Bass', A_SHAPE=PUL, A_PW=.4, F_CUTOFF=cut(900), F_RES=.5, F_ENVAMT=fo(3), FE_D=fdec(.1), FE_S=0, AMP_D=ed(.2), AMP_S=.3, DIST_AMT=.15)
bass('FM Bass', A_SHAPE=SIN, B_SHAPE=SIN, B_COARSE=co(0), A_FM=.55, B_LEVEL=0, AMP_D=ed(.3), AMP_S=.6, F_CUTOFF=cut(3000), F_ENVAMT=.5, SUB_LEVEL=.3)
bass('Ring Bass', A_SHAPE=TRI, B_SHAPE=SIN, B_COARSE=co(7), B_RING=.4, B_LEVEL=0, F_CUTOFF=cut(700), SUB_LEVEL=.4)
bass('Drive Bass', F_DRIVE=.5, DIST_AMT=.25, F_CUTOFF=cut(420), F_RES=.3, SUB_LEVEL=.3, OUT_TONE=.75)
bass('Pluck Bass', F_CUTOFF=cut(260), F_ENVAMT=fo(3.6), FE_D=fdec(.15), FE_S=0, AMP_D=ed(.4), AMP_S=0, AMP_R=ed(.15), SUB_LEVEL=.3)
bass('Soft Pad Bass', A_SHAPE=TRI, B_SHAPE=SAW, B_LEVEL=.35, F_CUTOFF=cut(380), F_ENVAMT=fo(1.0), AMP_A=ea(.05), CHO_MIX=.3, OUT_TONE=.7)

# =============================================================================================
# TWIN-SAW / WOBBLE / NEURO
# =============================================================================================
def twinsaw(name, **kw):
    d = dict(A_SHAPE=SAW, A_LEVEL=.7, B_SHAPE=SAW, B_LEVEL=.7, B_FINE=fine(18), A_FINE=fine(-14), F_CUTOFF=cut(520), F_RES=.25,
             F_SLOPE=1.0, SUB_LEVEL=.35, AMP_A=ea(.01), AMP_S=.9, AMP_R=ed(.15), CHO_MIX=.3, CHO_DEPTH=.4)
    d.update(kw); add('Growl & Wobble', name, 33, **d)
twinsaw('Twin Saw Classic')
twinsaw('Twin Saw Dark', F_CUTOFF=cut(300), DIST_AMT=.12, OUT_TONE=.65)
twinsaw('Twin Saw Wide', A_UNI=uni(3), A_UNIDET=udet(10), CHO_MIX=.5, F_CUTOFF=cut(650))
twinsaw('Neuro Growl', F_RES=.6, LFO_FILT=.5, LFO_RATE=lfo(6), LFO_SHAPE=.4, DIST_AMT=.3, F_DRIVE=.3)
twinsaw('Wobble Bass', LFO_FILT=.6, LFO_RATE=lfo(2.2), LFO_SHAPE=.1, F_RES=.45, F_CUTOFF=cut(380))
twinsaw('Wub Slow', LFO_FILT=.75, LFO_RATE=lfo(1.0), LFO_SHAPE=.1, F_RES=.5, F_CUTOFF=cut(300), DIST_AMT=.1)
twinsaw('Wub Fast', LFO_FILT=.5, LFO_RATE=lfo(8), LFO_SHAPE=.3, F_RES=.4, F_CUTOFF=cut(420))
twinsaw('Dubstep Growl', B_COARSE=co(12), B_FINE=fine(-6), F_RES=.65, LFO_FILT=.55, LFO_RATE=lfo(4), LFO_SHAPE=.6, DIST_AMT=.35, DIST_FOLD=.15)

# =============================================================================================
# LEADS
# =============================================================================================
def lead(name, **kw):
    d = dict(A_SHAPE=SAW, A_LEVEL=.75, F_CUTOFF=cut(5000), F_RES=.15, F_ENVAMT=.5, AMP_A=ea(.008), AMP_D=ed(.4), AMP_S=.85,
             AMP_R=ed(.2), LFO_VIB=vib(.25), LFO_RATE=lfo(5.2), LFO_FADE=.45, F_KEYTRK=.6)
    d.update(kw); add('Lead', name, 69, **d)
lead('Saw Lead', DLY_MIX=.12, DLY_TIME=dly(.3), DLY_FB=.3, REV_MIX=.1)
lead('Square Lead', A_SHAPE=PUL, A_PW=.5, F_CUTOFF=cut(3500))
lead('PWM Lead', A_SHAPE=PUL, A_PW=.35, B_SHAPE=PUL, B_PW=.65, B_LEVEL=.7, B_FINE=fine(6), CHO_MIX=.3)
lead('Fat Unison Lead', A_UNI=uni(5), A_UNIDET=udet(16), CHO_MIX=.2, REV_MIX=.12)
lead('Trance Lead', A_UNI=uni(5), A_UNIDET=udet(20), F_CUTOFF=cut(7000), DLY_MIX=.3, DLY_TIME=dly(.35), DLY_FB=.45, REV_MIX=.22)
lead('Soft Sine Lead', A_SHAPE=SIN, B_SHAPE=SIN, B_COARSE=co(12), B_LEVEL=.15, F_CUTOFF=cut(6000), REV_MIX=.15)
lead('Vibrato Lead', A_SHAPE=PUL, A_PW=.45, LFO_VIB=vib(.5), LFO_RATE=lfo(5.8), LFO_FADE=.3)
lead('Whistle Lead', A_SHAPE=SIN, A_LEVEL=.8, LFO_VIB=vib(.4), LFO_FADE=.55, C_LEVEL=.15, C_COUNT=.1, A_COARSE=co(12), REV_MIX=.2)
lead('FM Bell Lead', A_SHAPE=SIN, B_SHAPE=SIN, B_COARSE=co(12), A_FM=.35, B_LEVEL=0, AMP_D=ed(.9), AMP_S=.5, REV_MIX=.15)
lead('Ring Lead', A_SHAPE=TRI, B_SHAPE=SIN, B_COARSE=co(5), B_RING=.35, B_LEVEL=0, F_CUTOFF=cut(4000))
lead('Brass Lead', A_UNI=uni(2), A_UNIDET=udet(6), F_CUTOFF=cut(1600), F_ENVAMT=fo(2.2), FE_A=ea(.08), FE_D=fdec(.3), FE_S=.6, AMP_A=ea(.04))
lead('Pluck Lead', AMP_D=ed(.35), AMP_S=.1, F_CUTOFF=cut(900), F_ENVAMT=fo(3), FE_D=fdec(.2), FE_S=0, DLY_MIX=.2, DLY_TIME=dly(.25))
lead('Analog Lead', B_LEVEL=.6, B_FINE=fine(7), B_DRIFT=.3, F_CUTOFF=cut(3200), F_RES=.3, F_SLOPE=.6, LFO_VIB=vib(.2))
lead('Funky Mono Lead', A_SHAPE=PUL, A_PW=.3, F_CUTOFF=cut(900), F_RES=.5, F_ENVAMT=fo(3), FE_D=fdec(.18), FE_S=.2, AMP_S=.7)
lead('Wide Lead', A_UNI=uni(4), A_UNIDET=udet(24), CHO_MIX=.45, CHO_DEPTH=.5, REV_MIX=.2, F_CUTOFF=cut(6500))
lead('Thin Lead', A_SHAPE=PUL, A_PW=.15, F_CUTOFF=cut(8000), F_MODE=.3, REV_MIX=.1)
lead('Retro Lead', A_SHAPE=TRI, B_SHAPE=PUL, B_LEVEL=.4, B_PW=.25, F_CUTOFF=cut(2800), LFO_VIB=vib(.3), CHO_MIX=.2)
lead('Dist Lead', DIST_AMT=.45, F_CUTOFF=cut(3800), F_RES=.2, F_DRIVE=.2, OUT_TONE=.8, DLY_MIX=.15)
lead('Vowel Lead', A_UNI=uni(2), VOWEL_MIX=.8, VOWEL_POS=.25, VOWEL_Q=.6, F_CUTOFF=cut(9000))

# =============================================================================================
# PLUCKS
# =============================================================================================
def pluck(name, **kw):
    d = dict(A_SHAPE=SAW, A_LEVEL=.75, F_CUTOFF=cut(900), F_RES=.2, F_ENVAMT=fo(3.2), FE_D=fdec(.18), FE_S=0, AMP_A=ea(.001),
             AMP_D=ed(.35), AMP_S=0, AMP_R=ed(.2), F_KEYTRK=.6, F_VEL=.5, AMP_VEL=.7)
    d.update(kw); add('Pluck', name, 60, **d)
pluck('Saw Pluck', REV_MIX=.15)
pluck('Square Pluck', A_SHAPE=PUL, F_CUTOFF=cut(700))
pluck('Pulse Pluck', A_SHAPE=PUL, A_PW=.2, B_SHAPE=PUL, B_PW=.8, B_LEVEL=.5, CHO_MIX=.25)
pluck('Comb String', A_LEVEL=.2, NOISE_LEVEL=.7, NOISE_TRANS=1, NOISE_TDEC=.2, COMB_MIX=1.0, COMB_FB=.94, AMP_D=ed(1.0), F_CUTOFF=cut(6000), F_ENVAMT=.5)
pluck('Nylon Guitar', A_SHAPE=TRI, B_SHAPE=SAW, B_LEVEL=.3, COMB_MIX=.5, COMB_FB=.85, NOISE_LEVEL=.15, NOISE_TRANS=1, NOISE_TDEC=.1, AMP_D=ed(.9), F_CUTOFF=cut(2200), REV_MIX=.15)
pluck('Harp Pluck', A_SHAPE=TRI, C_LEVEL=.4, C_TILT=.5, C_COUNT=.3, AMP_D=ed(1.4), F_CUTOFF=cut(4000), REV_MIX=.3, REV_SIZE=.8)
pluck('Pizzicato', A_SHAPE=TRI, AMP_D=ed(.12), F_CUTOFF=cut(1500), COMB_MIX=.3, COMB_FB=.7)
pluck('Kalimba', A_SHAPE=SIN, B_SHAPE=SIN, B_COARSE=co(19), B_LEVEL=.25, AMP_D=ed(.7), F_CUTOFF=cut(5000), REV_MIX=.2)
pluck('Marimba', A_SHAPE=SIN, B_SHAPE=SIN, B_COARSE=co(24), B_LEVEL=.25, AMP_D=ed(.45), F_CUTOFF=cut(6000), PITCH_AMT=pen(.5), NOISE_LEVEL=.1, NOISE_TRANS=1, NOISE_TDEC=.05)
pluck('Koto', A_SHAPE=PUL, A_PW=.4, F_CUTOFF=cut(1400), F_RES=.35, COMB_MIX=.4, COMB_FB=.9, AMP_D=ed(.8), PITCH_AMT=pen(1.5), PITCH_DEC=pdec(.05))
pluck('Banjo', A_SHAPE=PUL, A_PW=.25, F_CUTOFF=cut(2800), F_ENVAMT=fo(2), COMB_MIX=.35, COMB_FB=.88, AMP_D=ed(.4), F_MODE=.15)
pluck('Short Pluck', AMP_D=ed(.1), FE_D=fdec(.08), F_CUTOFF=cut(700))
pluck('Edm Pluck', A_UNI=uni(3), A_UNIDET=udet(12), F_CUTOFF=cut(1600), AMP_D=ed(.5), CHO_MIX=.3, DLY_MIX=.2, DLY_TIME=dly(.3), REV_MIX=.2)
pluck('Soft Pluck', A_SHAPE=TRI, F_CUTOFF=cut(1100), F_RES=.05, AMP_D=ed(.45), OUT_TONE=.7, REV_MIX=.2)

# =============================================================================================
# KEYS / ORGANS
# =============================================================================================
def keys(name, **kw):
    d = dict(A_SHAPE=SIN, A_LEVEL=.7, F_CUTOFF=cut(6000), AMP_A=ea(.002), AMP_D=ed(1.2), AMP_S=.2, AMP_R=ed(.25), AMP_VEL=.8, F_VEL=.5, F_KEYTRK=.7)
    d.update(kw); add('Keys', name, 60, **d)
keys('Soft EP', B_SHAPE=SIN, B_COARSE=co(12), A_FM=.22, B_LEVEL=0, AMP_D=ed(1.6), CHO_MIX=.35, CHO_RATE=lfo(.8), REV_MIX=.15)
keys('Tine EP', B_SHAPE=SIN, B_COARSE=co(19), A_FM=.3, B_LEVEL=0, AMP_D=ed(1.3), NOISE_LEVEL=.1, NOISE_TRANS=1, NOISE_TDEC=.1, CHO_MIX=.3)
keys('Bark EP', B_SHAPE=SIN, B_COARSE=co(12), A_FM=.45, B_LEVEL=0, AMP_D=ed(.9), DIST_AMT=.1, CHO_MIX=.25)
keys('Spiky Keys', A_SHAPE=PUL, A_PW=.3, A_LEVEL=.75, F_CUTOFF=cut(1800), F_RES=.2, F_ENVAMT=fo(2.2), FE_D=fdec(.1), FE_S=.1, AMP_D=ed(.25), AMP_S=.1, F_MODE=.2)
keys('Drawbar Organ', A_LEVEL=0, C_LEVEL=.85, C_TILT=.4, C_COUNT=.22, C_ODDEVEN=.5, AMP_A=ea(.004), AMP_D=ed(.1), AMP_S=1.0, AMP_R=ed(.05), LFO_TREM=.15, LFO_RATE=lfo(6.5), CHO_MIX=.3)
keys('Perc Organ', A_LEVEL=0, C_LEVEL=.85, C_TILT=.45, C_COUNT=.2, AMP_S=1.0, AMP_D=ed(.1), AMP_R=ed(.05), NOISE_LEVEL=.25, NOISE_TRANS=1, NOISE_TDEC=.1, NOISE_TONAL=.8, CHO_MIX=.25)
keys('Rock Organ', A_LEVEL=0, C_LEVEL=.9, C_TILT=.6, C_COUNT=.3, C_ODDEVEN=.35, AMP_S=1.0, AMP_D=ed(.1), AMP_R=ed(.06), DIST_AMT=.25, CHO_MIX=.3, F_CUTOFF=cut(9000), OUT_TONE=.85)
keys('Church Organ', A_LEVEL=0, C_LEVEL=.9, C_TILT=.5, C_COUNT=.35, AMP_A=ea(.03), AMP_S=1.0, AMP_D=ed(.1), AMP_R=ed(.3), REV_MIX=.55, REV_SIZE=.95, SUB_LEVEL=.3)
keys('Synth Piano', A_SHAPE=TRI, B_SHAPE=SAW, B_LEVEL=.25, B_COARSE=co(12), F_CUTOFF=cut(2400), F_ENVAMT=fo(1.8), FE_D=fdec(.5), FE_S=.1, AMP_D=ed(1.8), NOISE_LEVEL=.12, NOISE_TRANS=1, NOISE_TDEC=.08, REV_MIX=.2)
keys('Rotary Organ', A_LEVEL=0, C_LEVEL=.85, C_TILT=.42, C_COUNT=.25, AMP_S=1.0, AMP_D=ed(.1), AMP_R=ed(.1), LFO_TREM=.35, LFO_VIB=vib(.12), LFO_RATE=lfo(5.0), LFO_SHAPE=0, CHO_MIX=.6, CHO_RATE=lfo(1.0))
keys('Toy Piano', A_SHAPE=SIN, B_SHAPE=SIN, B_COARSE=co(24), B_LEVEL=.45, B_FINE=fine(18), AMP_D=ed(.35), AMP_S=0, F_CUTOFF=cut(9000))
keys('Vintage Keys', A_SHAPE=TRI, B_SHAPE=PUL, B_LEVEL=.35, F_CUTOFF=cut(2800), AMP_D=ed(1.0), AMP_S=.35, CHO_MIX=.4, REV_MIX=.15, LFO_VIB=vib(.1))
keys('Glass Keys', A_SHAPE=SIN, A_LEVEL=.5, C_LEVEL=.4, C_INHARM=.3, C_TILT=.65, C_COUNT=.25, AMP_D=ed(1.4), AMP_S=.1, REV_MIX=.3)
keys('Harpsichord', A_SHAPE=PUL, A_PW=.35, B_SHAPE=PUL, B_PW=.2, B_LEVEL=.5, B_COARSE=co(12), F_CUTOFF=cut(3200), AMP_D=ed(.4), AMP_S=0, NOISE_LEVEL=.15, NOISE_TRANS=1, NOISE_TDEC=.06)

# =============================================================================================
# PADS
# =============================================================================================
def pad(name, **kw):
    d = dict(A_SHAPE=SAW, A_LEVEL=.6, A_UNI=uni(4), A_UNIDET=udet(14), B_SHAPE=SAW, B_LEVEL=.45, B_FINE=fine(9), F_CUTOFF=cut(3000),
             F_RES=.1, F_ENVAMT=fo(1.5), FE_A=ea(.8), FE_D=fdec(1.5), FE_S=.6, AMP_A=ea(.7), AMP_D=ed(1.0), AMP_S=.85, AMP_R=ed(1.2),
             CHO_MIX=.45, CHO_DEPTH=.5, REV_MIX=.3, REV_SIZE=.8, F_KEYTRK=.5, B_DRIFT=.25)
    d.update(kw); add('Pad', name, 60, **d)
pad('Warm Pad', F_CUTOFF=cut(1800), OUT_TONE=.75)
pad('Super Pad', A_UNI=uni(5), A_UNIDET=udet(22), F_CUTOFF=cut(4500))
pad('Glass Pad', A_SHAPE=TRI, B_SHAPE=SIN, B_COARSE=co(19), B_LEVEL=.3, C_LEVEL=.3, C_INHARM=.25, F_CUTOFF=cut(7000), REV_MIX=.45)
pad('String Pad', A_UNI=uni(5), A_UNIDET=udet(10), F_CUTOFF=cut(3500), AMP_A=ea(.5), CHO_MIX=.6, LFO_VIB=vib(.08), LFO_RATE=lfo(5))
pad('Choir Pad', VOWEL_MIX=.85, VOWEL_POS=.15, VOWEL_Q=.5, F_CUTOFF=cut(9000), AMP_A=ea(.6), LFO_VIB=vib(.1))
pad('Dark Pad', F_CUTOFF=cut(700), F_RES=.2, SUB_LEVEL=.25, REV_MIX=.4, OUT_TONE=.6)
pad('Evolving Pad', LFO_FILT=.3, LFO_RATE=lfo(.15), LFO_SHAPE=.1, B_SHAPE=PUL, B_PW=.4, F_CUTOFF=cut(2200), FE_A=ea(2.5), REV_MIX=.4)
pad('Slow Saw Pad', AMP_A=ea(1.8), AMP_R=ed(2.5), FE_A=ea(2.0), F_CUTOFF=cut(2600))
pad('Pulse Pad', A_SHAPE=PUL, A_PW=.35, B_SHAPE=PUL, B_PW=.65, B_LEVEL=.6, F_CUTOFF=cut(2600), LFO_RATE=lfo(.3))
pad('Air Pad', A_LEVEL=.3, NOISE_LEVEL=.35, NOISE_TONAL=.6, NOISE_COLOR=.65, F_CUTOFF=cut(6000), REV_MIX=.5, AMP_A=ea(1.2))
pad('Vowel Pad', VOWEL_MIX=.7, VOWEL_POS=.55, VOWEL_Q=.45, F_CUTOFF=cut(8000), LFO_RATE=lfo(.2), LFO_FILT=.1)
pad('Analog Pad', B_DRIFT=.5, A_UNI=uni(2), F_CUTOFF=cut(1500), F_RES=.2, LFO_VIB=vib(.06), CHO_MIX=.5)
pad('Lush Pad', A_UNI=uni(5), A_UNIDET=udet(26), B_LEVEL=.6, CHO_MIX=.7, CHO_DEPTH=.7, REV_MIX=.45, F_CUTOFF=cut(3800))
pad('Ambient Pad', A_SHAPE=TRI, B_SHAPE=SIN, B_COARSE=co(12), AMP_A=ea(2.5), AMP_R=ed(4), REV_MIX=.65, REV_SIZE=.95, DLY_MIX=.25, DLY_TIME=dly(.6), DLY_FB=.55, F_CUTOFF=cut(2400))
pad('Space Pad', B_COARSE=co(7), B_LEVEL=.5, LFO_FILT=.2, LFO_RATE=lfo(.1), DLY_MIX=.3, DLY_TIME=dly(.5), DLY_FB=.6, REV_MIX=.5, F_CUTOFF=cut(2800))
pad('Ice Pad', A_SHAPE=SIN, C_LEVEL=.5, C_INHARM=.3, C_TILT=.6, C_COUNT=.3, F_CUTOFF=cut(9000), REV_MIX=.55, CHO_MIX=.5, AMP_A=ea(1.0))
pad('Soft Sine Pad', A_SHAPE=SIN, B_SHAPE=TRI, B_LEVEL=.3, A_UNI=uni(3), A_UNIDET=udet(6), F_CUTOFF=cut(2200), OUT_TONE=.7)
pad('Detuned Pad', A_UNIDET=udet(34), B_FINE=fine(30), CHO_MIX=.5, F_CUTOFF=cut(2400), B_DRIFT=.6)

# =============================================================================================
# STRINGS / BRASS / WINDS
# =============================================================================================
def ens(name, **kw):
    d = dict(A_SHAPE=SAW, A_LEVEL=.7, A_UNI=uni(4), A_UNIDET=udet(9), F_CUTOFF=cut(3500), AMP_A=ea(.25), AMP_D=ed(.5), AMP_S=.9, AMP_R=ed(.4),
             CHO_MIX=.5, REV_MIX=.25, LFO_VIB=vib(.1), LFO_RATE=lfo(5.2), LFO_FADE=.5, F_KEYTRK=.6)
    d.update(kw); add('Strings & Brass', name, 60, **d)
ens('Strings Ensemble')
ens('String Machine', B_SHAPE=SAW, B_LEVEL=.5, B_COARSE=co(12), B_FINE=fine(12), CHO_MIX=.8, CHO_DEPTH=.7, F_CUTOFF=cut(2800))
ens('Cello Synth', A_UNI=uni(2), A_UNIDET=udet(5), F_CUTOFF=cut(1300), AMP_A=ea(.15), LFO_VIB=vib(.15), SUB_LEVEL=.15, A_COARSE=co(-12))
ens('Violin Synth', A_UNI=uni(2), A_UNIDET=udet(6), F_CUTOFF=cut(5500), AMP_A=ea(.12), LFO_VIB=vib(.22), LFO_FADE=.35, A_COARSE=co(12))
ens('Brass Section', A_UNI=uni(3), A_UNIDET=udet(8), F_CUTOFF=cut(1300), F_ENVAMT=fo(2.3), FE_A=ea(.12), FE_D=fdec(.3), FE_S=.7, AMP_A=ea(.06), CHO_MIX=.2, LFO_VIB=0)
ens('Soft Brass', A_UNI=uni(2), F_CUTOFF=cut(900), F_ENVAMT=fo(1.5), FE_A=ea(.2), FE_S=.6, AMP_A=ea(.12), CHO_MIX=.15, REV_MIX=.2, OUT_TONE=.8, LFO_VIB=0)
ens('Poly Brass', B_SHAPE=SAW, B_LEVEL=.6, B_FINE=fine(12), F_CUTOFF=cut(1100), F_RES=.2, F_ENVAMT=fo(2.6), FE_A=ea(.05), FE_D=fdec(.35), FE_S=.4, AMP_A=ea(.03), LFO_VIB=0)
ens('Brass Stab', A_UNI=uni(3), F_CUTOFF=cut(1000), F_ENVAMT=fo(3), FE_A=ea(.02), FE_D=fdec(.25), FE_S=.2, AMP_A=ea(.01), AMP_D=ed(.35), AMP_S=.1, AMP_R=ed(.2), LFO_VIB=0)
ens('French Horn', A_SHAPE=TRI, B_SHAPE=SAW, B_LEVEL=.4, F_CUTOFF=cut(900), AMP_A=ea(.15), REV_MIX=.4, REV_SIZE=.9, CHO_MIX=.2, LFO_VIB=vib(.06))
ens('Trumpet Synth', A_UNI=uni(1), F_CUTOFF=cut(2200), F_RES=.2, F_ENVAMT=fo(2.5), FE_A=ea(.04), FE_S=.7, AMP_A=ea(.03), CHO_MIX=0, LFO_VIB=vib(.2), LFO_FADE=.6, A_COARSE=co(12))
ens('Sax Synth', A_SHAPE=PUL, A_PW=.4, A_UNI=uni(1), F_CUTOFF=cut(1700), F_RES=.25, NOISE_LEVEL=.18, NOISE_TONAL=.6, AMP_A=ea(.05), CHO_MIX=0, LFO_VIB=vib(.18), LFO_FADE=.5, VOWEL_MIX=.3, VOWEL_POS=.4)
ens('Flute Synth', A_SHAPE=TRI, A_UNI=uni(1), A_LEVEL=.6, NOISE_LEVEL=.28, NOISE_TONAL=.85, NOISE_COLOR=.6, F_CUTOFF=cut(5500), AMP_A=ea(.07), CHO_MIX=0, LFO_VIB=vib(.14), LFO_FADE=.6, A_COARSE=co(12), REV_MIX=.3)

# =============================================================================================
# BELLS / MALLETS
# =============================================================================================
def bell(name, **kw):
    d = dict(A_SHAPE=SIN, A_LEVEL=0, C_LEVEL=.85, C_TILT=.6, C_COUNT=.4, C_INHARM=.5, AMP_A=ea(.0008), AMP_D=ed(2.0), AMP_S=0, AMP_R=ed(1.0),
             REV_MIX=.3, REV_SIZE=.7, F_CUTOFF=cut(14000), AMP_VEL=.8)
    d.update(kw); add('Bells & Mallets', name, 72, **d)
bell('Bell Classic')
bell('Tubular Bell', C_INHARM=.35, C_COUNT=.3, AMP_D=ed(3.5), C_ODDEVEN=.65, REV_MIX=.4)
bell('Glockenspiel', C_INHARM=.6, C_TILT=.55, AMP_D=ed(1.2), A_COARSE=co(12), C_OCT=.75)
bell('Music Box', C_INHARM=.4, C_COUNT=.2, C_TILT=.5, AMP_D=ed(.9), F_CUTOFF=cut(9000), C_OCT=.75)
bell('Vibraphone', C_INHARM=.2, C_COUNT=.15, C_TILT=.5, AMP_D=ed(2.5), LFO_TREM=.45, LFO_RATE=lfo(4.5), LFO_SHAPE=0, LFO_FADE=0)
bell('FM Bell', A_LEVEL=.7, C_LEVEL=0, B_SHAPE=SIN, B_COARSE=co(19.02), A_FM=.4, B_LEVEL=0, AMP_D=ed(2.5), FE_D=fdec(1.0), F_CUTOFF=cut(9000))
bell('Gong', C_INHARM=.85, C_COUNT=.7, C_TILT=.55, AMP_D=ed(5.0), AMP_R=ed(3.0), A_COARSE=co(-12), NOISE_LEVEL=.15, NOISE_TRANS=1, NOISE_TDEC=.5, REV_MIX=.45)
bell('Crystal', C_INHARM=.7, C_COUNT=.3, C_TILT=.7, AMP_D=ed(2.8), CHO_MIX=.3, REV_MIX=.5, C_OCT=.75)
bell('Chime', C_INHARM=.55, C_COUNT=.18, C_TILT=.65, AMP_D=ed(3.2), C_OCT=.75, REV_MIX=.4)
bell('Steel Drum', C_INHARM=.3, C_COUNT=.2, C_TILT=.45, AMP_D=ed(.7), A_LEVEL=.3, A_SHAPE=SIN, F_CUTOFF=cut(6000), NOISE_LEVEL=.1, NOISE_TRANS=1, NOISE_TDEC=.1)
bell('Celesta', C_INHARM=.45, C_COUNT=.15, C_TILT=.5, AMP_D=ed(1.3), C_OCT=.75, REV_MIX=.3)
bell('Hand Bell', C_INHARM=.5, C_COUNT=.3, C_TILT=.55, AMP_D=ed(2.2), C_ODDEVEN=.7, LFO_TREM=.15, LFO_RATE=lfo(5), LFO_FADE=0)

# =============================================================================================
# ARP / SEQUENCE
# =============================================================================================
def arp(name, **kw):
    d = dict(A_SHAPE=SAW, A_LEVEL=.75, F_CUTOFF=cut(1400), F_RES=.25, F_ENVAMT=fo(3), FE_D=fdec(.15), FE_S=0, AMP_A=ea(.001), AMP_D=ed(.2),
             AMP_S=0, AMP_R=ed(.1), DLY_MIX=.3, DLY_TIME=dly(.19), DLY_FB=.4, REV_MIX=.15, F_KEYTRK=.7)
    d.update(kw); add('Arp & Seq', name, 69, **d)
arp('Arp Pluck')
arp('Arp Saw', A_UNI=uni(2), A_UNIDET=udet(8))
arp('Arp Square', A_SHAPE=PUL, F_CUTOFF=cut(1000))
arp('Seq Bleep', A_SHAPE=SIN, B_SHAPE=SIN, B_COARSE=co(12), A_FM=.25, B_LEVEL=0, F_CUTOFF=cut(9000), F_ENVAMT=.5, AMP_D=ed(.15))
arp('Seq Bass Step', A_SHAPE=PUL, A_PW=.4, F_CUTOFF=cut(600), F_RES=.5, F_ENVAMT=fo(3.2), FE_D=fdec(.1), AMP_D=ed(.18), DLY_MIX=.1, A_COARSE=co(-12))
arp('Arp Bell', A_LEVEL=0, C_LEVEL=.7, C_INHARM=.35, C_TILT=.6, C_COUNT=.3, AMP_D=ed(.6), F_CUTOFF=cut(12000), F_ENVAMT=.5, REV_MIX=.3)
arp('Trance Arp', A_UNI=uni(3), A_UNIDET=udet(14), F_CUTOFF=cut(2200), AMP_D=ed(.25), DLY_MIX=.4, DLY_TIME=dly(.28), DLY_FB=.55, REV_MIX=.25)
arp('Chip Arp', A_SHAPE=PUL, A_PW=.25, F_CUTOFF=cut(14000), F_ENVAMT=.5, AMP_D=ed(.1), DLY_MIX=.2)
arp('Dub Chord Stab', A_UNI=uni(3), A_UNIDET=udet(10), F_CUTOFF=cut(900), F_ENVAMT=fo(2.8), AMP_D=ed(.3), DLY_MIX=.45, DLY_TIME=dly(.375), DLY_FB=.65, REV_MIX=.3, A_COARSE=co(-12))
arp('Rave Stab', B_SHAPE=SAW, B_LEVEL=.8, B_COARSE=co(7), B_FINE=fine(-9), A_UNI=uni(3), F_CUTOFF=cut(1800), F_ENVAMT=fo(2.5), AMP_D=ed(.35), AMP_S=.1, DIST_AMT=.1, DLY_MIX=.15)

# =============================================================================================
# FX / RISERS
# =============================================================================================
def fx(name, **kw):
    d = dict(A_LEVEL=0, AMP_A=ea(.01), AMP_D=ed(1.0), AMP_S=.8, AMP_R=ed(.8), REV_MIX=.3)
    d.update(kw); add('FX', name, 60, **d)
fx('Laser Zap', A_LEVEL=.8, A_SHAPE=SAW, PITCH_AMT=pen(40), PITCH_DEC=pdec(.2), F_CUTOFF=cut(4000), F_RES=.5, AMP_D=ed(.25), AMP_S=0, REV_MIX=.1)
fx('Noise Riser', NOISE_LEVEL=.85, NOISE_COLOR=.6, F_MODE=BP, F_CUTOFF=cut(300), F_RES=.5, F_ENVAMT=fo(6), FE_A=ea(3.5), FE_D=fdec(.5), FE_S=1.0, AMP_A=ea(3.0), AMP_S=1.0, REV_MIX=.35)
fx('Down Lifter', NOISE_LEVEL=.85, NOISE_COLOR=.55, F_MODE=BP, F_CUTOFF=cut(300), F_RES=.5, F_ENVAMT=fo(6), FE_A=0, FE_D=fdec(2.5), FE_S=0, AMP_A=ea(.05), AMP_D=ed(3), AMP_S=0, REV_MIX=.4)
fx('Sweep', A_LEVEL=.5, A_UNI=uni(4), A_UNIDET=udet(25), F_CUTOFF=cut(200), F_RES=.7, F_ENVAMT=fo(5), FE_A=ea(2.0), FE_D=fdec(2.0), FE_S=.3, AMP_A=ea(1.5), AMP_S=1.0, REV_MIX=.3)
fx('Wind', NOISE_LEVEL=.8, NOISE_COLOR=.45, F_MODE=BP, F_CUTOFF=cut(900), F_RES=.55, LFO_FILT=.5, LFO_RATE=lfo(.25), LFO_SHAPE=.1, AMP_A=ea(1.5), AMP_S=1.0, AMP_R=ed(2), REV_MIX=.5)
fx('Ocean', NOISE_LEVEL=.9, NOISE_COLOR=.25, F_CUTOFF=cut(900), F_RES=.1, LFO_TREM=.7, LFO_RATE=lfo(.18), LFO_SHAPE=0, LFO_FADE=0, AMP_A=ea(1.8), AMP_S=1.0, AMP_R=ed(3), REV_MIX=.4)
fx('Alien', A_LEVEL=.6, A_SHAPE=SIN, B_SHAPE=SIN, B_COARSE=co(13.5), B_RING=.7, LFO_VIB=vib(1.2), LFO_RATE=lfo(7), LFO_SHAPE=.3, F_CUTOFF=cut(6000), A_FM=.5, REV_MIX=.4)
fx('Siren', A_LEVEL=.8, A_SHAPE=PUL, LFO_VIB=vib(4), LFO_RATE=lfo(.7), LFO_SHAPE=.1, LFO_FADE=0, F_CUTOFF=cut(3500), AMP_A=ea(.3), AMP_S=1.0, AMP_R=ed(.6))
fx('Impact', A_LEVEL=.9, A_SHAPE=SIN, A_COARSE=co(-12), PITCH_AMT=pen(24), PITCH_DEC=pdec(.25), NOISE_LEVEL=.5, NOISE_COLOR=.3, NOISE_TRANS=1, NOISE_TDEC=.5, AMP_D=ed(2.0), AMP_S=0, AMP_R=ed(1.5), REV_MIX=.6, REV_SIZE=.95)
fx('Rumble', NOISE_LEVEL=.9, NOISE_COLOR=.05, F_CUTOFF=cut(160), F_SLOPE=1.0, SUB_LEVEL=.4, AMP_A=ea(.6), AMP_S=1.0, AMP_R=ed(1.5), LFO_TREM=.3, LFO_RATE=lfo(.5))
fx('Glitch', A_LEVEL=.7, A_SHAPE=PUL, F_CUTOFF=cut(1500), F_RES=.7, LFO_FILT=.9, LFO_RATE=lfo(14), LFO_SHAPE=1.0, LFO_FADE=0, DIST_AMT=.2, AMP_S=1.0, REV_MIX=.1)
fx('Whoosh', NOISE_LEVEL=.9, NOISE_COLOR=.6, F_MODE=BP, F_CUTOFF=cut(500), F_RES=.35, F_ENVAMT=fo(4.5), FE_A=ea(.45), FE_D=fdec(.7), FE_S=0, AMP_A=ea(.3), AMP_D=ed(.8), AMP_S=0, AMP_R=ed(.5), REV_MIX=.45)

# =============================================================================================
# DRUMS / PERCUSSION  (play on any key; pitch follows the key)
# =============================================================================================
def drum(name, root=48, **kw):
    d = dict(A_SHAPE=SIN, A_LEVEL=.9, AMP_A=0, AMP_D=ed(.3), AMP_S=0, AMP_R=ed(.08), AMP_VEL=.8, F_KEYTRK=.5)
    d.update(kw); add('Drums & Perc', name, root, **d)
drum('Kick Sub Boom', 36, PITCH_AMT=pen(30), PITCH_DEC=pdec(.045), AMP_D=ed(.7), NOISE_LEVEL=.15, NOISE_TRANS=1, NOISE_TDEC=.12)
drum('Kick Punch', 36, PITCH_AMT=pen(36), PITCH_DEC=pdec(.03), AMP_D=ed(.35), NOISE_LEVEL=.35, NOISE_TRANS=1, NOISE_TDEC=.1, F_CUTOFF=cut(9000), DIST_AMT=.15)
drum('Kick Hard', 36, A_SHAPE=.1, PITCH_AMT=pen(34), PITCH_DEC=pdec(.035), AMP_D=ed(.4), DIST_AMT=.45, NOISE_LEVEL=.3, NOISE_TRANS=1, NOISE_TDEC=.08)
drum('Snare Analog', 48, A_SHAPE=TRI, A_LEVEL=.55, PITCH_AMT=pen(12), PITCH_DEC=pdec(.04), NOISE_LEVEL=.75, NOISE_COLOR=.65, AMP_D=ed(.22), F_CUTOFF=cut(7500), F_RES=.1, REV_MIX=.1)
drum('Snare Noise', 48, A_LEVEL=.2, NOISE_LEVEL=.9, NOISE_COLOR=.6, AMP_D=ed(.28), F_MODE=.4, F_CUTOFF=cut(3500), F_RES=.2, REV_MIX=.18)
drum('Clap', 60, A_LEVEL=0, NOISE_LEVEL=.9, NOISE_COLOR=.55, F_MODE=BP, F_CUTOFF=cut(1500), F_RES=.35, AMP_D=ed(.25), NOISE_TRANS=.6, NOISE_TDEC=.3, REV_MIX=.3, DLY_MIX=.12, DLY_TIME=dly(.03))
drum('Hat Closed', 60, A_LEVEL=0, NOISE_LEVEL=.85, NOISE_COLOR=1.0, F_MODE=HP, F_CUTOFF=cut(7500), AMP_D=ed(.05), B_LEVEL=.3, B_SHAPE=PUL, B_COARSE=co(24), B_RING=.5)
drum('Hat Open', 60, A_LEVEL=0, NOISE_LEVEL=.85, NOISE_COLOR=1.0, F_MODE=HP, F_CUTOFF=cut(7000), AMP_D=ed(.4), AMP_R=ed(.25), B_LEVEL=.3, B_SHAPE=PUL, B_COARSE=co(24), B_RING=.5)
drum('Tom Low', 41, PITCH_AMT=pen(14), PITCH_DEC=pdec(.07), AMP_D=ed(.6), NOISE_LEVEL=.1, NOISE_TRANS=1, NOISE_TDEC=.1)
drum('Tom High', 55, A_SHAPE=.1, PITCH_AMT=pen(10), PITCH_DEC=pdec(.05), AMP_D=ed(.3), NOISE_LEVEL=.1, NOISE_TRANS=1, NOISE_TDEC=.1)
drum('Rim Shot', 60, A_SHAPE=PUL, A_LEVEL=.5, NOISE_LEVEL=.4, NOISE_COLOR=.7, F_MODE=BP, F_CUTOFF=cut(1800), F_RES=.4, AMP_D=ed(.05))
drum('Cowbell', 60, A_SHAPE=PUL, A_LEVEL=.6, B_SHAPE=PUL, B_LEVEL=.6, B_COARSE=co(7), F_MODE=BP, F_CUTOFF=cut(2600), F_RES=.3, AMP_D=ed(.3), DIST_AMT=.05)
drum('Conga', 55, A_SHAPE=TRI, PITCH_AMT=pen(5), PITCH_DEC=pdec(.04), AMP_D=ed(.22), NOISE_LEVEL=.25, NOISE_TRANS=1, NOISE_TDEC=.1, F_CUTOFF=cut(4000))
drum('Perc Zap', 60, A_SHAPE=SIN, PITCH_AMT=pen(30), PITCH_DEC=pdec(.08), AMP_D=ed(.15), A_FM=.3, B_SHAPE=SIN, B_COARSE=co(7), B_LEVEL=0, REV_MIX=.15)

# =============================================================================================
# VOCAL / FORMANT
# =============================================================================================
def vox(name, **kw):
    d = dict(A_SHAPE=SAW, A_LEVEL=.8, A_UNI=uni(2), A_UNIDET=udet(5), VOWEL_MIX=1.0, VOWEL_Q=.6, F_CUTOFF=cut(12000), AMP_A=ea(.12), AMP_S=.9,
             AMP_R=ed(.35), LFO_VIB=vib(.12), LFO_RATE=lfo(5.5), LFO_FADE=.5, CHO_MIX=.3, REV_MIX=.3)
    d.update(kw); add('Vocal', name, 60, **d)
vox('Vowel Ah', VOWEL_POS=0.0)
vox('Vowel Eh', VOWEL_POS=.25)
vox('Vowel Ee', VOWEL_POS=.5)
vox('Vowel Oh', VOWEL_POS=.75)
vox('Vowel Oo', VOWEL_POS=1.0, VOWEL_Q=.5)
vox('Robot Voice', A_SHAPE=PUL, A_UNI=uni(1), VOWEL_POS=.15, LFO_VIB=0, CHO_MIX=0, COMB_MIX=.5, COMB_FB=.8, REV_MIX=.1)
vox('Formant Lead', A_UNI=uni(1), VOWEL_POS=.3, LFO_VIB=vib(.25), AMP_A=ea(.02), DLY_MIX=.15)
vox('Throat Growl', A_UNI=uni(3), A_UNIDET=udet(12), VOWEL_POS=.1, VOWEL_Q=.85, DIST_AMT=.25, LFO_FILT=.2, LFO_RATE=lfo(4), A_COARSE=co(-12))

# =============================================================================================
# TEXTURE / AMBIENT
# =============================================================================================
def tex(name, **kw):
    d = dict(AMP_A=ea(2.0), AMP_S=1.0, AMP_D=ed(1.0), AMP_R=ed(3.0), REV_MIX=.55, REV_SIZE=.95)
    d.update(kw); add('Texture', name, 48, **d)
tex('Drone', A_UNI=uni(3), A_UNIDET=udet(5), B_SHAPE=TRI, B_COARSE=co(7), B_LEVEL=.4, SUB_LEVEL=.3, F_CUTOFF=cut(900), LFO_FILT=.15, LFO_RATE=lfo(.12), LFO_SHAPE=.1)
tex('Dark Drone', A_SHAPE=SAW, SUB_LEVEL=.5, B_SHAPE=SAW, B_LEVEL=.4, B_COARSE=co(-5), F_CUTOFF=cut(380), F_RES=.35, LFO_FILT=.3, LFO_RATE=lfo(.1), OUT_TONE=.5)
tex('Shimmer', A_LEVEL=.2, C_LEVEL=.6, C_INHARM=.2, C_TILT=.65, C_COUNT=.5, C_OCT=.75, CHO_MIX=.5, DLY_MIX=.3, DLY_TIME=dly(.45), DLY_FB=.6, F_CUTOFF=cut(12000))
tex('Granular Haze', A_LEVEL=.25, NOISE_LEVEL=.5, NOISE_TONAL=.5, NOISE_COLOR=.55, LFO_FILT=.4, LFO_RATE=lfo(7), LFO_SHAPE=1.0, F_CUTOFF=cut(3000), F_RES=.4, LFO_FADE=0)
tex('Ghost', A_SHAPE=SIN, A_UNI=uni(3), A_UNIDET=udet(30), VOWEL_MIX=.6, VOWEL_POS=.9, VOWEL_Q=.7, LFO_VIB=vib(.4), LFO_RATE=lfo(.3), LFO_FADE=0, DLY_MIX=.35, DLY_TIME=dly(.5), DLY_FB=.6)
tex('Tape Hiss Pad', A_SHAPE=TRI, NOISE_LEVEL=.45, NOISE_COLOR=.6, F_CUTOFF=cut(2500), CHO_MIX=.5, LFO_VIB=vib(.1), LFO_RATE=lfo(.5), DIST_AMT=.08, OUT_TONE=.7)
tex('Noise Wash', A_LEVEL=0, NOISE_LEVEL=.9, NOISE_COLOR=.5, F_MODE=BP, F_CUTOFF=cut(1200), F_RES=.4, LFO_FILT=.6, LFO_RATE=lfo(.2), LFO_SHAPE=.1, LFO_FADE=0)
tex('Breathing', A_SHAPE=TRI, A_LEVEL=.5, NOISE_LEVEL=.4, NOISE_TONAL=.7, LFO_TREM=.8, LFO_RATE=lfo(.25), LFO_SHAPE=0, LFO_FADE=0, F_CUTOFF=cut(2500), REV_MIX=.45)

# =============================================================================================
# CHIP / RETRO
# =============================================================================================
def chip(name, **kw):
    d = dict(A_SHAPE=PUL, A_LEVEL=.8, F_CUTOFF=cut(16000), AMP_A=0, AMP_D=ed(.3), AMP_S=.8, AMP_R=ed(.03), AMP_VEL=.2, F_KEYTRK=1.0)
    d.update(kw); add('Chip & Retro', name, 69, **d)
chip('Chip Square', A_PW=.5)
chip('Chip Lead', A_PW=.25, LFO_VIB=vib(.3), LFO_RATE=lfo(6), LFO_FADE=.3)
chip('Chip Bass', A_PW=.5, A_COARSE=co(-12), AMP_S=.9)
chip('Chip Noise Hat', A_LEVEL=0, NOISE_LEVEL=.8, NOISE_COLOR=1.0, F_MODE=HP, F_CUTOFF=cut(6000), AMP_D=ed(.06), AMP_S=0)
chip('8-bit Triangle', A_SHAPE=TRI, A_LEVEL=.85)
chip('Handheld Pulse', A_PW=.125 , AMP_D=ed(.4), AMP_S=.6)
chip('Micro Pulse', A_PW=.35, B_SHAPE=PUL, B_PW=.65, B_LEVEL=.5, B_FINE=fine(4), LFO_RATE=lfo(1.5), F_CUTOFF=cut(7000), F_MODE=.1)
chip('8bit Arp', A_PW=.5, AMP_D=ed(.08), AMP_S=0, DLY_MIX=.2, DLY_TIME=dly(.12), DLY_FB=.4)

# --------------------------------------------------------------------------------------------
names = [p[1] for p in PATCHES]
assert len(names) == len(set(names)), [n for n in names if names.count(n) > 1]
assert len(PATCHES) == 200, len(PATCHES)
for _c, _n, _r, _d in PATCHES:
    for _k, _v in _d.items():
        assert 0.0 <= _v <= 1.0, 'parameter out of range: %s %s=%s' % (_n, _k, _v)

# ---------------------------------------------------------------------------------------------
levels = json.load(open(LEVELS)) if os.path.exists(LEVELS) else {}
def f(v):
    s = ('%.4f' % v).rstrip('0')
    if s.endswith('.'): s += '0'
    return s + 'f'

with open(OUT, 'w') as o:
    o.write('// SPDX-License-Identifier: MIT\n')
    o.write('// GENERATED by tools/gen_factory_presets.py -- edit the script, not this file.\n')
    o.write('// %d factory patches. Values are normalised 0..1 engine parameters; unlisted parameters keep their defaults.\n' % len(PATCHES))
    o.write('#pragma once\n\n#include "Presets.h"\n\nnamespace kraken\n{\n')
    o.write('struct FactoryDef\n{\n    const char* name;\n    const char* category;\n    int root;       // MIDI note the patch is meant to be played around\n    float level;    // loudness-calibrated output level\n    std::vector<Set> sets;\n};\n\n')
    o.write('inline const std::vector<FactoryDef>& factoryDefs()\n{\n    static const std::vector<FactoryDef> v = {\n')
    for cat, name, root, d in PATCHES:
        lv = levels.get(name, 0.5)
        items = ', '.join('{P::%s,%s}' % (k, f(d[k])) for k in NAMES if k in d)
        o.write('        { "%s", "%s", %d, %s, { %s } },\n' % (name, cat, root, f(lv), items))
    o.write('    };\n    return v;\n}\n\n')
    o.write('inline Patch factoryPatch (const FactoryDef& d)\n{\n    Patch p = defaultPatch();\n    for (const auto& s : d.sets) p.p[(size_t) s.idx] = s.v;\n    p.level = d.level;\n    return p;\n}\n\n} // namespace kraken\n')
print('wrote %s with %d patches' % (OUT, len(PATCHES)))

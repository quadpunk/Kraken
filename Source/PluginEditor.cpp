#include "PluginEditor.h"

using namespace kraken;

namespace
{
juce::Font monoFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

juce::String noteName (int n)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
}

bool sameAs (const Patch& a, const Patch& b)
{
    for (int i = 0; i < kNumParams; ++i)
        if (std::abs (a.p[(size_t) i] - b.p[(size_t) i]) > 0.01f) return false;
    return true;
}

void drawPiece (juce::Graphics& g, int kind, juce::Point<float> pos, float rot, float su)
{
    const auto white = juce::Colours::white;
    const auto black = juce::Colours::black;
    juce::Graphics::ScopedSaveState state (g);
    g.addTransform (juce::AffineTransform::scale (su).rotated (rot).translated (pos.x, pos.y));

    auto box = [&] (float x, float y, float w, float h)
    {
        g.setColour (black); g.fillRect (x, y, w, h);
        g.setColour (white); g.drawRect (x, y, w, h, 1.6f);
    };

    switch (kind)
    {
        case 0: // plank
            box (-24.0f, -4.0f, 48.0f, 8.0f);
            g.fillEllipse (-20.0f, -1.5f, 3.0f, 3.0f);
            g.fillEllipse (17.0f, -1.5f, 3.0f, 3.0f);
            break;
        case 1: // barrel
            g.setColour (black); g.fillEllipse (-10.0f, -12.0f, 20.0f, 24.0f);
            g.setColour (white); g.drawEllipse (-10.0f, -12.0f, 20.0f, 24.0f, 1.6f);
            g.drawLine (-9.5f, -5.0f, 9.5f, -5.0f, 1.4f);
            g.drawLine (-9.5f, 5.0f, 9.5f, 5.0f, 1.4f);
            break;
        case 2: // crate
            box (-11.0f, -11.0f, 22.0f, 22.0f);
            g.drawLine (-11.0f, -11.0f, 11.0f, 11.0f, 1.2f);
            g.drawLine (11.0f, -11.0f, -11.0f, 11.0f, 1.2f);
            break;
        case 3: // life ring
            g.setColour (white); g.drawEllipse (-11.0f, -11.0f, 22.0f, 22.0f, 5.0f);
            g.setColour (black); g.drawEllipse (-6.5f, -6.5f, 13.0f, 13.0f, 1.0f);
            break;
        case 4: // torn sail
        {
            juce::Path p;
            p.startNewSubPath (-14.0f, 10.0f);
            p.lineTo (14.0f, 10.0f);
            p.lineTo (-2.0f, -14.0f);
            p.closeSubPath();
            g.setColour (black); g.fillPath (p);
            g.setColour (white); g.strokePath (p, juce::PathStrokeType (1.5f));
            g.drawLine (-6.0f, 6.0f, 4.0f, 6.0f, 1.0f);
            g.drawLine (-3.0f, 0.0f, 2.0f, 0.0f, 1.0f);
            break;
        }
        case 5: // broken mast
            box (-3.0f, -28.0f, 6.0f, 56.0f);
            box (-14.0f, -18.0f, 28.0f, 4.0f);
            break;
        default: // the PATCH sign
            box (-44.0f, -12.0f, 88.0f, 24.0f);
            g.setFont (monoFont (15.0f, true));
            g.setColour (white);
            g.drawText ("PATCH", juce::Rectangle<float> (-44.0f, -12.0f, 88.0f, 24.0f), juce::Justification::centred, false);
            break;
    }
}

constexpr int kHeaderH = 112;
constexpr int kFooterH = 100;
} // namespace

//==============================================================================
BWLookAndFeel::BWLookAndFeel()
{
    setColour (juce::TextButton::buttonColourId, juce::Colours::black);
    setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
}

void BWLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (down ? juce::Colours::white : juce::Colours::black);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (over ? 1.0f : 0.75f));
    g.drawRoundedRectangle (r, 4.0f, over ? 2.0f : 1.5f);
}

void BWLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    g.setFont (monoFont (13.0f, true));
    g.setColour (down ? juce::Colours::black : juce::Colours::white);
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}

juce::Font BWLookAndFeel::getTextButtonFont (juce::TextButton&, int) { return monoFont (13.0f, true); }

//==============================================================================
KrakenView::KrakenView (KrakenProcessor& p) : proc (p)
{
    startTimerHz (30);
}

void KrakenView::timerCallback()
{
    time += 1.0f / 30.0f;
    updateAnimation();
    repaint();
}

//==============================================================================
// sample -> patch animation
void KrakenView::startShip()
{
    shipActive = true;
    shipFinishing = false;
    shipT0 = time;
    shipD = 0.0f;
    debrisActive = false;
    debrisPendingAt = -1.0f;
    debris.clear();
}

void KrakenView::finishShip (bool success)
{
    shipFinishing = true;
    if (success) debrisPendingAt = time + 0.7f;
}

void KrakenView::spawnDebris()
{
    struct L { int k; float x, y, r; float delay, flight; };
    static const L layout[] = {
        { 0, -52.0f,   0.0f, -0.12f, 0.00f, 1.2f }, { 1, -24.0f,  -2.0f,  0.00f, 0.06f, 1.3f },
        { 2,   6.0f,   0.0f,  0.05f, 0.12f, 1.1f }, { 3,  34.0f,  -1.0f,  0.00f, 0.18f, 1.4f },
        { 0,  60.0f,   2.0f,  0.10f, 0.24f, 1.2f }, { 2, -38.0f, -17.0f,  0.15f, 0.30f, 1.3f },
        { 1,  -8.0f, -19.0f,  0.00f, 0.36f, 1.2f }, { 4,  22.0f, -17.0f,  0.10f, 0.42f, 1.4f },
        { 0, -22.0f, -33.0f, -0.30f, 0.48f, 1.3f }, { 5,  14.0f, -38.0f,  0.35f, 0.54f, 1.2f },
        { 6,   0.0f, -54.0f, -0.04f, 0.70f, 1.5f } };

    juce::Random rnd;
    debris.clear();
    for (const auto& l : layout)
    {
        Debris d;
        d.kind = l.k;
        d.off = { l.x, l.y };
        d.rot = l.r;
        d.delay = l.delay + rnd.nextFloat() * 0.05f;
        d.flight = l.flight + rnd.nextFloat() * 0.2f;
        d.apex = 45.0f + rnd.nextFloat() * 45.0f;
        d.spin = (rnd.nextFloat() - 0.5f) * 10.0f;
        d.phase = rnd.nextFloat() * 6.28f;
        debris.push_back (d);
    }
    debrisActive = true;
    debrisT0 = time;
}

void KrakenView::updateAnimation()
{
    const bool an = proc.analysing.load();
    if (an && ! wasAnalysing) startShip();
    if (! an && wasAnalysing && shipActive) finishShip (proc.matchPct > 0.0f);
    wasAnalysing = an;

    if (shipActive)
    {
        float target;
        if (shipFinishing)
            target = 1.0f;
        else
        {
            const float e = std::min (1.0f, (time - shipT0) / 2.0f);
            const float ease = 1.0f - (1.0f - e) * (1.0f - e);
            target = std::min (0.97f, 0.55f * ease + 0.42f * proc.progress.load());
        }
        shipD += (target - shipD) * 0.12f;
        if (shipFinishing && shipD > 0.985f) shipActive = false;
    }

    if (debrisPendingAt >= 0.0f && time >= debrisPendingAt)
    {
        spawnDebris();
        debrisPendingAt = -1.0f;
    }
}

void KrakenView::shipPose (float& x, float& y, float& sc, float& ang, float& alpha) const
{
    const float e = juce::jlimit (0.0f, 1.0f, shipD);
    const float ease = e * e * (3.0f - 2.0f * e);
    const float startY = arena.getY() - 6.0f * scaleU;
    const float endY = headC.y + headRy * 0.65f;
    x = headC.x + std::sin (time * 1.3f) * 6.0f * (1.0f - e);
    y = startY + (endY - startY) * ease;
    sc = scaleU * 0.8f * (1.0f - 0.7f * ease);
    ang = 0.12f * std::sin (time * 2.1f) * (1.0f - e) + 0.9f * ease;
    alpha = e < 0.7f ? 1.0f : juce::jlimit (0.0f, 1.0f, 1.0f - (e - 0.7f) / 0.3f);
}

void KrakenView::drawShip (juce::Graphics& g)
{
    if (! shipActive) return;
    float x, y, sc, ang, alpha;
    shipPose (x, y, sc, ang, alpha);
    if (alpha < 0.01f) return;

    const auto white = juce::Colours::white;
    const auto black = juce::Colours::black;
    const bool layer = alpha < 0.999f;
    if (layer) g.beginTransparencyLayer (alpha);
    {
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::scale (sc).rotated (ang).translated (x, y));

        // sails + mast
        g.setColour (white);
        g.drawLine (0.0f, -2.0f, 0.0f, -54.0f, 2.5f);
        juce::Path sail;
        sail.startNewSubPath (3.0f, -50.0f);
        sail.quadraticTo (36.0f, -32.0f, 30.0f, -8.0f);
        sail.lineTo (3.0f, -8.0f);
        sail.closeSubPath();
        g.setColour (black); g.fillPath (sail);
        g.setColour (white); g.strokePath (sail, juce::PathStrokeType (1.8f));
        for (int k = 0; k < 3; ++k)
            g.drawLine (6.0f, -40.0f + (float) k * 10.0f, 24.0f - (float) k * 2.0f, -36.0f + (float) k * 10.0f, 1.0f);
        juce::Path jib;
        jib.startNewSubPath (-3.0f, -46.0f);
        jib.lineTo (-3.0f, -12.0f);
        jib.lineTo (-26.0f, -12.0f);
        jib.closeSubPath();
        g.setColour (black); g.fillPath (jib);
        g.setColour (white); g.strokePath (jib, juce::PathStrokeType (1.8f));
        juce::Path flag;
        flag.startNewSubPath (0.0f, -54.0f);
        flag.lineTo (15.0f, -58.0f);
        flag.lineTo (0.0f, -62.0f);
        flag.closeSubPath();
        g.setColour (white); g.fillPath (flag);

        // hull with the word SAMPLE
        juce::Path hull;
        hull.startNewSubPath (-54.0f, -2.0f);
        hull.lineTo (54.0f, -2.0f);
        hull.quadraticTo (48.0f, 24.0f, 26.0f, 26.0f);
        hull.lineTo (-26.0f, 26.0f);
        hull.quadraticTo (-48.0f, 24.0f, -54.0f, -2.0f);
        hull.closeSubPath();
        g.setColour (black); g.fillPath (hull);
        g.setColour (white); g.strokePath (hull, juce::PathStrokeType (2.2f));
        g.setFont (monoFont (12.0f, true));
        g.drawText ("SAMPLE", juce::Rectangle<float> (-44.0f, 3.0f, 88.0f, 20.0f), juce::Justification::centred, false);
    }
    if (layer) g.endTransparencyLayer();
}

void KrakenView::drawDebris (juce::Graphics& g)
{
    if (! debrisActive) return;
    const float su = scaleU;
    const float tt = time - debrisT0;
    const juce::Point<float> pile (headC.x + headRx * 2.15f, arena.getY() + 28.0f);
    const juce::Point<float> mouth (headC.x, headC.y + headRy * 0.65f);

    if (tt > 0.8f)
        for (int k = 0; k < 2; ++k)
        {
            const float w = (150.0f + (float) k * 40.0f + 8.0f * std::sin (time * 1.5f + (float) k)) * su;
            g.setColour (juce::Colours::white.withAlpha (0.35f - 0.12f * (float) k));
            g.drawEllipse (pile.x - w * 0.5f, pile.y + 8.0f * su - w * 0.04f, w, w * 0.08f, 1.2f);
        }

    for (const auto& d : debris)
    {
        const float t = tt - d.delay;
        if (t < 0.0f) continue;
        const juce::Point<float> target = pile + d.off * su;
        const float f = std::min (1.0f, t / d.flight);
        juce::Point<float> pos;
        float rot;
        if (f < 1.0f)
        {
            pos = mouth + (target - mouth) * f;
            pos.y -= 4.0f * d.apex * su * f * (1.0f - f);
            rot = d.rot + (1.0f - f) * d.spin;
        }
        else
        {
            pos = target;
            pos.y += std::sin (time * 1.6f + d.phase) * 2.0f * su;
            rot = d.rot + std::sin (time * 1.2f + d.phase) * 0.05f;
        }
        drawPiece (g, d.kind, pos, rot, su);
    }
}

void KrakenView::resized()
{
    const float W = (float) getWidth(), H = (float) getHeight();
    const float s = std::min (W / 980.0f, H / 720.0f);
    scaleU = s;
    arena = { 0.0f, (float) kHeaderH, W, H - (float) kHeaderH - (float) kFooterH };

    headRx = 95.0f * s;
    headRy = 80.0f * s;
    headC = { W * 0.5f, arena.getY() + 12.0f + headRy + 8.0f * s };

    const float margin = 24.0f;
    const float cell = (W - 2.0f * margin) / (float) kNumArms;
    inspector = { margin, arena.getBottom() - 86.0f, W - 2.0f * margin, 82.0f };
    const float zoneTop = headC.y + headRy + 30.0f * s;
    const float zoneBottom = inspector.getY() - 24.0f;
    for (int i = 0; i < kNumArms; ++i)
    {
        zones[(size_t) i] = { margin + cell * (float) i + 4.0f, zoneTop, cell - 8.0f, zoneBottom - zoneTop };
        const float dx = ((float) i - 3.5f) / 3.5f * headRx * 0.80f;
        const float k = std::sqrt (std::max (0.0f, 1.0f - (dx / headRx) * (dx / headRx)));
        anchors[(size_t) i] = { headC.x + dx, headC.y + headRy * k * 0.97f };
    }

    const float by = (float) getHeight() - 52.0f;
    for (int k = 0; k < 6; ++k)
        kinRects[(size_t) k] = juce::Rectangle<float> (38.0f, 38.0f).withCentre ({ W - 34.0f - (float) (5 - k) * 50.0f, by });
    waveRect = { margin, (float) getHeight() - 82.0f, kinRects[0].getX() - margin - 24.0f, 60.0f };
    layoutInspector();
}

void KrakenView::layoutInspector()
{
    const int n = kArmCount[selArm];
    const float cw = inspector.getWidth() / 7.0f;
    for (int j = 0; j < n; ++j)
        cells[(size_t) j] = { inspector.getX() + (float) (j % 7) * cw, inspector.getY() + 20.0f + (float) (j / 7) * 30.0f,
                              cw - 12.0f, 26.0f };
}

void KrakenView::setParamFromCell (int param, float x)
{
    const auto& cell = cells[(size_t) (param - kArmFirst[selArm])];
    proc.setParam (param, juce::jlimit (0.0f, 1.0f, (x - cell.getX()) / cell.getWidth()));
}

juce::Point<float> KrakenView::tipPos (const Patch& p, int a) const
{
    const auto& z = zones[(size_t) a];
    const int f = kArmFirst[a];
    return { z.getX() + p.p[(size_t) f] * z.getWidth(),
             z.getY() + (1.0f - p.p[(size_t) (f + 1)]) * z.getHeight() };
}

//==============================================================================
void KrakenView::paint (juce::Graphics& g)
{
    const auto white = juce::Colours::white;
    const float W = (float) getWidth();
    g.fillAll (juce::Colours::black);
    const auto patch = proc.getPatch();

    // sea surface
    for (int k = 0; k < 3; ++k)
    {
        juce::Path p;
        const float y0 = arena.getY() + 6.0f + (float) k * 9.0f;
        p.startNewSubPath (0.0f, y0);
        for (float x = 0.0f; x <= W; x += 8.0f)
            p.lineTo (x, y0 + std::sin (x * 0.035f + time * (1.2f + (float) k * 0.3f) + (float) k) * 3.0f);
        g.setColour (white.withAlpha (0.26f - 0.08f * (float) k));
        g.strokePath (p, juce::PathStrokeType (1.2f));
    }

    // bubbles
    for (int i = 0; i < 16; ++i)
    {
        const float r = std::fmod (std::sin ((float) i * 12.9898f) * 43758.5453f, 1.0f);
        const float rr = std::abs (r);
        const float x = rr * W;
        const float rise = std::fmod (time * (14.0f + rr * 22.0f) + rr * arena.getHeight(), arena.getHeight());
        const float y = arena.getBottom() - rise;
        const float rad = 2.0f + rr * 5.0f;
        g.setColour (white.withAlpha (0.28f));
        g.drawEllipse (x + std::sin (time + (float) i) * 6.0f - rad, y - rad, rad * 2.0f, rad * 2.0f, 1.0f);
    }

    drawHeader (g);

    // tide pools (drag zones)
    for (int i = 0; i < kNumArms; ++i)
    {
        const auto& z = zones[(size_t) i];
        const bool active = (i == hoverArm || i == dragArm || i == selArm);
        juce::Path rp, dashed;
        rp.addRoundedRectangle (z, 8.0f);
        const float dl[] = { 3.0f, 5.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, rp, dl, 2);
        g.setColour (white.withAlpha (active ? 0.7f : 0.22f));
        g.strokePath (dashed, juce::PathStrokeType (1.0f));

        g.setColour (white.withAlpha (active ? 1.0f : 0.55f));
        g.setFont (monoFont (11.0f, true));
        g.drawText (armName (i), z.withTrimmedTop (z.getHeight() - 16.0f).toNearestInt().toFloat(),
                    juce::Justification::centred, false);
        g.setFont (monoFont (9.0f));
        g.setColour (white.withAlpha (active ? 0.9f : 0.35f));
        g.drawText (juce::String (paramShort (kArmFirst[i])) + " >", z.withHeight (14.0f).translated (0.0f, z.getHeight() - 30.0f),
                    juce::Justification::centredRight, false);
        g.drawText ("^ " + juce::String (paramShort (kArmFirst[i] + 1)), z.withHeight (14.0f).translated (0.0f, 2.0f),
                    juce::Justification::centredLeft, false);
    }

    // tentacles gripping the ship
    if (shipActive && shipD < 0.8f)
    {
        float sx, sy, ssc, sang, sal;
        shipPose (sx, sy, ssc, sang, sal);
        for (int side = -1; side <= 1; side += 2)
        {
            const float sd = (float) side;
            const juce::Point<float> p0 (headC.x + sd * headRx * 0.85f, headC.y - headRy * 0.1f);
            const juce::Point<float> p3 (sx + sd * 44.0f * ssc, sy + 6.0f * ssc);
            drawTentacle (g, p0, p3, sd, sd * 3.0f, 0.85f, false);
        }
    }

    // arms (behind head)
    for (int i = 0; i < kNumArms; ++i)
        drawArm (g, i, tipPos (patch, i), i == hoverArm || i == dragArm);

    drawHead (g);

    // tip handles
    for (int i = 0; i < kNumArms; ++i)
    {
        const auto t = tipPos (patch, i);
        const bool active = (i == hoverArm || i == dragArm);
        const float r = active ? 11.0f : 8.0f;
        g.setColour (white);
        g.fillEllipse (t.x - r, t.y - r, r * 2.0f, r * 2.0f);
        g.setColour (juce::Colours::black);
        g.fillEllipse (t.x - r * 0.45f, t.y - r * 0.45f, r * 0.9f, r * 0.9f);
    }

    drawShip (g);
    drawDebris (g);

    // hover read-out
    const int showArm = dragArm >= 0 ? dragArm : hoverArm;
    if (showArm >= 0)
    {
        const int f = kArmFirst[showArm];
        g.setColour (white);
        g.setFont (monoFont (12.0f, true));
        g.drawText (juce::String (armName (showArm)) + "   "
                        + paramShort (f) + " " + juce::String (juce::roundToInt (patch.p[(size_t) f] * 100.0f)) + "%   "
                        + paramShort (f + 1) + " " + juce::String (juce::roundToInt (patch.p[(size_t) (f + 1)] * 100.0f)) + "%",
                    juce::Rectangle<float> (0.0f, inspector.getY() - 22.0f, W, 16.0f), juce::Justification::centred, false);
    }

    drawInspector (g, patch);
    drawBottom (g, patch);
}

void KrakenView::drawHeader (juce::Graphics& g)
{
    const auto white = juce::Colours::white;
    g.setColour (white);
    g.setFont (monoFont (36.0f, true));
    g.drawText ("kr@ken", 24, 10, 220, 44, juce::Justification::centredLeft, false);
    g.setFont (monoFont (10.0f));
    g.setColour (white.withAlpha (0.55f));
    g.drawText ("feed it a sound. it hunts down the patch.", 26, 52, 320, 14, juce::Justification::centredLeft, false);

    juce::String status;
    if (proc.analysing.load())
        status = "HUNTING...  " + juce::String (juce::roundToInt (proc.progress.load() * 100.0f)) + "%   "
                 + proc.sampleName;
    else if (proc.sampleName.isNotEmpty() && proc.matchPct > 0.0f)
        status = proc.sampleName + "   root " + noteName (proc.rootNote.load())
                 + "   match " + juce::String (juce::roundToInt (proc.matchPct)) + "%";
    else
        status = proc.statusMessage;

    g.setColour (white);
    g.setFont (monoFont (12.0f, true));
    g.drawText (status, 24, 78, getWidth() / 2 - 40, 16, juce::Justification::centredLeft, false);

    if (proc.analysing.load())
    {
        const juce::Rectangle<float> bar (24.0f, 98.0f, 300.0f, 5.0f);
        g.setColour (white.withAlpha (0.5f));
        g.drawRect (bar, 1.0f);
        g.setColour (white);
        g.fillRect (bar.withWidth (bar.getWidth() * proc.progress.load()));
    }

    g.setColour (white.withAlpha (0.55f));
    g.setFont (monoFont (10.0f));
    g.drawText ("scroll = level " + juce::String (juce::roundToInt (proc.getLevel() * 100.0f)) + "%   dbl-click = reset",
                24, getHeight() - 98, 360, 12, juce::Justification::centredLeft, false);
}

void KrakenView::drawHead (juce::Graphics& g)
{
    const auto white = juce::Colours::white;
    const auto black = juce::Colours::black;
    const float cx = headC.x, cy = headC.y, rx = headRx, ry = headRy;

    // sharp curved horns
    for (int side = -1; side <= 1; side += 2)
    {
        const float sd = (float) side;
        juce::Path horn;
        horn.startNewSubPath (cx + sd * rx * 0.30f, cy - ry * 0.93f);
        horn.quadraticTo (cx + sd * rx * 0.70f, cy - ry * 1.05f, cx + sd * rx * 1.05f, cy - ry * 1.62f);
        horn.quadraticTo (cx + sd * rx * 0.88f, cy - ry * 0.95f, cx + sd * rx * 0.72f, cy - ry * 0.70f);
        horn.closeSubPath();
        g.setColour (black);
        g.fillPath (horn);
        g.setColour (white);
        g.strokePath (horn, juce::PathStrokeType (2.0f, juce::PathStrokeType::mitered));
        for (int k = 1; k <= 3; ++k)   // ridges
        {
            const float t = 0.25f * (float) k;
            const float x0 = cx + sd * rx * (0.42f + 0.40f * t), y0 = cy - ry * (0.90f + 0.45f * t);
            g.drawLine (x0, y0, x0 + sd * rx * 0.07f, y0 + ry * 0.10f, 1.2f);
        }
    }

    const juce::Rectangle<float> head (cx - rx, cy - ry, rx * 2.0f, ry * 2.0f);
    g.setColour (black);
    g.fillEllipse (head);

    {   // hatching shade
        juce::Graphics::ScopedSaveState state (g);
        juce::Path ep;
        ep.addEllipse (head);
        g.reduceClipRegion (ep);
        g.reduceClipRegion (juce::Rectangle<int> ((int) (cx - rx), (int) (cy + ry * 0.30f), (int) (rx * 2.0f), (int) ry));
        g.setColour (white.withAlpha (0.40f));
        for (float x = cx - rx - ry; x < cx + rx + ry; x += 7.0f)
            g.drawLine (x, cy + ry * 1.1f, x + ry * 0.9f, cy + ry * 0.2f, 1.0f);
    }

    g.setColour (white);
    g.drawEllipse (head, 2.5f);

    // narrow slanted eyes with slit pupils, heavy lids and a V-shaped scowl
    for (int side = -1; side <= 1; side += 2)
    {
        const float sd = (float) side;
        const juce::Point<float> ec (cx + sd * rx * 0.40f, cy - ry * 0.02f);
        const float ew = rx * 0.46f, eh = ry * 0.30f;
        const juce::Point<float> inner (ec.x - sd * ew * 0.5f, ec.y + eh * 0.25f);
        const juce::Point<float> outer (ec.x + sd * ew * 0.5f, ec.y - eh * 0.35f);
        const juce::Point<float> mid = (inner + outer) * 0.5f;

        juce::Path eye;
        eye.startNewSubPath (inner);
        eye.quadraticTo (mid.x, mid.y - eh * 0.05f, outer.x, outer.y);
        eye.quadraticTo (mid.x, mid.y + eh * 1.0f, inner.x, inner.y);
        eye.closeSubPath();

        g.setColour (white);
        g.fillPath (eye);
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (eye);
            auto d = mousePos - mid;
            const float len = std::max (1.0f, d.getDistanceFromOrigin());
            d = d * (std::min (len, 60.0f) / len);
            const float px = mid.x + d.x / 60.0f * ew * 0.22f;
            const float py = mid.y + d.y / 60.0f * eh * 0.12f + eh * 0.15f;
            g.setColour (black);
            g.fillEllipse (px - 3.2f, py - eh * 0.8f, 6.4f, eh * 1.6f);
        }
        g.setColour (white);
        g.strokePath (eye, juce::PathStrokeType (2.0f));

        juce::Path lid;                                   // thick heavy upper lid
        lid.startNewSubPath (inner);
        lid.quadraticTo (mid.x, mid.y - eh * 0.05f, outer.x, outer.y);
        g.strokePath (lid, juce::PathStrokeType (4.5f));

        // brow: inner end low, outer end high = scowl
        g.drawLine (ec.x - sd * ew * 0.55f, ec.y - eh * 0.60f, ec.x + sd * ew * 0.85f, ec.y - eh * 1.65f, 5.0f);

        if (side < 0)                                     // old battle scar over the left eye
        {
            const juce::Point<float> a (ec.x - ew * 0.10f, ec.y - eh * 2.0f), b (ec.x + ew * 0.30f, ec.y + eh * 1.7f);
            g.drawLine (a.x, a.y, b.x, b.y, 2.0f);
            for (int k = 1; k <= 3; ++k)
            {
                const auto p = a + (b - a) * (0.18f + 0.22f * (float) k);
                g.drawLine (p.x - 5.0f, p.y - 2.0f, p.x + 5.0f, p.y + 2.0f, 1.5f);
            }
        }
    }

    // lopsided smirk full of teeth
    const float my = cy + ry * 0.52f;
    const juce::Point<float> ml (cx - rx * 0.52f, my - ry * 0.02f), mr (cx + rx * 0.52f, my - ry * 0.12f);
    const juce::Point<float> topC (cx, my + ry * 0.08f), botC (cx + rx * 0.03f, my + ry * 0.62f);
    juce::Path mouth;
    mouth.startNewSubPath (ml);
    mouth.quadraticTo (topC.x, topC.y, mr.x, mr.y);
    mouth.quadraticTo (botC.x, botC.y, ml.x, ml.y);
    mouth.closeSubPath();
    g.setColour (black);
    g.fillPath (mouth);
    g.setColour (white);
    g.strokePath (mouth, juce::PathStrokeType (2.5f));
    for (int k = 1; k <= 9; ++k)
    {
        const float t = (float) k / 10.0f, u = 1.0f - t;
        const juce::Point<float> base = ml * (u * u) + topC * (2.0f * u * t) + mr * (t * t);
        const float th = ((k & 1) ? 0.14f : 0.09f) * ry, tw = rx * 0.036f;
        juce::Path tooth;
        tooth.startNewSubPath (base.x - tw, base.y);
        tooth.lineTo (base.x + tw, base.y);
        tooth.lineTo (base.x, base.y + th);
        tooth.closeSubPath();
        g.fillPath (tooth);
    }
}

void KrakenView::drawArm (juce::Graphics& g, int i, juce::Point<float> tip, bool highlight)
{
    drawTentacle (g, anchors[(size_t) i], tip, (i % 2 == 0) ? 1.0f : -1.0f, (float) i, 1.0f, highlight);
}

void KrakenView::drawTentacle (juce::Graphics& g, juce::Point<float> p0, juce::Point<float> p3,
                               float sgn, float phase, float ws, bool highlight)
{
    const auto v = p3 - p0;
    const float len = std::max (1.0f, v.getDistanceFromOrigin());
    const juce::Point<float> nrm (-v.y / len, v.x / len);
    const float wob1 = std::sin (time * 1.4f + phase * 1.9f) * 0.10f * len;
    const float wob2 = std::sin (time * 1.1f + phase * 2.7f + 1.0f) * 0.10f * len;
    const auto p1 = p0 + v * 0.33f + nrm * (sgn * 0.20f * len + wob1);
    const auto p2 = p0 + v * 0.68f + nrm * (-sgn * 0.20f * len + wob2);

    constexpr int N = 40;
    auto bez = [&] (float t)
    {
        const float u = 1.0f - t;
        return p0 * (u * u * u) + p1 * (3.0f * u * u * t) + p2 * (3.0f * u * t * t) + p3 * (t * t * t);
    };
    auto tang = [&] (float t)
    {
        const float u = 1.0f - t;
        return (p1 - p0) * (3.0f * u * u) + (p2 - p1) * (6.0f * u * t) + (p3 - p2) * (3.0f * t * t);
    };
    auto hw = [ws] (float t) { return (11.0f * std::pow (1.0f - t, 0.8f) + 2.5f) * ws; };

    std::array<juce::Point<float>, N + 1> left, right;
    for (int k = 0; k <= N; ++k)
    {
        const float t = (float) k / (float) N;
        const auto c = bez (t);
        const auto tg = tang (t);
        const float tl = std::max (0.001f, tg.getDistanceFromOrigin());
        const juce::Point<float> n (-tg.y / tl, tg.x / tl);
        left[(size_t) k] = c + n * hw (t);
        right[(size_t) k] = c - n * hw (t);
    }

    juce::Path path;
    path.startNewSubPath (left[0]);
    for (int k = 1; k <= N; ++k) path.lineTo (left[(size_t) k]);
    for (int k = N; k >= 0; --k) path.lineTo (right[(size_t) k]);
    path.closeSubPath();

    g.setColour (juce::Colours::black);
    g.fillPath (path);
    g.setColour (juce::Colours::white.withAlpha (highlight ? 1.0f : 0.9f));
    g.strokePath (path, juce::PathStrokeType (highlight ? 2.4f : 1.5f, juce::PathStrokeType::curved));

    int idx = 0;
    for (float t = 0.10f; t < 0.93f; t += 0.075f, ++idx)
    {
        const auto c = bez (t);
        const float r = hw (t) * 0.42f;
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.0f);
        if (idx % 2 == 0)
            g.fillEllipse (c.x - r * 0.35f, c.y - r * 0.35f, r * 0.7f, r * 0.7f);
    }
}

void KrakenView::drawInspector (juce::Graphics& g, const Patch& patch)
{
    const auto white = juce::Colours::white;
    g.setColour (white.withAlpha (0.35f));
    g.drawRoundedRectangle (inspector.expanded (4.0f, 2.0f), 6.0f, 1.0f);
    g.setColour (white);
    g.setFont (monoFont (11.0f, true));
    g.drawText (juce::String ("> ") + armName (selArm) + "   (click a tentacle to inspect)",
                inspector.withHeight (16.0f).translated (2.0f, 0.0f), juce::Justification::centredLeft, false);

    const int first = kArmFirst[selArm];
    for (int j = 0; j < kArmCount[selArm]; ++j)
    {
        const int idx = first + j;
        const auto& cell = cells[(size_t) j];
        const float v = patch.p[(size_t) idx];
        const bool active = (idx == hoverParam || idx == dragParam);
        g.setFont (monoFont (10.0f, active));
        g.setColour (white.withAlpha (active ? 1.0f : 0.7f));
        g.drawText (paramShort (idx), cell.withHeight (12.0f), juce::Justification::centredLeft, false);
        g.drawText (juce::String (juce::roundToInt (v * 100.0f)), cell.withHeight (12.0f), juce::Justification::centredRight, false);
        const float y = cell.getBottom() - 6.0f;
        g.setColour (white.withAlpha (0.3f));
        g.drawLine (cell.getX(), y, cell.getRight(), y, 1.0f);
        g.setColour (white);
        g.drawLine (cell.getX(), y, cell.getX() + v * cell.getWidth(), y, active ? 3.0f : 2.0f);
        const float r = active ? 5.0f : 4.0f;
        g.fillEllipse (cell.getX() + v * cell.getWidth() - r, y - r, r * 2.0f, r * 2.0f);
    }
}

void KrakenView::drawBottom (juce::Graphics& g, const Patch& patch)
{
    const auto white = juce::Colours::white;

    // waveform
    g.setColour (white.withAlpha (0.35f));
    g.drawRoundedRectangle (waveRect, 4.0f, 1.0f);
    if (! proc.thumb.empty())
    {
        const float n = (float) proc.thumb.size();
        g.setColour (white);
        for (size_t i = 0; i < proc.thumb.size(); ++i)
        {
            const float x = waveRect.getX() + 3.0f + ((float) i / n) * (waveRect.getWidth() - 6.0f);
            const float h = proc.thumb[i] * (waveRect.getHeight() * 0.5f - 4.0f);
            g.drawLine (x, waveRect.getCentreY() - h, x, waveRect.getCentreY() + h, 1.0f);
        }
    }
    else
    {
        g.setFont (monoFont (11.0f));
        g.setColour (white.withAlpha (0.4f));
        g.drawText ("drop a .wav / .aiff / .flac / .ogg here", waveRect, juce::Justification::centred, false);
    }

    // kin blobs
    g.setFont (monoFont (10.0f, true));
    g.setColour (white.withAlpha (0.6f));
    g.drawText ("KIN", (int) kinRects[0].getX() - 4, (int) kinRects[0].getY() - 16, 60, 12, juce::Justification::left, false);

    for (int k = 0; k < 6; ++k)
    {
        if (k >= (int) proc.kin.size()) continue;
        const auto r = kinRects[(size_t) k];
        const bool selected = sameAs (proc.kin[(size_t) k], patch);
        juce::Path blob;
        const int pts = 28;
        for (int j = 0; j <= pts; ++j)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) j / (float) pts;
            const float rr = r.getWidth() * 0.5f * (1.0f + 0.13f * std::sin (a * 3.0f + time * 1.5f + (float) k)
                                                       + 0.06f * std::sin (a * 5.0f - time + (float) k * 2.0f));
            const juce::Point<float> pt (r.getCentreX() + std::cos (a) * rr, r.getCentreY() + std::sin (a) * rr);
            if (j == 0) blob.startNewSubPath (pt); else blob.lineTo (pt);
        }
        blob.closeSubPath();
        g.setColour (selected ? white : juce::Colours::black);
        g.fillPath (blob);
        g.setColour (white);
        g.strokePath (blob, juce::PathStrokeType (k == hoverKin ? 3.0f : 1.6f));
        g.setColour (selected ? juce::Colours::black : white);
        g.setFont (monoFont (11.0f, true));
        g.drawText (k == 0 ? "*" : juce::String (k), r, juce::Justification::centred, false);
    }
}

//==============================================================================
void KrakenView::mouseMove (const juce::MouseEvent& e)
{
    mousePos = e.position;
    const auto patch = proc.getPatch();
    hoverArm = -1;
    for (int i = 0; i < kNumArms; ++i)
        if (tipPos (patch, i).getDistanceFrom (e.position) < 16.0f) { hoverArm = i; break; }
    hoverKin = -1;
    for (int k = 0; k < 6 && k < (int) proc.kin.size(); ++k)
        if (kinRects[(size_t) k].contains (e.position)) hoverKin = k;
    hoverParam = -1;
    for (int j = 0; j < kArmCount[selArm]; ++j)
        if (cells[(size_t) j].expanded (6.0f, 4.0f).contains (e.position)) hoverParam = kArmFirst[selArm] + j;
    setMouseCursor (hoverArm >= 0 || hoverKin >= 0 || hoverParam >= 0 ? juce::MouseCursor::PointingHandCursor
                                                                      : juce::MouseCursor::NormalCursor);
}

void KrakenView::mouseDown (const juce::MouseEvent& e)
{
    mouseMove (e);
    if (hoverKin >= 0) { proc.applyKin (hoverKin); return; }
    if (hoverParam >= 0)
    {
        dragParam = hoverParam;
        proc.beginGesture (dragParam);
        setParamFromCell (dragParam, e.position.x);
        return;
    }
    int arm = hoverArm;
    if (arm < 0)
        for (int i = 0; i < kNumArms; ++i)
            if (zones[(size_t) i].contains (e.position)) { arm = i; break; }
    if (arm >= 0)
    {
        dragArm = arm;
        if (selArm != arm) { selArm = arm; layoutInspector(); }
        proc.beginGesture (kArmFirst[arm]);
        proc.beginGesture (kArmFirst[arm] + 1);
        mouseDrag (e);
    }
}

void KrakenView::mouseDrag (const juce::MouseEvent& e)
{
    mousePos = e.position;
    if (dragParam >= 0) { setParamFromCell (dragParam, e.position.x); return; }
    if (dragArm < 0) return;
    const auto& z = zones[(size_t) dragArm];
    const float x = juce::jlimit (0.0f, 1.0f, (e.position.x - z.getX()) / z.getWidth());
    const float y = juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - z.getY()) / z.getHeight());
    proc.setParam (kArmFirst[dragArm], x);
    proc.setParam (kArmFirst[dragArm] + 1, y);
}

void KrakenView::mouseUp (const juce::MouseEvent&)
{
    if (dragParam >= 0) { proc.endGesture (dragParam); dragParam = -1; }
    if (dragArm >= 0)
    {
        proc.endGesture (kArmFirst[dragArm]);
        proc.endGesture (kArmFirst[dragArm] + 1);
        dragArm = -1;
    }
}

void KrakenView::mouseDoubleClick (const juce::MouseEvent& e)
{
    mouseMove (e);
    const auto def = defaultPatch();
    auto reset = [&] (int idx)
    {
        proc.beginGesture (idx);
        proc.setParam (idx, def.p[(size_t) idx]);
        proc.endGesture (idx);
    };
    if (hoverParam >= 0) { reset (hoverParam); return; }
    if (hoverArm >= 0)
        for (int j = 0; j < kArmCount[hoverArm]; ++j) reset (kArmFirst[hoverArm] + j);
}

void KrakenView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    proc.setLevel (proc.getLevel() + w.deltaY * 0.15f);
}

//==============================================================================
KrakenEditor::KrakenEditor (KrakenProcessor& p) : juce::AudioProcessorEditor (&p), proc (p), view (p)
{
    setLookAndFeel (&laf);
    addAndMakeVisible (view);
    for (auto* b : { &loadBtn, &playSampleBtn, &playPatchBtn, &mutateBtn, &stopBtn })
    {
        b->setLookAndFeel (&laf);
        addAndMakeVisible (*b);
    }

    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Pick a sample for the kraken", juce::File(),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto f = fc.getResult();
                                  if (f.existsAsFile()) proc.loadSampleFile (f);
                              });
    };
    playSampleBtn.onClick = [this] { proc.sendCommand (1); };
    playPatchBtn.onClick = [this] { proc.sendCommand (2); };
    stopBtn.onClick = [this] { proc.sendCommand (3); };
    mutateBtn.onClick = [this] { proc.mutate(); };

    setResizable (true, true);
    setResizeLimits (820, 620, 1700, 1200);
    setSize (980, 720);
}

KrakenEditor::~KrakenEditor()
{
    for (auto* b : { &loadBtn, &playSampleBtn, &playPatchBtn, &mutateBtn, &stopBtn })
        b->setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void KrakenEditor::resized()
{
    view.setBounds (getLocalBounds());
    int x = getWidth() - 24;
    const int y = 18, h = 30, gap = 8;
    auto place = [&] (juce::TextButton& b, int w) { x -= w; b.setBounds (x, y, w, h); x -= gap; };
    place (stopBtn, 64);
    place (mutateBtn, 90);
    place (playPatchBtn, 110);
    place (playSampleBtn, 118);
    place (loadBtn, 126);
}

void KrakenEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.isEmpty()) return;
    const juce::File f (files[0]);
    if (f.existsAsFile()) proc.loadSampleFile (f);
}

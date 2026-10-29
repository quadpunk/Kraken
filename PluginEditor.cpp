// SPDX-License-Identifier: MIT
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
constexpr int kFooterH = 200;
} // namespace

//==============================================================================
BWLookAndFeel::BWLookAndFeel()
{
    setColour (juce::TextButton::buttonColourId, juce::Colours::black);
    setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colours::black);
    setColour (juce::PopupMenu::textColourId, juce::Colours::white);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colours::white);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::PopupMenu::headerTextColourId, juce::Colours::white);
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
    proc.updatePreviews();
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
    // 0 .. 0.22: the arms reach the ship at the surface and coil around it; 0.22 .. 1: it is dragged down, past the kraken's
    // side, into the dark water above the inspector, tilting and shrinking as it goes
    const float e = juce::jlimit (0.0f, 1.0f, shipD);
    auto smooth = [] (float a, float b, float v) { const float t = juce::jlimit (0.0f, 1.0f, (v - a) / (b - a)); return t * t * (3.0f - 2.0f * t); };
    const float pull = smooth (0.22f, 1.0f, e);
    const float sideP = smooth (0.30f, 0.62f, e);
    const float startY = arena.getY() + 4.0f * scaleU;
    const float deepY = inspector.getY() - 26.0f * scaleU;
    x = headC.x + std::sin (time * 1.3f) * 5.0f * (1.0f - pull) - headRx * 1.62f * sideP + std::sin (time * 3.1f) * 3.0f * pull;
    y = startY + (deepY - startY) * (pull * pull * (1.4f - 0.4f * pull));
    sc = scaleU * 0.8f * (1.0f - 0.5f * pull);
    ang = 0.12f * std::sin (time * 2.1f) * (1.0f - pull) - 1.15f * pull + 0.08f * std::sin (time * 4.0f) * pull;
    alpha = e < 0.78f ? 1.0f : juce::jlimit (0.0f, 1.0f, 1.0f - (e - 0.78f) / 0.22f);
}

void KrakenView::drawShipTentacles (juce::Graphics& g)
{
    if (! shipActive) return;
    float sx, sy, ssc, sang, sal;
    shipPose (sx, sy, ssc, sang, sal);
    if (sal < 0.02f) return;

    const float e = juce::jlimit (0.0f, 1.0f, shipD);
    const float reach = juce::jlimit (0.0f, 1.0f, e / 0.22f);
    const float grab = reach * reach * (3.0f - 2.0f * reach) * sal;      // arms retract again while the ship fades away
    const auto xf = juce::AffineTransform::scale (ssc).rotated (sang).translated (sx, sy);
    const auto bx = breathXf();

    struct Grip { float side, headY, hx, hy; };      // head anchor (relative to head) and contact point on the hull (ship space)
    static const Grip grips[] = { { -1.0f, -0.18f, -52.0f,  4.0f }, { -1.0f, 0.28f, -40.0f, 20.0f },
                                  {  1.0f, -0.18f,  52.0f,  4.0f }, {  1.0f, 0.28f,  40.0f, 20.0f } };
    int k = 0;
    for (const auto& gr : grips)
    {
        const juce::Point<float> p0 = juce::Point<float> (headC.x + gr.side * headRx * 0.86f, headC.y + gr.headY * headRy).transformedBy (bx);
        const juce::Point<float> contact = juce::Point<float> (gr.hx, gr.hy).transformedBy (xf);
        // the tip trails a little behind the contact point while the arm is still reaching, and strains when the ship is pulled
        const float strain = 3.0f * std::sin (time * 5.0f + (float) k) * e;
        const auto p3 = p0 + (contact - p0) * grab + juce::Point<float> (strain, -strain * 0.5f);
        if (grab > 0.02f)
            drawTentacle (g, p0, p3, gr.side, gr.side * 2.0f + (float) k, 0.85f, false);
        ++k;
    }
}

void KrakenView::drawShipWraps (juce::Graphics& g, float grab)
{
    // three coils across the hull, growing from the sides as the arms close around it
    const auto white = juce::Colours::white;
    for (int b = 0; b < 3; ++b)
    {
        const float f = juce::jlimit (0.0f, 1.0f, (grab - 0.15f * (float) b) / 0.55f);
        if (f <= 0.01f) continue;
        const float yk = 2.0f + (float) b * 8.0f;
        const juce::Point<float> a (-58.0f, yk), c (0.0f, yk + 12.0f), d (58.0f, yk);
        juce::Path band;
        const int N = 28;
        for (int i = 0; i <= N; ++i)
        {
            const float t = 0.5f * f * (float) i / (float) N;           // from both sides towards the middle
            const float u = 1.0f - t;
            const auto pl = a * (u * u) + c * (2.0f * u * t) + d * (t * t);
            if (i == 0) band.startNewSubPath (pl); else band.lineTo (pl);
        }
        juce::Path bandR;
        for (int i = 0; i <= N; ++i)
        {
            const float t = 1.0f - 0.5f * f * (float) i / (float) N;
            const float u = 1.0f - t;
            const auto pr = a * (u * u) + c * (2.0f * u * t) + d * (t * t);
            if (i == 0) bandR.startNewSubPath (pr); else bandR.lineTo (pr);
        }
        for (const auto* p : { &band, &bandR })
        {
            g.setColour (white);
            g.strokePath (*p, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (juce::Colours::black);
            g.strokePath (*p, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        // suckers
        g.setColour (white);
        for (int i = 1; i < 7; ++i)
        {
            const float t = (float) i / 7.0f;
            if (t > 0.5f * f && t < 1.0f - 0.5f * f) continue;
            const float u = 1.0f - t;
            const auto q = a * (u * u) + c * (2.0f * u * t) + d * (t * t);
            g.fillEllipse (q.x - 1.4f, q.y - 1.4f, 2.8f, 2.8f);
        }
    }
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

    // the deeper it goes the darker and murkier the water around it; a trail of bubbles rises from the hull
    {
        const float e = juce::jlimit (0.0f, 1.0f, shipD);
        const float depth = juce::jlimit (0.0f, 1.0f, (e - 0.25f) / 0.6f);
        if (depth > 0.0f)
        {
            const float R = 120.0f * sc / scaleU * scaleU;
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.85f * depth * alpha), x, y,
                                                     juce::Colours::black.withAlpha (0.0f), x + R, y, true));
            g.fillEllipse (x - R, y - R, R * 2.0f, R * 2.0f);
            for (int i = 0; i < 7; ++i)
            {
                const float ph = std::fmod (time * (0.45f + 0.07f * (float) i) + (float) i * 0.37f, 1.0f);
                const float bx = x + std::sin ((float) i * 2.3f + time * 2.0f) * 20.0f * sc / scaleU * scaleU;
                const float by = y - 10.0f * sc / scaleU * scaleU - ph * 90.0f * scaleU;
                const float br = (1.5f + 3.0f * (float) (i % 3)) * scaleU * (1.0f - ph * 0.5f);
                g.setColour (juce::Colours::white.withAlpha (0.55f * (1.0f - ph) * depth * alpha));
                g.drawEllipse (bx - br, by - br, br * 2.0f, br * 2.0f, 1.0f);
            }
        }
    }

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

        {   // the coils
            const float reach = juce::jlimit (0.0f, 1.0f, juce::jlimit (0.0f, 1.0f, shipD) / 0.22f);
            drawShipWraps (g, reach * reach * (3.0f - 2.0f * reach));
        }
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
    headC = { W * 0.5f, arena.getY() - 4.0f + headRy + 8.0f * s };   // 16 px higher than before: more room above the meters

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
    {
        const float ww = kinRects[0].getX() - margin - 24.0f, H2 = (float) getHeight();
        waveRect   = { margin, H2 - 186.0f, ww, 28.0f };
        regionRect = { margin, H2 - 154.0f, ww, 56.0f };
        patchRect  = { margin, H2 -  94.0f, ww, 56.0f };
    }
    layoutInspector();
}

void KrakenView::layoutInspector()
{
    const int n = kArmCount[selArm];
    const int cols = n > 14 ? 8 : 7;
    const float cw = inspector.getWidth() / (float) cols;
    for (int j = 0; j < n; ++j)
        cells[(size_t) j] = { inspector.getX() + (float) (j % cols) * cw, inspector.getY() + 20.0f + (float) (j / cols) * 30.0f,
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

    drawArenaDecor (g);
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
        g.setColour (white.withAlpha (active ? 0.75f : 0.25f));
        g.drawText (juce::String (i + 1).paddedLeft ('0', 2), z.withHeight (14.0f).translated (-4.0f, 2.0f),
                    juce::Justification::centredRight, false);
    }

    // tentacles reaching for the ship and dragging it down
    drawShipTentacles (g);

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
    g.drawText (status, 24, 78, juce::jmax (200, (int) (getWidth() * 0.5f - 100.0f * scaleU - 34.0f)), 16, juce::Justification::centredLeft, true);

    if (proc.analysing.load())
    {
        const juce::Rectangle<float> bar (24.0f, 98.0f, 300.0f, 5.0f);
        g.setColour (white.withAlpha (0.5f));
        g.drawRect (bar, 1.0f);
        g.setColour (white);
        g.fillRect (bar.withWidth (bar.getWidth() * proc.progress.load()));
    }

    // thin ruler under the header
    const float W = (float) getWidth(), ry = (float) kHeaderH - 3.0f;
    g.setColour (white.withAlpha (0.22f));
    g.drawLine (0.0f, ry, W, ry, 1.0f);
    for (int x = 0; x < (int) W; x += 20)
    {
        const bool major = x % 100 == 0;
        g.setColour (white.withAlpha (major ? 0.4f : 0.2f));
        g.drawLine ((float) x + 0.5f, ry - (major ? 6.0f : 3.0f), (float) x + 0.5f, ry, 1.0f);
    }
    g.setFont (monoFont (9.0f));
    g.setColour (white.withAlpha (0.3f));
    g.drawText ("200 patches  |  " + juce::String (kNumParams) + " parameters  |  8 voices", (int) W - 24 - 320, 3, 320, 11, juce::Justification::centredRight, false);

    // row under the preset browser: patch info, output meter, voices
    const float rowX = W - 368.0f, y3 = 90.0f;
    juce::String info = "NO PATCH LOADED";
    if (proc.presetName.isNotEmpty())
    {
        info = "USER / RECENT";
        const auto& fac = proc.presets.factory();
        for (size_t i = 0; i < fac.size(); ++i)
            if (fac[i].name == proc.presetName)
            {
                info = fac[i].category.toUpperCase() + "  " + juce::String ((int) i + 1) + "/" + juce::String ((int) fac.size());
                break;
            }
    }
    g.setFont (monoFont (9.0f, true));
    g.setColour (white.withAlpha (0.75f));
    g.drawText (info, (int) rowX, (int) y3 - 1, 138, 12, juce::Justification::centredLeft, true);

    g.setFont (monoFont (9.0f));
    g.setColour (white.withAlpha (0.55f));
    g.drawText ("OUT", (int) rowX + 142, (int) y3 - 1, 24, 12, juce::Justification::centredLeft, false);
    const float db = 20.0f * std::log10 (std::max (proc.outPeak.load(), 1.0e-5f));
    const int lit = juce::roundToInt (juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f) * 20.0f);
    for (int k = 0; k < 20; ++k)
    {
        g.setColour (white.withAlpha (k < lit ? (k >= 17 ? 1.0f : 0.8f) : 0.16f));
        g.fillRect (rowX + 168.0f + (float) k * 5.0f, y3, 4.0f, 9.0f);
    }
    const int voices = proc.activeVoiceCount.load();
    for (int k = 0; k < 8; ++k)
    {
        const juce::Rectangle<float> r (rowX + 344.0f - 70.0f + (float) k * 9.0f, y3 + 1.0f, 7.0f, 7.0f);
        if (k < voices) { g.setColour (white); g.fillRect (r); }
        else            { g.setColour (white.withAlpha (0.3f)); g.drawRect (r, 1.0f); }
    }
}

void KrakenView::drawArenaDecor (juce::Graphics& g)
{
    const auto white = juce::Colours::white;
    const float W = (float) getWidth();
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (arena.toNearestInt());

    // sonar rings and spokes around the kraken
    for (int k = 0; k < 5; ++k)
    {
        const float r = (170.0f + 85.0f * (float) k) * scaleU;
        g.setColour (white.withAlpha (0.085f - 0.014f * (float) k));
        g.drawEllipse (headC.x - r, headC.y - r * 0.72f, r * 2.0f, r * 1.44f, 1.0f);
    }
    g.setColour (white.withAlpha (0.045f));
    for (int a = 0; a < 12; ++a)
    {
        const float ang = juce::MathConstants<float>::twoPi * (float) a / 12.0f + 0.26f;
        g.drawLine (headC.x, headC.y, headC.x + std::cos (ang) * 900.0f * scaleU, headC.y + std::sin (ang) * 650.0f * scaleU, 1.0f);
    }

    // depth ruler on the left edge
    const float top = arena.getY() + 28.0f, bottom = inspector.getY() - 30.0f;
    int n = 0;
    for (float y = top; y < bottom; y += 20.0f, ++n)
    {
        const bool major = n % 4 == 0;
        g.setColour (white.withAlpha (major ? 0.4f : 0.2f));
        g.drawLine (6.0f, y, major ? 14.0f : 10.0f, y, 1.0f);
        if (major)
        {
            g.setFont (monoFont (8.0f));
            g.setColour (white.withAlpha (0.3f));
            g.drawText (juce::String (n * 5), 15, (int) y - 5, 18, 10, juce::Justification::centredLeft, false);
        }
    }

    // corner brackets
    g.setColour (white.withAlpha (0.4f));
    const float m = 8.0f, L = 14.0f;
    const float x0 = m, x1 = W - m, y0 = arena.getY() + m, y1 = arena.getBottom() - m;
    for (int cx = 0; cx < 2; ++cx)
        for (int cy = 0; cy < 2; ++cy)
        {
            const float px = cx ? x1 : x0, py = cy ? y1 : y0;
            const float dx = cx ? -L : L, dy = cy ? -L : L;
            g.drawLine (px, py, px + dx, py, 1.2f);
            g.drawLine (px, py, px, py + dy, 1.2f);
        }
}

juce::AffineTransform KrakenView::breathXf() const
{
    // slow in/out (about 0.25 Hz) - the head swells a little, pivoting around its lower half so the mouth stays put
    const float b = std::sin (time * 1.55f);
    return juce::AffineTransform::scale (1.0f + 0.010f * b, 1.0f + 0.028f * b, headC.x, headC.y + headRy * 0.55f);
}

void KrakenView::drawHead (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState breathState (g);
    g.addTransform (breathXf());
    const auto white = juce::Colours::white;
    const auto black = juce::Colours::black;
    const float cx = headC.x, cy = headC.y, rx = headRx, ry = headRy;

    // sharp curved horns
    for (int side = -1; side <= 1; side += 2)
    {
        const float sd = (float) side;
        juce::Path horn;
        horn.startNewSubPath (cx + sd * rx * 0.30f, cy - ry * 0.93f);
        horn.quadraticTo (cx + sd * rx * 0.70f, cy - ry * 1.05f, cx + sd * rx * 1.12f, cy - ry * 1.83f);
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
    // Rough offset ink contours and red scratches: purely visual, no animation changes.
    g.setColour (white.withAlpha (0.55f));
    g.drawEllipse (head.expanded (3.0f, 2.0f), 0.9f);
    g.setColour (juce::Colour (0xffdc1025).withAlpha (0.80f));
    juce::Path cheekMark;
    cheekMark.startNewSubPath (cx - rx * 0.58f, cy + ry * 0.27f);
    cheekMark.quadraticTo (cx - rx * 0.42f, cy + ry * 0.62f, cx - rx * 0.18f, cy + ry * 0.72f);
    g.strokePath (cheekMark, juce::PathStrokeType (1.5f));
    g.setColour (white);

    // narrow slanted eyes with slit pupils, heavy lids and a V-shaped scowl
    for (int side = -1; side <= 1; side += 2)
    {
        const float sd = (float) side;
        const juce::Point<float> ec (cx + sd * rx * 0.40f, cy - ry * 0.02f);
        const float ew = rx * 0.49f, eh = ry * 0.32f;
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
        g.setColour (juce::Colour (0xffd90820));
        g.drawLine (ec.x - sd * ew * 0.50f, ec.y - eh * 0.43f, ec.x + sd * ew * 0.52f, ec.y - eh * 1.24f, 1.2f);
        g.setColour (white);

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
    for (int k = 1; k <= 11; ++k)
    {
        const float t = (float) k / 12.0f, u = 1.0f - t;
        const juce::Point<float> base = ml * (u * u) + topC * (2.0f * u * t) + mr * (t * t);
        const float th = ((k & 1) ? 0.21f : 0.14f) * ry, tw = rx * 0.036f;
        juce::Path tooth;
        tooth.startNewSubPath (base.x - tw, base.y);
        tooth.lineTo (base.x + tw, base.y);
        tooth.lineTo (base.x, base.y + th);
        tooth.closeSubPath();
        g.fillPath (tooth);
    }
    g.setColour (juce::Colour (0xffd90820));
    juce::Path lowerLip;
    lowerLip.startNewSubPath (ml.x + rx * 0.08f, my + ry * 0.18f);
    lowerLip.quadraticTo (cx, my + ry * 0.72f, mr.x - rx * 0.08f, my + ry * 0.12f);
    g.strokePath (lowerLip, juce::PathStrokeType (1.6f));
}

void KrakenView::drawArm (juce::Graphics& g, int i, juce::Point<float> tip, bool highlight)
{
    drawTentacle (g, anchors[(size_t) i].transformedBy (breathXf()), tip, (i % 2 == 0) ? 1.0f : -1.0f, (float) i, 1.0f, highlight);
}

void KrakenView::drawTentacle (juce::Graphics& g, juce::Point<float> p0, juce::Point<float> p3,
                               float sgn, float phase, float ws, bool highlight)
{
    const auto v = p3 - p0;
    const float len = std::max (1.0f, v.getDistanceFromOrigin());
    const juce::Point<float> nrm (-v.y / len, v.x / len);
    // the arms never rest: two slow waves plus a faint breathing swell. Only the control points move, the tip (p3) stays put.
    const float breath = std::sin (time * 1.55f);
    const float wob1 = (std::sin (time * 1.0f + phase * 1.9f) * 0.15f + std::sin (time * 2.3f + phase * 0.7f) * 0.035f) * len;
    const float wob2 = (std::sin (time * 0.8f + phase * 2.7f + 1.0f) * 0.15f + std::sin (time * 1.9f + phase * 1.3f) * 0.035f) * len;
    const auto p1 = p0 + v * (0.33f + 0.015f * breath) + nrm * (sgn * (0.20f + 0.03f * breath) * len + wob1);
    const auto p2 = p0 + v * 0.68f + nrm * (-sgn * (0.20f - 0.03f * breath) * len + wob2);

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
    g.setColour (white.withAlpha (0.045f));
    g.fillRoundedRectangle (inspector.expanded (4.0f, 2.0f), 6.0f);
    g.setColour (white.withAlpha (0.35f));
    g.drawRoundedRectangle (inspector.expanded (4.0f, 2.0f), 6.0f, 1.0f);
    {
        const juce::Rectangle<float> tab (inspector.getX() + 2.0f, inspector.getY() + 1.0f, 116.0f, 15.0f);
        g.setColour (white);
        g.fillRoundedRectangle (tab, 3.0f);
        g.setColour (juce::Colours::black);
        g.setFont (monoFont (11.0f, true));
        g.drawText (armName (selArm), tab, juce::Justification::centred, false);
        g.setColour (white.withAlpha (0.55f));
        g.setFont (monoFont (10.0f));
        g.drawText ("ARM " + juce::String (selArm + 1) + "/" + juce::String (kNumArms) + "   click a tentacle to inspect",
                    (int) tab.getRight() + 10, (int) tab.getY(), 330, 15, juce::Justification::centredLeft, false);
        g.setColour (white.withAlpha (0.4f));
        g.drawText ("drag = set    dbl-click = reset    scroll = level " + juce::String (juce::roundToInt (proc.getLevel() * 100.0f)) + "%",
                    (int) inspector.getRight() - 420, (int) tab.getY(), 418, 15, juce::Justification::centredRight, false);
    }

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
        for (int t = 0; t <= 4; ++t)
        {
            const float tx = cell.getX() + (float) t * 0.25f * cell.getWidth();
            g.setColour (white.withAlpha (t == 2 ? 0.55f : 0.3f));
            g.drawLine (tx, y + 2.0f, tx, y + (t == 2 ? 6.0f : 4.0f), 1.0f);
        }
        g.setColour (white);
        g.drawLine (cell.getX(), y, cell.getX() + v * cell.getWidth(), y, active ? 3.0f : 2.0f);
        const float r = active ? 5.0f : 4.0f;
        g.fillEllipse (cell.getX() + v * cell.getWidth() - r, y - r, r * 2.0f, r * 2.0f);
    }
}

void KrakenView::drawBottom (juce::Graphics& g, const Patch& patch)
{
    const auto white = juce::Colours::white;

    g.setFont (monoFont (9.0f, true));
    g.setColour (white.withAlpha (0.55f));
    g.drawText ("SAMPLE EDITOR   |   COMPARE SAMPLE vs PATCH", (int) waveRect.getX(), (int) waveRect.getY() - 14, (int) waveRect.getWidth(), 11,
                juce::Justification::centredLeft, false);
    g.setColour (white.withAlpha (0.2f));
    g.drawLine (waveRect.getX() + 290.0f, waveRect.getY() - 9.0f, waveRect.getRight(), waveRect.getY() - 9.0f, 1.0f);

    // sample editor: overview strip (handles) + zoomed sample region + rendered patch, all on the same time axis
    const float sampleLen = proc.getSampleLength();
    g.setColour (white.withAlpha (0.35f));
    g.drawRoundedRectangle (waveRect, 4.0f, 1.0f);
    g.drawRoundedRectangle (regionRect, 4.0f, 1.0f);
    g.drawRoundedRectangle (patchRect, 4.0f, 1.0f);
    if (! proc.thumb.empty() && sampleLen > 0.0f)
    {
        const float n = (float) proc.thumb.size();
        const float ss = proc.getSelStart(), se = proc.getSelEnd();
        const float x0 = timeToX (ss), x1 = timeToX (se);
        g.setColour (white.withAlpha (0.14f));
        g.fillRect (juce::Rectangle<float> (x0, waveRect.getY() + 1.0f, x1 - x0, waveRect.getHeight() - 2.0f));
        for (size_t i = 0; i < proc.thumb.size(); ++i)
        {
            const float t = ((float) i + 0.5f) / n * sampleLen;
            const float x = timeToX (t);
            const float h = proc.thumb[i] * (waveRect.getHeight() * 0.5f - 3.0f);
            g.setColour (white.withAlpha (t >= ss && t <= se ? 1.0f : 0.3f));
            g.drawLine (x, waveRect.getCentreY() - h, x, waveRect.getCentreY() + h, 1.0f);
        }
        g.setColour (white.withAlpha (0.4f));
        for (float s = 1.0f; s < sampleLen; s += 1.0f)
            g.drawLine (timeToX (s), waveRect.getBottom() - 4.0f, timeToX (s), waveRect.getBottom() - 1.5f, 1.0f);

        const float gh = waveRect.getHeight() - 8.0f;
        for (float xh : { x0, x1 })
        {
            g.setColour (white);
            g.fillRect (xh - 1.0f, waveRect.getY() + 1.0f, 2.0f, waveRect.getHeight() - 2.0f);
            g.fillRect (xh - 4.0f, waveRect.getCentreY() - gh * 0.5f, 8.0f, gh);
            g.setColour (juce::Colours::black);
            g.drawLine (xh, waveRect.getCentreY() - gh * 0.3f, xh, waveRect.getCentreY() + gh * 0.3f, 1.0f);
        }
        const float ph = proc.getPlayhead();
        if (ph >= 0.0f)
        {
            g.setColour (white);
            g.drawLine (timeToX (ph), waveRect.getY() + 1.0f, timeToX (ph), waveRect.getBottom() - 1.0f, 1.5f);
        }

        auto drawLane = [&] (const juce::Rectangle<float>& r, const std::vector<float>& v, const char* label)
        {
            g.setColour (white.withAlpha (0.15f));
            g.drawLine (r.getX() + 3.0f, r.getCentreY(), r.getRight() - 3.0f, r.getCentreY(), 1.0f);
            g.setColour (white);
            const float nb = (float) v.size();
            for (size_t i = 0; i < v.size(); ++i)
            {
                const float x = r.getX() + 3.0f + ((float) i + 0.5f) / nb * (r.getWidth() - 6.0f);
                const float h = std::min (1.0f, v[i]) * (r.getHeight() * 0.5f - 3.0f);
                g.drawLine (x, r.getCentreY() - h, x, r.getCentreY() + h, 1.0f);
            }
            g.setFont (monoFont (9.0f, true));
            g.setColour (white.withAlpha (0.7f));
            g.drawText (label, (int) r.getX() + 6, (int) r.getY() + 2, 200, 11, juce::Justification::left, false);
            if (ph >= ss && ph <= se)
            {
                const float x = r.getX() + 3.0f + (ph - ss) / std::max (0.001f, se - ss) * (r.getWidth() - 6.0f);
                g.setColour (white.withAlpha (0.9f));
                g.drawLine (x, r.getY() + 1.0f, x, r.getBottom() - 1.0f, 1.5f);
            }
        };
        drawLane (regionRect, proc.regionThumb, "SAMPLE (selected region)");
        drawLane (patchRect, proc.patchThumb,
                  ("PATCH (root note " + juce::MidiMessage::getMidiNoteName (proc.rootNote.load(), true, true, 4)
                   + ", level-matched)").toRawUTF8());

        const bool capped = (se - ss) > KrakenProcessor::kMaxAnalysisSec - 0.01f;
        g.setFont (monoFont (10.0f));
        g.setColour (white.withAlpha (capped ? 1.0f : 0.65f));
        juce::String info = "ANALYSE REGION " + juce::String (ss, 2) + " - " + juce::String (se, 2) + " s   ("
                            + juce::String (se - ss, 2) + " s, max 4.0)   sample " + juce::String (sampleLen, 1) + " s";
        if (sampleLen > KrakenProcessor::kMaxAnalysisSec + 0.05f) info += "   drag handles / window";
        g.drawText (info, (int) waveRect.getX(), (int) patchRect.getBottom() + 4, (int) waveRect.getWidth() - 120, 14,
                    juce::Justification::centredLeft, true);
    }
    else
    {
        g.setFont (monoFont (11.0f));
        g.setColour (white.withAlpha (0.4f));
        g.drawText ("drop a .wav / .aiff / .flac / .ogg here", waveRect.getUnion (patchRect), juce::Justification::centred, false);
    }

    // kin blobs
    g.setColour (white.withAlpha (0.045f));
    g.fillRoundedRectangle (juce::Rectangle<float> (kinRects[0].getX() - 10.0f, kinRects[0].getY() - 22.0f,
                                                    kinRects[5].getRight() - kinRects[0].getX() + 20.0f, 66.0f), 6.0f);
    g.setColour (white.withAlpha (0.25f));
    g.drawRoundedRectangle (juce::Rectangle<float> (kinRects[0].getX() - 10.0f, kinRects[0].getY() - 22.0f,
                                                    kinRects[5].getRight() - kinRects[0].getX() + 20.0f, 66.0f), 6.0f, 1.0f);
    g.setFont (monoFont (9.0f));
    g.setColour (white.withAlpha (0.4f));
    g.drawText ("click = load", (int) kinRects[5].getRight() - 70, (int) kinRects[0].getY() - 16, 70, 12, juce::Justification::right, false);
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
float KrakenView::timeToX (float t) const
{
    const float len = std::max (0.001f, proc.getSampleLength());
    return waveRect.getX() + 3.0f + juce::jlimit (0.0f, 1.0f, t / len) * (waveRect.getWidth() - 6.0f);
}
float KrakenView::xToTime (float x) const
{
    const float len = proc.getSampleLength();
    return juce::jlimit (0.0f, 1.0f, (x - waveRect.getX() - 3.0f) / std::max (1.0f, waveRect.getWidth() - 6.0f)) * len;
}
void KrakenView::mouseMove (const juce::MouseEvent& e)
{
    mousePos = e.position;
    if (proc.getSampleLength() > 0.0f && waveRect.contains (e.position))
    {
        hoverArm = hoverKin = hoverParam = -1;
        const bool onHandle = std::abs (e.position.x - timeToX (proc.getSelStart())) < 9.0f
                              || std::abs (e.position.x - timeToX (proc.getSelEnd())) < 9.0f;
        setMouseCursor (onHandle ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
        return;
    }
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
    if (proc.getSampleLength() > 0.0f && waveRect.contains (e.position))
    {
        const float len = proc.getSampleLength();
        const float ss = proc.getSelStart(), se = proc.getSelEnd();
        const float d0 = std::abs (e.position.x - timeToX (ss)), d1 = std::abs (e.position.x - timeToX (se));
        if (d0 < 9.0f && d0 <= d1)      selDrag = SelDrag::start;
        else if (d1 < 9.0f)             selDrag = SelDrag::end;
        else
        {
            const float t = xToTime (e.position.x);
            if (t < ss || t > se)   // click outside: centre the window there
            {
                const float L = se - ss;
                const float a = juce::jlimit (0.0f, std::max (0.0f, len - L), t - 0.5f * L);
                proc.setSelection (a, a + L, true);
            }
            selDrag = SelDrag::window;
            selGrab = t - proc.getSelStart();
        }
        return;
    }
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
    if (selDrag != SelDrag::none)
    {
        const float t = xToTime (e.position.x);
        const float ss = proc.getSelStart(), se = proc.getSelEnd();
        if (selDrag == SelDrag::start)      proc.setSelection (t, se, true);
        else if (selDrag == SelDrag::end)   proc.setSelection (ss, t, false);
        else
        {
            const float L = se - ss;
            const float a = juce::jlimit (0.0f, std::max (0.0f, proc.getSampleLength() - L), t - selGrab);
            proc.setSelection (a, a + L, true);
        }
        return;
    }
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
    selDrag = SelDrag::none;
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
    for (auto* b : { &loadBtn, &playSampleBtn, &playPatchBtn, &mutateBtn, &stopBtn, &prevBtn, &presetBtn, &nextBtn, &saveBtn, &exportBtn, &analyseBtn })
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
    analyseBtn.onClick = [this] { proc.analyseSelection(); };
    prevBtn.onClick = [this] { proc.stepPreset (-1); };
    nextBtn.onClick = [this] { proc.stepPreset (+1); };
    presetBtn.onClick = [this] { showPresetMenu(); };
    saveBtn.onClick = [this] { askNameAndSave(); };
    exportBtn.onClick = [this] { showExportMenu(); };
    timerCallback();
    startTimerHz (4);

    setResizable (true, true);
    setResizeLimits (960, 700, 1700, 1280);
    setSize (980, 820);
}

KrakenEditor::~KrakenEditor()
{
    stopTimer();
    for (auto* b : { &loadBtn, &playSampleBtn, &playPatchBtn, &mutateBtn, &stopBtn, &prevBtn, &presetBtn, &nextBtn, &saveBtn, &exportBtn, &analyseBtn })
        b->setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void KrakenEditor::resized()
{
    view.setBounds (getLocalBounds());
    {
        const auto wb = view.getWaveBounds();
        analyseBtn.setBounds (wb.getRight() - 110, wb.getBottom() + 3, 110, 22);
    }
    int x = getWidth() - 24;
    const int y = 18, h = 30, gap = 8;
    auto place = [&] (juce::TextButton& b, int w) { x -= w; b.setBounds (x, y, w, h); x -= gap; };
    place (stopBtn, 64);
    place (mutateBtn, 90);
    place (playPatchBtn, 110);
    place (playSampleBtn, 118);
    place (loadBtn, 126);

    // second row: preset browser  [<] [name v] [>] [SAVE] [EXPORT]
    int x2 = getWidth() - 24;
    const int y2 = 56, h2 = 26, g2 = 5;
    auto place2 = [&] (juce::TextButton& b, int w) { x2 -= w; b.setBounds (x2, y2, w, h2); x2 -= g2; };
    place2 (exportBtn, 76);
    place2 (saveBtn, 56);
    place2 (nextBtn, 26);
    place2 (presetBtn, 140);
    place2 (prevBtn, 26);
}

namespace
{
struct FnCallback : juce::ModalComponentManager::Callback
{
    explicit FnCallback (std::function<void (int)> f) : fn (std::move (f)) {}
    void modalStateFinished (int result) override { if (fn) fn (result); }
    std::function<void (int)> fn;
};
}

void KrakenEditor::timerCallback()
{
    juce::String t = proc.presetName.isEmpty() ? juce::String ("PRESETS  v") : proc.presetName;
    if (t.length() > 16) t = t.substring (0, 15) + "...";
    if (presetBtn.getButtonText() != t) presetBtn.setButtonText (t);
}

void KrakenEditor::showPresetMenu()
{
    const auto rec = proc.presets.recent();
    const auto usr = proc.presets.user();
    const auto& fac = proc.presets.factory();

    juce::PopupMenu menu, recentMenu, userMenu;
    for (size_t i = 0; i < rec.size(); ++i)
        recentMenu.addItem (5000 + (int) i, rec[i].name, true, rec[i].name == proc.presetName);
    if (rec.empty()) recentMenu.addItem (1, "(analyse a sample or save a patch)", false, false);
    for (size_t i = 0; i < usr.size(); ++i)
        userMenu.addItem (8000 + (int) i, usr[i].name, true, usr[i].name == proc.presetName);
    if (usr.empty()) userMenu.addItem (2, "(press SAVE to keep a patch)", false, false);

    menu.addSubMenu ("Recent  (" + juce::String ((int) rec.size()) + ")", recentMenu);
    menu.addSubMenu ("My patches  (" + juce::String ((int) usr.size()) + ")", userMenu);
    menu.addSeparator();

    std::vector<juce::String> cats;
    for (const auto& f : fac)
        if (std::find (cats.begin(), cats.end(), f.category) == cats.end()) cats.push_back (f.category);
    for (const auto& c : cats)
    {
        juce::PopupMenu sub;
        for (size_t i = 0; i < fac.size(); ++i)
            if (fac[i].category == c) sub.addItem (1000 + (int) i, fac[i].name, true, fac[i].name == proc.presetName);
        menu.addSubMenu (c, sub);
    }

    juce::Component::SafePointer<KrakenEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetBtn).withMinimumWidth (220),
                        [safe, rec, usr] (int id)
                        {
                            if (safe == nullptr || id < 1000) return;
                            const auto& f = safe->proc.presets.factory();
                            if (id >= 8000)      { const auto i = (size_t) (id - 8000); if (i < usr.size()) safe->proc.loadPreset (usr[i]); }
                            else if (id >= 5000) { const auto i = (size_t) (id - 5000); if (i < rec.size()) safe->proc.loadPreset (rec[i]); }
                            else                 { const auto i = (size_t) (id - 1000); if (i < f.size())   safe->proc.loadPreset (f[i]); }
                        });
}

void KrakenEditor::askNameAndSave()
{
    juce::Component::SafePointer<KrakenEditor> safe (this);
    auto* aw = new juce::AlertWindow ("Save patch", "Give the patch a name:", juce::MessageBoxIconType::NoIcon, this);
    aw->addTextEditor ("name", proc.presetName.isEmpty() ? juce::String ("My patch") : proc.presetName, "");
    aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, new FnCallback ([safe, aw] (int r)
    {
        if (r == 1 && safe != nullptr) safe->proc.saveCurrentAsUser (aw->getTextEditorContents ("name"));
    }), true);
}

void KrakenEditor::showExportMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Export this patch (.krkpatch)...");
    m.addItem (2, "Export all my patches as a bank (.krkbank)...");
    m.addItem (6, "Export this patch as MIDI SysEx (.syx)...");
    m.addItem (7, "Export all my patches as SysEx (.syx)...");
    m.addItem (3, "Export this sound as WAV (4 s at root note)...");
    m.addSeparator();
    m.addItem (4, "Import patch or bank...");
    m.addItem (5, "Show patch folder");

    juce::Component::SafePointer<KrakenEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&exportBtn).withMinimumWidth (260), [safe] (int id)
    {
        if (safe == nullptr || id == 0) return;
        auto* self = safe.getComponent();
        auto& proc = self->proc;
        const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        const auto name = proc.presetName.isEmpty() ? juce::String ("kraken patch") : proc.presetName;
        const int saveFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting;

        if (id == 5) { proc.presets.userFolder().revealToUser(); return; }

        if (id == 1)
        {
            self->chooser = std::make_unique<juce::FileChooser> ("Export patch", docs.getChildFile (juce::File::createLegalFileName (name) + ".krkpatch"), "*.krkpatch");
            self->chooser->launchAsync (saveFlags, [safe] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (safe == nullptr || f == juce::File()) return;
                if (! f.hasFileExtension ("krkpatch")) f = f.withFileExtension ("krkpatch");
                auto& p = safe->proc;
                const bool ok = kraken::PresetManager::exportPatch (f, p.currentEntry (p.presetName.isEmpty() ? f.getFileNameWithoutExtension() : p.presetName));
                p.statusMessage = ok ? "exported: " + f.getFileName() : "export failed";
            });
        }
        else if (id == 2)
        {
            self->chooser = std::make_unique<juce::FileChooser> ("Export bank", docs.getChildFile ("kraken patches.krkbank"), "*.krkbank");
            self->chooser->launchAsync (saveFlags, [safe] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (safe == nullptr || f == juce::File()) return;
                if (! f.hasFileExtension ("krkbank")) f = f.withFileExtension ("krkbank");
                auto& p = safe->proc;
                auto list = p.presets.user();
                if (list.empty()) list = p.presets.recent();
                if (list.empty()) { p.statusMessage = "nothing to export yet: save a patch first"; return; }
                const bool ok = kraken::PresetManager::exportBank (f, list);
                p.statusMessage = ok ? "exported " + juce::String ((int) list.size()) + " patches: " + f.getFileName() : "export failed";
            });
        }
        else if (id == 6 || id == 7)
        {
            const bool single = id == 6;
            self->chooser = std::make_unique<juce::FileChooser> ("Export SysEx", docs.getChildFile (juce::File::createLegalFileName (single ? name : juce::String ("kraken patches")) + ".syx"), "*.syx");
            self->chooser->launchAsync (saveFlags, [safe, single] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (safe == nullptr || f == juce::File()) return;
                if (! f.hasFileExtension ("syx")) f = f.withFileExtension ("syx");
                auto& p = safe->proc;
                std::vector<kraken::PresetEntry> list;
                if (single) list.push_back (p.currentEntry (p.presetName.isEmpty() ? f.getFileNameWithoutExtension() : p.presetName));
                else { list = p.presets.user(); if (list.empty()) list = p.presets.recent(); }
                if (list.empty()) { p.statusMessage = "nothing to export yet: save a patch first"; return; }
                const bool ok = kraken::PresetManager::exportSyx (f, list);
                p.statusMessage = ok ? "exported " + juce::String ((int) list.size()) + " SysEx patch(es): " + f.getFileName() : "export failed";
            });
        }
        else if (id == 3)
        {
            self->chooser = std::make_unique<juce::FileChooser> ("Export sound as WAV", docs.getChildFile (juce::File::createLegalFileName (name) + ".wav"), "*.wav");
            self->chooser->launchAsync (saveFlags, [safe] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (safe == nullptr || f == juce::File()) return;
                if (! f.hasFileExtension ("wav")) f = f.withFileExtension ("wav");
                safe->proc.statusMessage = safe->proc.exportWav (f) ? "exported: " + f.getFileName() : "WAV export failed";
            });
        }
        else if (id == 4)
        {
            self->chooser = std::make_unique<juce::FileChooser> ("Import patch or bank", docs, "*.krkpatch;*.krkbank;*.syx");
            self->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                        [safe] (const juce::FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (safe == nullptr || ! f.existsAsFile()) return;
                auto& p = safe->proc;
                const auto list = kraken::PresetManager::importFile (f);
                if (list.empty()) { p.statusMessage = "that file contains no kr@ken patches"; return; }
                for (const auto& e : list) p.presets.saveUser (e);
                p.loadPreset (list.front());
                p.statusMessage = "imported " + juce::String ((int) list.size()) + " patch(es) into My patches";
            });
        }
    });
}

void KrakenEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.isEmpty()) return;
    const juce::File f (files[0]);
    if (f.existsAsFile()) proc.loadSampleFile (f);
}

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
    repaint();
}

void KrakenView::resized()
{
    const float W = (float) getWidth(), H = (float) getHeight();
    const float s = std::min (W / 920.0f, H / 640.0f);
    arena = { 0.0f, (float) kHeaderH, W, H - (float) kHeaderH - (float) kFooterH };

    headRx = 95.0f * s;
    headRy = 80.0f * s;
    headC = { W * 0.5f, arena.getY() + 12.0f + headRy + 8.0f * s };

    const float margin = 24.0f;
    const float cell = (W - 2.0f * margin) / (float) kNumArms;
    const float zoneTop = headC.y + headRy + 30.0f * s;
    const float zoneBottom = arena.getBottom() - 8.0f;
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
}

juce::Point<float> KrakenView::tipPos (const Patch& p, int a) const
{
    const auto& z = zones[(size_t) a];
    return { z.getX() + p.p[(size_t) (2 * a)] * z.getWidth(),
             z.getY() + (1.0f - p.p[(size_t) (2 * a + 1)]) * z.getHeight() };
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
        const bool active = (i == hoverArm || i == dragArm);
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
        g.drawText (juce::String (armAxisX (i)) + " >", z.withHeight (14.0f).translated (0.0f, z.getHeight() - 30.0f),
                    juce::Justification::centredRight, false);
        g.drawText ("^ " + juce::String (armAxisY (i)), z.withHeight (14.0f).translated (0.0f, 2.0f),
                    juce::Justification::centredLeft, false);
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

    // value read-out
    const int showArm = dragArm >= 0 ? dragArm : hoverArm;
    if (showArm >= 0)
    {
        g.setColour (white);
        g.setFont (monoFont (12.0f, true));
        g.drawText (juce::String (armName (showArm)) + "   "
                        + armAxisX (showArm) + " " + juce::String (juce::roundToInt (patch.p[(size_t) (2 * showArm)] * 100.0f)) + "%   "
                        + armAxisY (showArm) + " " + juce::String (juce::roundToInt (patch.p[(size_t) (2 * showArm + 1)] * 100.0f)) + "%",
                    juce::Rectangle<float> (0.0f, arena.getBottom() - 4.0f, W, 16.0f), juce::Justification::centred, false);
    }

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
    g.drawText (status, 24, 78, getWidth() - 200, 16, juce::Justification::centredLeft, false);

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
    g.drawText ("scroll = level " + juce::String (juce::roundToInt (proc.getLevel() * 100.0f)) + "%   dbl-click tip = reset arm",
                getWidth() - 360, 98, 336, 12, juce::Justification::centredRight, false);
}

void KrakenView::drawHead (juce::Graphics& g)
{
    const auto white = juce::Colours::white;
    const auto black = juce::Colours::black;
    const float cx = headC.x, cy = headC.y, rx = headRx, ry = headRy;

    // fins
    for (int side = -1; side <= 1; side += 2)
    {
        const float sd = (float) side;
        juce::Path fin;
        fin.startNewSubPath (cx + sd * rx * 0.50f, cy - ry * 0.80f);
        fin.lineTo (cx + sd * rx * 1.12f, cy - ry * 1.30f);
        fin.lineTo (cx + sd * rx * 0.90f, cy - ry * 0.35f);
        fin.closeSubPath();
        g.setColour (black);
        g.fillPath (fin);
        g.setColour (white);
        g.strokePath (fin, juce::PathStrokeType (2.0f, juce::PathStrokeType::mitered));
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
        g.setColour (white.withAlpha (0.45f));
        for (float x = cx - rx - ry; x < cx + rx + ry; x += 7.0f)
            g.drawLine (x, cy + ry * 1.1f, x + ry * 0.9f, cy + ry * 0.2f, 1.0f);
    }

    g.setColour (white);
    g.drawEllipse (head, 2.5f);

    // eyes follow the mouse
    for (int side = -1; side <= 1; side += 2)
    {
        const float sd = (float) side;
        const juce::Point<float> ec (cx + sd * rx * 0.38f, cy + ry * 0.02f);
        const float ew = rx * 0.40f, eh = ry * 0.28f;
        g.setColour (white);
        g.fillEllipse (ec.x - ew * 0.5f, ec.y - eh * 0.5f, ew, eh);
        auto d = mousePos - ec;
        const float len = std::max (1.0f, d.getDistanceFromOrigin());
        d = d * (std::min (len, 40.0f) / len * 0.12f);
        g.setColour (black);
        g.fillEllipse (ec.x + d.x - 5.0f, ec.y + d.y - eh * 0.4f, 10.0f, eh * 0.8f);
        // angry brow
        g.setColour (white);
        g.drawLine (ec.x - sd * ew * 0.62f, ec.y - eh * 0.85f, ec.x + sd * ew * 0.62f, ec.y - eh * 0.25f - 4.0f, 4.0f);
    }

    // jagged mouth
    juce::Path mouth;
    const float my = cy + ry * 0.60f;
    mouth.startNewSubPath (cx - rx * 0.36f, my);
    for (int i = 1; i <= 8; ++i)
        mouth.lineTo (cx - rx * 0.36f + rx * 0.72f * (float) i / 8.0f, my + ((i % 2) ? 7.0f : 0.0f));
    g.setColour (white);
    g.strokePath (mouth, juce::PathStrokeType (2.0f));
}

void KrakenView::drawArm (juce::Graphics& g, int i, juce::Point<float> tip, bool highlight)
{
    const auto p0 = anchors[(size_t) i];
    const auto p3 = tip;
    const auto v = p3 - p0;
    const float len = std::max (1.0f, v.getDistanceFromOrigin());
    const juce::Point<float> nrm (-v.y / len, v.x / len);
    const float sgn = (i % 2 == 0) ? 1.0f : -1.0f;
    const float wob1 = std::sin (time * 1.4f + (float) i * 1.9f) * 0.10f * len;
    const float wob2 = std::sin (time * 1.1f + (float) i * 2.7f + 1.0f) * 0.10f * len;
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
    auto hw = [] (float t) { return 11.0f * std::pow (1.0f - t, 0.8f) + 2.5f; };

    std::array<juce::Point<float>, N + 1> left, right;
    for (int k = 0; k <= N; ++k)
    {
        const float t = (float) k / (float) N;
        const auto c = bez (t);
        auto tg = tang (t);
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

    // suckers
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
    setMouseCursor (hoverArm >= 0 || hoverKin >= 0 ? juce::MouseCursor::PointingHandCursor
                                                   : juce::MouseCursor::NormalCursor);
}

void KrakenView::mouseDown (const juce::MouseEvent& e)
{
    mouseMove (e);
    if (hoverKin >= 0) { proc.applyKin (hoverKin); return; }
    if (hoverArm >= 0)
    {
        dragArm = hoverArm;
        proc.beginGesture (2 * dragArm);
        proc.beginGesture (2 * dragArm + 1);
        mouseDrag (e);
    }
}

void KrakenView::mouseDrag (const juce::MouseEvent& e)
{
    mousePos = e.position;
    if (dragArm < 0) return;
    const auto& z = zones[(size_t) dragArm];
    const float x = juce::jlimit (0.0f, 1.0f, (e.position.x - z.getX()) / z.getWidth());
    const float y = juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - z.getY()) / z.getHeight());
    proc.setParam (2 * dragArm, x);
    proc.setParam (2 * dragArm + 1, y);
}

void KrakenView::mouseUp (const juce::MouseEvent&)
{
    if (dragArm >= 0)
    {
        proc.endGesture (2 * dragArm);
        proc.endGesture (2 * dragArm + 1);
        dragArm = -1;
    }
}

void KrakenView::mouseDoubleClick (const juce::MouseEvent& e)
{
    mouseMove (e);
    if (hoverArm < 0) return;
    const auto def = defaultPatch();
    for (int j = 0; j < 2; ++j)
    {
        const int idx = 2 * hoverArm + j;
        proc.beginGesture (idx);
        proc.setParam (idx, def.p[(size_t) idx]);
        proc.endGesture (idx);
    }
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
    setResizeLimits (760, 560, 1600, 1100);
    setSize (920, 640);
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

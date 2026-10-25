// SPDX-License-Identifier: MIT
#include "PluginProcessor.h"
#include <cstring>
#include "PluginEditor.h"

using namespace kraken;

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto def = defaultPatch();
    for (int i = 0; i < kNumParams; ++i)
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "p" + juce::String (i), 1 },
            juce::String (armName (armOf (i))) + " " + paramShort (i),
            juce::NormalisableRange<float> (0.0f, 1.0f), def.p[(size_t) i]));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "level", 1 }, "Level",
        juce::NormalisableRange<float> (0.0f, 1.0f), def.level));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "tune", 1 }, "Tune (cents)",
        juce::NormalisableRange<float> (-50.0f, 50.0f), 0.0f));
    return layout;
}

KrakenProcessor::KrakenProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KRAKEN", makeLayout())
{
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto id = "p" + juce::String (i);
        params[(size_t) i] = apvts.getParameter (id);
        raw[(size_t) i] = apvts.getRawParameterValue (id);
    }
    levelParam = apvts.getParameter ("level");
    levelRaw = apvts.getRawParameterValue ("level");
    tuneParam = apvts.getParameter ("tune");
    tuneRaw = apvts.getRawParameterValue ("tune");

    kin.push_back (defaultPatch());
    statusMessage = "drop a sample on the kraken to feed it";

    analyzer.onProgress = [this] (float p) { progress.store (p); };
    analyzer.onFinished = [weak = juce::WeakReference<KrakenProcessor> (this)] (AnalysisResult r)
    {
        juce::MessageManager::callAsync ([weak, r]
        {
            if (auto* self = weak.get()) self->applyResult (r);
        });
    };
}

KrakenProcessor::~KrakenProcessor()
{
    analyzer.onFinished = nullptr;
    analyzer.onProgress = nullptr;
    analyzer.stopThread (10000);
}

//==============================================================================
void KrakenProcessor::prepareToPlay (double sampleRate, int)
{
    currentSr = sampleRate;
    synth.prepare (sampleRate);
}

bool KrakenProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void KrakenProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();

    synth.setPatch (getPatch());

    // preview commands from the UI
    const int cmd = command.exchange (0);
    if (cmd == 1)
    {
        synth.allOff();
        previewNote = -1;
        samplePos = (double) selStartSamp.load();
        samplePlaying = true;
    }
    else if (cmd == 2)
    {
        samplePlaying = false;
        if (previewNote >= 0) synth.noteOff (previewNote);
        previewNote = rootNote.load();
        synth.noteOn (previewNote, 0.8f);
        previewNoteLeft = (int) (std::max (0.3f, sampleSeconds.load()) * 0.6f * (float) currentSr);
    }
    else if (cmd == 3)
    {
        samplePlaying = false;
        synth.allOff();
        previewNote = -1;
    }
    if (previewNote >= 0)
    {
        previewNoteLeft -= n;
        if (previewNoteLeft <= 0) { synth.noteOff (previewNote); previewNote = -1; }
    }

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = juce::jlimit (pos, n, meta.samplePosition);
        if (t > pos) { synth.render (L + pos, R != nullptr ? R + pos : nullptr, t - pos); pos = t; }
        const auto m = meta.getMessage();
        if (m.isNoteOn()) synth.noteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) synth.noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) synth.allOff();
    }
    if (pos < n) synth.render (L + pos, R != nullptr ? R + pos : nullptr, n - pos);

    if (samplePlaying)
    {
        const juce::SpinLock::ScopedTryLockType lock (sampleLock);
        if (lock.isLocked() && ! sampleData.empty())
        {
            const double ratio = sampleSr / currentSr;
            const int size = std::min ((int) sampleData.size(), std::max (selEndSamp.load(), 2));
            for (int i = 0; i < n; ++i)
            {
                const int i0 = (int) samplePos;
                if (i0 + 1 >= size) { samplePlaying = false; break; }
                const float f = (float) (samplePos - i0);
                const float smp = 0.8f * (sampleData[(size_t) i0] * (1.0f - f) + sampleData[(size_t) i0 + 1] * f);
                L[i] += smp;
                if (R != nullptr) R[i] += smp;
                samplePos += ratio;
            }
        }
        else if (! lock.isLocked()) {}
        else samplePlaying = false;
    }
    playhead.store (samplePlaying ? (float) (samplePos / sampleSr) : -1.0f);
}

//==============================================================================
Patch KrakenProcessor::getPatch() const
{
    Patch p;
    for (int i = 0; i < kNumParams; ++i) p.p[(size_t) i] = raw[(size_t) i]->load();
    p.level = levelRaw->load();
    p.tune = tuneRaw->load() * 0.01f;
    return p;
}

void KrakenProcessor::setPatch (const Patch& p)
{
    for (int i = 0; i < kNumParams; ++i) params[(size_t) i]->setValueNotifyingHost (p.p[(size_t) i]);
    levelParam->setValueNotifyingHost (p.level);
    tuneParam->setValueNotifyingHost (tuneParam->convertTo0to1 (juce::jlimit (-50.0f, 50.0f, p.tune * 100.0f)));
}

void KrakenProcessor::setParam (int i, float v) { params[(size_t) i]->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v)); }
void KrakenProcessor::beginGesture (int i) { params[(size_t) i]->beginChangeGesture(); }
void KrakenProcessor::endGesture (int i) { params[(size_t) i]->endChangeGesture(); }
void KrakenProcessor::setLevel (float v) { levelParam->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v)); }

void KrakenProcessor::sendCommand (int cmd) { command.store (cmd); }

void KrakenProcessor::applyKin (int index)
{
    if (index >= 0 && index < (int) kin.size()) setPatch (kin[(size_t) index]);
}

void KrakenProcessor::mutate()
{
    juce::Random rnd;
    auto gauss = [&rnd]
    {
        const float u1 = std::max (1.0e-6f, rnd.nextFloat()), u2 = rnd.nextFloat();
        return std::sqrt (-2.0f * std::log (u1)) * std::cos (kTwoPi * u2);
    };
    const Patch cur = getPatch();
    kin.clear();
    kin.push_back (cur);
    for (int k = 0; k < 5; ++k)
    {
        Patch m = cur;
        for (auto& v : m.p)
            if (rnd.nextFloat() < 0.6f) v = juce::jlimit (0.0f, 1.0f, v + 0.14f * gauss());
        kin.push_back (m);
    }
}

//==============================================================================
void KrakenProcessor::loadSampleFile (const juce::File& file)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        statusMessage = "the kraken cannot digest that file";
        return;
    }

    const int numSamples = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 12.0));
    juce::AudioBuffer<float> buf ((int) reader->numChannels, numSamples);
    reader->read (&buf, 0, numSamples, 0, true, true);

    std::vector<float> mono ((size_t) numSamples, 0.0f);
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        const auto* src = buf.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i) mono[(size_t) i] += src[i] / (float) buf.getNumChannels();
    }

    // thumbnail
    constexpr int kThumb = 1600;
    thumb.assign (kThumb, 0.0f);
    for (int i = 0; i < numSamples; ++i)
    {
        const size_t b = std::min<size_t> (kThumb - 1, (size_t) ((juce::int64) i * kThumb / numSamples));
        thumb[b] = std::max (thumb[b], std::abs (mono[(size_t) i]));
    }
    float mx = 0.0001f;
    for (float v : thumb) mx = std::max (mx, v);
    for (auto& v : thumb) v /= mx;

    // copy for the audio-thread preview (swap under lock, free outside)
    std::vector<float> copy = mono;
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);
        sampleData.swap (copy);
        sampleSr = reader->sampleRate;
    }
    sampleName = file.getFileName();
    matchPct = 0.0f;
    progress.store (0.0f);
    statusMessage = {};

    analyzer.stopThread (10000);
    analysing.store (false);
    fullMono = std::move (mono);
    fullSr = reader->sampleRate;
    const float len = getSampleLength();
    selStart = 0.0f;
    selEnd = std::min (len, kMaxAnalysisSec);
    setSelection (selStart, selEnd, false);

    if (len <= kMaxAnalysisSec + 0.05f)
        analyseSelection();          // short sample: analyse it right away, as before
    else
        statusMessage = "sample is " + juce::String (len, 1) + " s: drag the window on the waveform, listen, then press ANALYSE (max 4 s)";
}

void KrakenProcessor::setSelection (float a, float b, bool startMoved)
{
    const float len = getSampleLength();
    if (len <= 0.0f) return;
    constexpr float kMin = 0.05f;
    a = juce::jlimit (0.0f, std::max (0.0f, len - kMin), a);
    b = juce::jlimit (std::min (len, a + kMin), len, b);
    if (b - a > kMaxAnalysisSec)
    {
        if (startMoved) b = a + kMaxAnalysisSec; else a = b - kMaxAnalysisSec;
    }
    selStart = std::max (0.0f, a);
    selEnd = std::min (len, b);
    selStartSamp.store ((int) (selStart * fullSr));
    selEndSamp.store ((int) (selEnd * fullSr));
    sampleSeconds.store (selEnd - selStart);
}

void KrakenProcessor::updatePreviews()
{
    if (fullMono.empty())
    {
        if (! regionThumb.empty() || ! patchThumb.empty()) { regionThumb.clear(); patchThumb.clear(); previewKey = regionKey = 0; }
        return;
    }

    const auto patch = getPatch();
    const int root = rootNote.load();
    auto mix = [] (juce::uint64 h, juce::uint32 v) { h ^= v; h *= 1099511628211ULL; return h; };
    auto bits = [] (float f) { juce::uint32 u; std::memcpy (&u, &f, sizeof (u)); return u; };

    juce::uint64 rk = 1469598103934665603ULL;
    rk = mix (rk, (juce::uint32) selStartSamp.load());
    rk = mix (rk, (juce::uint32) selEndSamp.load());
    juce::uint64 key = rk;
    key = mix (key, (juce::uint32) root);
    key = mix (key, bits (patch.level));
    key = mix (key, bits (patch.tune));
    for (float v : patch.p) key = mix (key, bits (v));

    if (key == previewKey && ! regionThumb.empty()) return;
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastPreviewMs < 150) return;      // rate limit while sliders are being dragged
    lastPreviewMs = now;
    previewKey = key;

    constexpr int kBins = 1000;
    const auto i0 = (size_t) juce::jlimit (0, (int) fullMono.size() - 1, (int) (selStart * fullSr));
    const auto i1 = (size_t) juce::jlimit ((int) i0 + 1, (int) fullMono.size(), (int) (selEnd * fullSr));
    const size_t n = i1 - i0;

    if (rk != regionKey || regionThumb.empty())
    {
        regionKey = rk;
        regionThumb.assign (kBins, 0.0f);
        double e = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const float v = fullMono[i0 + i];
            const auto b = std::min<size_t> (kBins - 1, i * kBins / n);
            regionThumb[b] = std::max (regionThumb[b], std::abs (v));
            e += (double) v * v;
        }
        regionPeak = 1.0e-4f;
        for (float v : regionThumb) regionPeak = std::max (regionPeak, v);
        regionRms = (float) std::sqrt (e / (double) n);
        for (auto& v : regionThumb) v /= regionPeak;
    }

    // patch at its root note, same duration as the region (note held for 60 %, like the analysis assumes)
    constexpr double sr = 44100.0;
    const int N = juce::jlimit (2205, (int) (4.05 * sr), (int) ((double) n / fullSr * sr));
    const float f0 = 440.0f * std::pow (2.0f, (float) (root - 69) / 12.0f);
    std::vector<float> out;
    renderOffline (patch, sr, f0, N, (int) (0.6 * N), out);
    double e = 0;
    for (float v : out) e += (double) v * v;
    const float rms = (float) std::sqrt (e / (double) N);
    const float g = rms > 1.0e-6f ? std::min (50.0f, regionRms / rms) : 0.0f;

    patchThumb.assign (kBins, 0.0f);
    for (int i = 0; i < N; ++i)
    {
        const auto b = std::min<size_t> (kBins - 1, (size_t) i * kBins / (size_t) N);
        patchThumb[b] = std::max (patchThumb[b], std::abs (out[(size_t) i]) * g / regionPeak);
    }
}

void KrakenProcessor::analyseSelection()
{
    if (fullMono.empty() || analysing.load()) return;
    const auto i0 = (size_t) juce::jlimit (0, (int) fullMono.size() - 1, (int) (selStart * fullSr));
    const auto i1 = (size_t) juce::jlimit ((int) i0 + 1, (int) fullMono.size(), (int) (selEnd * fullSr));
    std::vector<float> region (fullMono.begin() + (long) i0, fullMono.begin() + (long) i1);

    AnalysisOptions opt;
    opt.maxSeconds = kMaxAnalysisSec;
    opt.trimSilence = selStart < 0.01f;   // an explicit start point is respected as chosen
    matchPct = 0.0f;
    progress.store (0.0f);
    analysing.store (true);
    statusMessage = {};
    analyzer.analyse (std::move (region), fullSr, opt);
}

void KrakenProcessor::applyResult (const AnalysisResult& r)
{
    analysing.store (false);
    progress.store (1.0f);
    if (! r.ok)
    {
        statusMessage = "nothing to hunt: sample silent or too short";
        return;
    }
    kin = r.kin;
    matchPct = r.match;
    rootNote.store (r.midiNote);
    setPatch (r.best);

    // keep every analysis result in the "recent" list so it can be recalled later
    const auto stem = juce::File (sampleName).getFileNameWithoutExtension();
    presetName = (stem.isEmpty() ? juce::String ("Analysis") : stem) + " " + juce::Time::getCurrentTime().formatted ("%H:%M:%S");
    presets.addRecent (currentEntry (presetName));
    currentProgram = -1;
}

//==============================================================================
kraken::PresetEntry KrakenProcessor::currentEntry (const juce::String& name) const
{
    PresetEntry e;
    e.name = name;
    e.category = "User";
    e.patch = getPatch();
    e.root = rootNote.load();
    e.timeMs = juce::Time::currentTimeMillis();
    return e;
}

void KrakenProcessor::loadPreset (const PresetEntry& e)
{
    setPatch (e.patch);
    rootNote.store (e.root);
    presetName = e.name;
    matchPct = 0.0f;
    statusMessage = "patch: " + e.name + (e.category.isNotEmpty() ? "   [" + e.category + "]" : juce::String());
    mutate();   // the kin blobs become relatives of the loaded patch
    currentProgram = -1;
    if (e.origin == PresetEntry::Origin::Factory)
        for (size_t i = 0; i < presets.factory().size(); ++i)
            if (presets.factory()[i].name == e.name) { currentProgram = (int) i; break; }
}

void KrakenProcessor::stepPreset (int direction)
{
    auto list = presets.factory();
    for (auto& u : presets.user()) list.push_back (u);
    if (list.empty()) return;
    int idx = direction > 0 ? -1 : (int) list.size();
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].name == presetName) { idx = (int) i; break; }
    idx = (idx + direction + (int) list.size()) % (int) list.size();
    loadPreset (list[(size_t) idx]);
}

bool KrakenProcessor::saveCurrentAsUser (const juce::String& name)
{
    const auto clean = name.trim();
    if (clean.isEmpty()) return false;
    auto e = currentEntry (clean);
    if (! presets.saveUser (e)) { statusMessage = "could not save the patch"; return false; }
    presets.addRecent (e);
    presetName = clean;
    matchPct = 0.0f;
    statusMessage = "saved patch: " + clean;
    return true;
}

bool KrakenProcessor::exportWav (const juce::File& file)
{
    const double sr = 44100.0;
    const int n = (int) (4.0 * sr), hold = (int) (2.5 * sr);
    const auto p = getPatch();
    const float f0 = 440.0f * std::pow (2.0f, (float) (rootNote.load() - 69) / 12.0f);
    std::vector<float> out;
    renderOffline (p, sr, f0, n, hold, out);

    juce::AudioBuffer<float> buf (1, n);
    for (int i = 0; i < n; ++i) buf.setSample (0, i, juce::jlimit (-1.0f, 1.0f, out[(size_t) i]));
    file.deleteFile();
    auto stream = std::make_unique<juce::FileOutputStream> (file);
    if (! stream->openedOk()) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sr, 1, 24, {}, 0));
    if (writer == nullptr) return false;
    stream.release();   // owned by the writer now
    return writer->writeFromAudioSampleBuffer (buf, 0, n);
}

void KrakenProcessor::setCurrentProgram (int i)
{
    const auto& f = presets.factory();
    if (i >= 0 && i < (int) f.size()) loadPreset (f[(size_t) i]);
}

const juce::String KrakenProcessor::getProgramName (int i)
{
    const auto& f = presets.factory();
    return (i >= 0 && i < (int) f.size()) ? f[(size_t) i].name : juce::String();
}

//==============================================================================
juce::AudioProcessorEditor* KrakenProcessor::createEditor() { return new KrakenEditor (*this); }

void KrakenProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    apvts.state.setProperty ("root", rootNote.load(), nullptr);
    apvts.state.setProperty ("sampleName", sampleName, nullptr);
    apvts.state.setProperty ("match", matchPct, nullptr);
    apvts.state.setProperty ("presetName", presetName, nullptr);
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void KrakenProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            rootNote.store ((int) apvts.state.getProperty ("root", 57));
            sampleName = apvts.state.getProperty ("sampleName", "").toString();
            matchPct = (float) (double) apvts.state.getProperty ("match", 0.0);
            presetName = apvts.state.getProperty ("presetName", "").toString();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KrakenProcessor(); }

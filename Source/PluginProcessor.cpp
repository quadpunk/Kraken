#include "PluginProcessor.h"
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
        samplePos = 0.0;
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
            const int size = (int) sampleData.size();
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
}

//==============================================================================
Patch KrakenProcessor::getPatch() const
{
    Patch p;
    for (int i = 0; i < kNumParams; ++i) p.p[(size_t) i] = raw[(size_t) i]->load();
    p.level = levelRaw->load();
    return p;
}

void KrakenProcessor::setPatch (const Patch& p)
{
    for (int i = 0; i < kNumParams; ++i) params[(size_t) i]->setValueNotifyingHost (p.p[(size_t) i]);
    levelParam->setValueNotifyingHost (p.level);
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
    thumb.assign (400, 0.0f);
    for (int i = 0; i < numSamples; ++i)
    {
        const size_t b = std::min<size_t> (399, (size_t) ((juce::int64) i * 400 / numSamples));
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
    sampleSeconds.store ((float) numSamples / (float) reader->sampleRate);

    sampleName = file.getFileName();
    matchPct = 0.0f;
    progress.store (0.0f);
    analysing.store (true);
    statusMessage = {};
    analyzer.analyse (std::move (mono), reader->sampleRate);
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
}

//==============================================================================
juce::AudioProcessorEditor* KrakenProcessor::createEditor() { return new KrakenEditor (*this); }

void KrakenProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    apvts.state.setProperty ("root", rootNote.load(), nullptr);
    apvts.state.setProperty ("sampleName", sampleName, nullptr);
    apvts.state.setProperty ("match", matchPct, nullptr);
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
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KrakenProcessor(); }

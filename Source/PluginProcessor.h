// SPDX-License-Identifier: MIT
#pragma once

#include "Common.h"
#include "SynthEngine.h"
#include "Analyzer.h"
#include "PresetManager.h"

class KrakenProcessor : public juce::AudioProcessor
{
public:
    KrakenProcessor();
    ~KrakenProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "kr@ken"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    // the 200 factory patches are exposed to the host as programs
    int getNumPrograms() override { return (int) presets.factory().size(); }
    int getCurrentProgram() override { return std::max (0, currentProgram); }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    // API used by the editor (message thread)
    kraken::Patch getPatch() const;
    void setPatch (const kraken::Patch&);
    void setParam (int index, float normalised);
    void beginGesture (int index);
    void endGesture (int index);
    void setLevel (float normalised);
    float getLevel() const { return levelRaw->load(); }

    void loadSampleFile (const juce::File&);

    // sample editor: the region that is analysed and previewed (max kMaxAnalysisSec)
    static constexpr float kMaxAnalysisSec = 4.0f;
    float getSampleLength() const { return fullMono.empty() ? 0.0f : (float) fullMono.size() / (float) fullSr; }
    float getSelStart() const { return selStart; }
    float getSelEnd() const { return selEnd; }
    void setSelection (float startSec, float endSec, bool startMoved);   // enforces 0.05 s .. 4 s
    void analyseSelection();
    float getPlayhead() const { return playhead.load(); }
    std::atomic<float> outPeak { 0.0f };          // output level for the UI meter (audio thread writes)
    std::atomic<int> activeVoiceCount { 0 };

    // comparison lanes (message thread): peak envelopes of the selected sample region and of the current patch rendered
    // at its root note, level-matched to the sample (RMS), both scaled so the sample's peak = 1
    void updatePreviews();
    std::vector<float> regionThumb, patchThumb;                // seconds into the sample, <0 when idle
    void mutate();
    void applyKin (int index);
    void sendCommand (int cmd); // 1 = play sample, 2 = play patch, 3 = stop

    // presets / patches (message thread)
    void loadPreset (const kraken::PresetEntry&);
    void stepPreset (int direction);                       // +1 / -1 through factory + user patches
    bool saveCurrentAsUser (const juce::String& name);
    kraken::PresetEntry currentEntry (const juce::String& name) const;
    bool exportWav (const juce::File&);                    // renders the current patch at its root note
    kraken::PresetManager presets;
    juce::String presetName;

    juce::AudioProcessorValueTreeState apvts;

    // visible state (message thread only)
    std::vector<kraken::Patch> kin;
    std::vector<float> thumb;   // peak envelope of the whole sample (normalised)
    juce::String sampleName, statusMessage;
    float matchPct = 0.0f;
    std::atomic<int> rootNote { 57 };
    std::atomic<bool> analysing { false };
    std::atomic<float> progress { 0.0f };

private:
    void applyResult (const kraken::AnalysisResult&);

    std::array<juce::RangedAudioParameter*, kraken::kNumParams> params {};
    std::array<std::atomic<float>*, kraken::kNumParams> raw {};
    juce::RangedAudioParameter* levelParam = nullptr;
    juce::RangedAudioParameter* tuneParam = nullptr;
    std::atomic<float>* tuneRaw = nullptr;
    int currentProgram = -1;
    std::atomic<float>* levelRaw = nullptr;

    kraken::Synth synth;
    double currentSr = 44100.0;

    // sample preview
    juce::SpinLock sampleLock;
    std::vector<float> sampleData;
    double sampleSr = 44100.0;
    std::vector<float> fullMono;   // message thread only: the loaded sample
    double fullSr = 44100.0;
    float selStart = 0.0f, selEnd = 0.0f;
    juce::uint64 previewKey = 0, regionKey = 0;
    juce::uint32 lastPreviewMs = 0;
    float regionPeak = 1.0f, regionRms = 0.0f;
    std::atomic<int> selStartSamp { 0 }, selEndSamp { 0 };
    std::atomic<float> playhead { -1.0f };
    double samplePos = 0.0;
    bool samplePlaying = false;
    std::atomic<int> command { 0 };
    std::atomic<float> sampleSeconds { 1.0f };
    int previewNote = -1;
    int previewNoteLeft = 0;

    kraken::Analyzer analyzer; // keep last: destroyed first

    JUCE_DECLARE_WEAK_REFERENCEABLE (KrakenProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KrakenProcessor)
};

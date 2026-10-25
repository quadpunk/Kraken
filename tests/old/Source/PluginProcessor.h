#pragma once

#include "Common.h"
#include "SynthEngine.h"
#include "Analyzer.h"

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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
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
    void mutate();
    void applyKin (int index);
    void sendCommand (int cmd); // 1 = play sample, 2 = play patch, 3 = stop

    juce::AudioProcessorValueTreeState apvts;

    // visible state (message thread only)
    std::vector<kraken::Patch> kin;
    std::vector<float> thumb;
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
    std::atomic<float>* levelRaw = nullptr;

    kraken::Synth synth;
    double currentSr = 44100.0;

    // sample preview
    juce::SpinLock sampleLock;
    std::vector<float> sampleData;
    double sampleSr = 44100.0;
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

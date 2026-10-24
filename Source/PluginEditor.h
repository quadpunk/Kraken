#pragma once

#include "Common.h"
#include "PluginProcessor.h"

//==============================================================================
// Black & white look for buttons
class BWLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BWLookAndFeel();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
};

//==============================================================================
// The kraken: head, 8 draggable tentacles, kin blobs, sample waveform
class KrakenView : public juce::Component, private juce::Timer
{
public:
    explicit KrakenView (KrakenProcessor&);
    ~KrakenView() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;
    juce::Point<float> tipPos (const kraken::Patch&, int arm) const;

    void drawHeader (juce::Graphics&);
    void drawHead (juce::Graphics&);
    void drawArm (juce::Graphics&, int arm, juce::Point<float> tip, bool highlight);
    void drawBottom (juce::Graphics&, const kraken::Patch&);

    KrakenProcessor& proc;
    std::array<juce::Rectangle<float>, kraken::kNumArms> zones;
    std::array<juce::Point<float>, kraken::kNumArms> anchors;
    std::array<juce::Rectangle<float>, 6> kinRects;
    juce::Rectangle<float> arena, waveRect;
    juce::Point<float> headC, mousePos;
    float headRx = 95.0f, headRy = 80.0f;
    float time = 0.0f;
    int hoverArm = -1, dragArm = -1, hoverKin = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KrakenView)
};

//==============================================================================
class KrakenEditor : public juce::AudioProcessorEditor,
                     public juce::FileDragAndDropTarget
{
public:
    explicit KrakenEditor (KrakenProcessor&);
    ~KrakenEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    KrakenProcessor& proc;
    BWLookAndFeel laf;
    KrakenView view;
    juce::TextButton loadBtn { "LOAD SAMPLE" }, playSampleBtn { "PLAY SAMPLE" },
                     playPatchBtn { "PLAY PATCH" }, mutateBtn { "MUTATE" }, stopBtn { "STOP" };
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KrakenEditor)
};

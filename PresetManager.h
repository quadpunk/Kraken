// SPDX-License-Identifier: MIT
#pragma once

#include "Common.h"
#include "SynthEngine.h"

namespace kraken
{
struct PresetEntry
{
    enum class Origin { Factory, User, Recent };

    juce::String name, category;
    Patch patch;
    int root = 60;                 // note the patch is meant to be played at (the sample's root for analysed patches)
    Origin origin = Origin::User;
    juce::int64 timeMs = 0;
};

// Factory bank (200 patches), user patches (one .krkpatch XML file each), recently made patches
// (analysis results and saves), plus import / export of single patches and banks.
class PresetManager
{
public:
    PresetManager();

    const std::vector<PresetEntry>& factory() const { return factoryList; }
    std::vector<PresetEntry> user() const;                       // scans the user folder, sorted by name
    const std::vector<PresetEntry>& recent() const { return recentList; }

    juce::File userFolder() const;

    void addRecent (PresetEntry entry);                           // newest first, max 30, persisted
    bool saveUser (const PresetEntry& entry, juce::File* written = nullptr);

    static bool exportPatch (const juce::File& file, const PresetEntry& entry);
    static bool exportBank (const juce::File& file, const std::vector<PresetEntry>& entries);
    // kr@ken's own MIDI SysEx format (not DX7 compatible, see SysEx.h); a bank is several messages in one file
    static bool exportSyx (const juce::File& file, const std::vector<PresetEntry>& entries);
    static std::vector<PresetEntry> importFile (const juce::File& file);   // .krkpatch, .krkbank or .syx

    static std::unique_ptr<juce::XmlElement> toXml (const PresetEntry&);
    static bool fromXml (const juce::XmlElement&, PresetEntry&);

private:
    juce::File recentFile() const;
    void loadRecent();
    void saveRecent() const;

    std::vector<PresetEntry> factoryList, recentList;
    static constexpr int kMaxRecent = 30;
};

} // namespace kraken

// SPDX-License-Identifier: MIT
#include "PresetManager.h"
#include "FactoryPresets.h"
#include "SysEx.h"

namespace kraken
{
namespace
{
const char* kPatchTag = "KRAKEN_PATCH";
const char* kBankTag = "KRAKEN_BANK";
}

PresetManager::PresetManager()
{
    for (const auto& d : factoryDefs())
    {
        PresetEntry e;
        e.name = d.name;
        e.category = d.category;
        e.patch = factoryPatch (d);
        e.root = d.root;
        e.origin = PresetEntry::Origin::Factory;
        factoryList.push_back (std::move (e));
    }
    loadRecent();
}

juce::File PresetManager::userFolder() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("kr@ken").getChildFile ("Patches");
    dir.createDirectory();
    return dir;
}

juce::File PresetManager::recentFile() const
{
    return userFolder().getParentDirectory().getChildFile ("recent.xml");
}

//==============================================================================
std::unique_ptr<juce::XmlElement> PresetManager::toXml (const PresetEntry& e)
{
    auto xml = std::make_unique<juce::XmlElement> (kPatchTag);
    xml->setAttribute ("version", 1);
    xml->setAttribute ("name", e.name);
    xml->setAttribute ("category", e.category);
    xml->setAttribute ("root", e.root);
    xml->setAttribute ("level", (double) e.patch.level);
    xml->setAttribute ("tune", (double) e.patch.tune);
    xml->setAttribute ("time", juce::String (e.timeMs));
    juce::String values;
    for (int i = 0; i < kNumParams; ++i)
        values << juce::String (e.patch.p[(size_t) i], 5) << (i + 1 < kNumParams ? " " : "");
    xml->setAttribute ("values", values);
    return xml;
}

bool PresetManager::fromXml (const juce::XmlElement& xml, PresetEntry& e)
{
    if (! xml.hasTagName (kPatchTag)) return false;
    const auto tokens = juce::StringArray::fromTokens (xml.getStringAttribute ("values"), " ", "");
    if (tokens.size() < 8) return false;

    e = PresetEntry();
    e.name = xml.getStringAttribute ("name", "Imported patch");
    e.category = xml.getStringAttribute ("category", "User");
    e.root = juce::jlimit (0, 127, xml.getIntAttribute ("root", 60));
    e.timeMs = xml.getStringAttribute ("time").getLargeIntValue();
    e.patch = defaultPatch();
    // patches written by older versions may have fewer parameters: the rest keeps its default
    for (int i = 0; i < kNumParams && i < tokens.size(); ++i)
        e.patch.p[(size_t) i] = juce::jlimit (0.0f, 1.0f, tokens[i].getFloatValue());
    e.patch.level = juce::jlimit (0.0f, 1.0f, (float) xml.getDoubleAttribute ("level", 0.7));
    e.patch.tune = juce::jlimit (-1.0f, 1.0f, (float) xml.getDoubleAttribute ("tune", 0.0));
    return true;
}

//==============================================================================
std::vector<PresetEntry> PresetManager::user() const
{
    std::vector<PresetEntry> list;
    const auto files = userFolder().findChildFiles (juce::File::findFiles, false, "*.krkpatch");
    for (const auto& f : files)
        if (auto xml = juce::parseXML (f))
        {
            PresetEntry e;
            if (fromXml (*xml, e)) { e.origin = PresetEntry::Origin::User; list.push_back (std::move (e)); }
        }
    std::sort (list.begin(), list.end(), [] (const PresetEntry& a, const PresetEntry& b)
               { return a.name.compareIgnoreCase (b.name) < 0; });
    return list;
}

bool PresetManager::saveUser (const PresetEntry& entry, juce::File* written)
{
    PresetEntry e = entry;
    e.origin = PresetEntry::Origin::User;
    if (e.category.isEmpty() || e.category == "Recent") e.category = "User";
    if (e.timeMs == 0) e.timeMs = juce::Time::currentTimeMillis();
    const auto file = userFolder().getChildFile (juce::File::createLegalFileName (e.name) + ".krkpatch");
    const bool ok = exportPatch (file, e);
    if (ok && written != nullptr) *written = file;
    return ok;
}

bool PresetManager::exportPatch (const juce::File& file, const PresetEntry& entry)
{
    auto xml = toXml (entry);
    return file.replaceWithText (xml->toString());
}

bool PresetManager::exportBank (const juce::File& file, const std::vector<PresetEntry>& entries)
{
    juce::XmlElement bank (kBankTag);
    bank.setAttribute ("version", 1);
    for (const auto& e : entries) bank.addChildElement (toXml (e).release());
    return file.replaceWithText (bank.toString());
}

bool PresetManager::exportSyx (const juce::File& file, const std::vector<PresetEntry>& entries)
{
    std::vector<uint8_t> bytes;
    for (const auto& e : entries)
    {
        syx::Data d;
        d.name = e.name.toStdString();
        d.root = e.root;
        d.tuneCents = juce::roundToInt (e.patch.tune * 100.0f);
        d.level = e.patch.level;
        d.values.assign (e.patch.p.begin(), e.patch.p.end());
        const auto m = syx::encode (d);
        bytes.insert (bytes.end(), m.begin(), m.end());
    }
    return ! bytes.empty() && file.replaceWithData (bytes.data(), bytes.size());
}

std::vector<PresetEntry> PresetManager::importFile (const juce::File& file)
{
    std::vector<PresetEntry> out;
    if (file.hasFileExtension ("syx"))
    {
        juce::MemoryBlock mb;
        if (! file.loadFileAsData (mb)) return out;
        const auto* p = static_cast<const uint8_t*> (mb.getData());
        for (const auto& d : syx::decodeAll (std::vector<uint8_t> (p, p + mb.getSize())))
        {
            PresetEntry e;
            e.name = juce::String (d.name);
            e.category = "Imported";
            e.root = juce::jlimit (0, 127, d.root);
            e.patch = defaultPatch();
            for (size_t i = 0; i < (size_t) kNumParams && i < d.values.size(); ++i)
                e.patch.p[i] = juce::jlimit (0.0f, 1.0f, d.values[i]);
            e.patch.level = juce::jlimit (0.0f, 1.0f, d.level);
            e.patch.tune = (float) d.tuneCents * 0.01f;
            out.push_back (std::move (e));
        }
        return out;
    }
    auto xml = juce::parseXML (file);
    if (xml == nullptr) return out;
    PresetEntry e;
    if (xml->hasTagName (kPatchTag))
    {
        if (fromXml (*xml, e)) out.push_back (e);
    }
    else if (xml->hasTagName (kBankTag))
    {
        for (auto* child : xml->getChildIterator())
            if (fromXml (*child, e)) out.push_back (e);
    }
    return out;
}

//==============================================================================
void PresetManager::addRecent (PresetEntry entry)
{
    entry.origin = PresetEntry::Origin::Recent;
    entry.category = "Recent";
    if (entry.timeMs == 0) entry.timeMs = juce::Time::currentTimeMillis();
    recentList.erase (std::remove_if (recentList.begin(), recentList.end(),
                                      [&] (const PresetEntry& r) { return r.name == entry.name; }),
                      recentList.end());
    recentList.insert (recentList.begin(), std::move (entry));
    if ((int) recentList.size() > kMaxRecent) recentList.resize ((size_t) kMaxRecent);
    saveRecent();
}

void PresetManager::saveRecent() const
{
    juce::XmlElement bank (kBankTag);
    for (const auto& e : recentList) bank.addChildElement (toXml (e).release());
    recentFile().replaceWithText (bank.toString());
}

void PresetManager::loadRecent()
{
    recentList.clear();
    const auto f = recentFile();
    if (! f.existsAsFile()) return;
    for (auto& e : importFile (f))
    {
        e.origin = PresetEntry::Origin::Recent;
        e.category = "Recent";
        recentList.push_back (std::move (e));
        if ((int) recentList.size() >= kMaxRecent) break;
    }
}

} // namespace kraken

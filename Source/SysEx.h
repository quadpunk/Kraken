// SPDX-License-Identifier: MIT
// kr@ken patch <-> MIDI System Exclusive (.syx) messages. JUCE-free.
//
// This is kr@ken's OWN format (it is NOT a DX7 / any other synth's format - those cannot represent these patches).
// It uses the "non-commercial / educational" manufacturer ID 0x7D, so it never clashes with a real device.
//
//   F0 7D 'K' 'R' 'K' 'N' <version=01> <type=01 patch>
//   <name: 24 ASCII bytes, space padded> <root note> <tune cents + 64> <level: 2 bytes, 14 bit>
//   <parameter count> <count x 2 bytes, 14 bit, 0..16383 = 0..1>
//   <checksum: (128 - (sum of all bytes between the type byte and the checksum) mod 128) mod 128>  F7
//
// A bank is simply several messages concatenated in one file.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace kraken::syx
{
struct Data
{
    std::string name;
    int root = 60;
    int tuneCents = 0;                 // -50..+50
    float level = 0.7f;
    std::vector<float> values;         // normalised 0..1, in engine parameter order
};

inline void put14 (std::vector<uint8_t>& v, float x)
{
    const int n = (int) (std::min (1.0f, std::max (0.0f, x)) * 16383.0f + 0.5f);
    v.push_back ((uint8_t) ((n >> 7) & 0x7F));
    v.push_back ((uint8_t) (n & 0x7F));
}

inline std::vector<uint8_t> encode (const Data& d)
{
    std::vector<uint8_t> out { 0xF0, 0x7D, 'K', 'R', 'K', 'N', 0x01, 0x01 };
    const size_t bodyStart = out.size();
    for (size_t i = 0; i < 24; ++i)
    {
        char c = i < d.name.size() ? d.name[i] : ' ';
        if (c < 32 || c > 126) c = '?';
        out.push_back ((uint8_t) c);
    }
    out.push_back ((uint8_t) std::min (127, std::max (0, d.root)));
    out.push_back ((uint8_t) (std::min (50, std::max (-50, d.tuneCents)) + 64));
    put14 (out, d.level);
    out.push_back ((uint8_t) std::min<size_t> (127, d.values.size()));
    for (size_t i = 0; i < d.values.size() && i < 127; ++i) put14 (out, d.values[i]);
    unsigned sum = 0;
    for (size_t i = bodyStart; i < out.size(); ++i) sum += out[i];
    out.push_back ((uint8_t) ((128 - (sum & 0x7F)) & 0x7F));
    out.push_back (0xF7);
    return out;
}

// decodes one message starting at p[0] == 0xF0; returns false if it is not a valid kr@ken patch message
inline bool decode (const uint8_t* p, size_t n, Data& d, size_t* consumed = nullptr)
{
    if (n < 8 + 24 + 5 + 2 || p[0] != 0xF0 || p[1] != 0x7D || p[2] != 'K' || p[3] != 'R' || p[4] != 'K' || p[5] != 'N'
        || p[6] != 0x01 || p[7] != 0x01)
        return false;
    size_t end = 8;
    while (end < n && p[end] != 0xF7) ++end;
    if (end >= n) return false;
    if (consumed) *consumed = end + 1;

    unsigned sum = 0;
    for (size_t i = 8; i + 1 < end; ++i) sum += p[i];
    if (((128 - (sum & 0x7F)) & 0x7F) != p[end - 1]) return false;

    size_t i = 8;
    d = Data();
    d.name.assign ((const char*) p + i, 24);
    while (! d.name.empty() && d.name.back() == ' ') d.name.pop_back();
    i += 24;
    d.root = p[i++];
    d.tuneCents = (int) p[i++] - 64;
    auto get14 = [&] { const int v = (p[i] << 7) | p[i + 1]; i += 2; return (float) v / 16383.0f; };
    d.level = get14();
    const size_t count = p[i++];
    if (i + count * 2 + 1 != end) return false;
    for (size_t k = 0; k < count; ++k) d.values.push_back (get14());
    return true;
}

inline std::vector<Data> decodeAll (const std::vector<uint8_t>& bytes)
{
    std::vector<Data> out;
    for (size_t pos = 0; pos < bytes.size();)
    {
        if (bytes[pos] != 0xF0) { ++pos; continue; }
        Data d; size_t used = 0;
        if (decode (bytes.data() + pos, bytes.size() - pos, d, &used)) { out.push_back (std::move (d)); pos += used; }
        else ++pos;
    }
    return out;
}

} // namespace kraken::syx

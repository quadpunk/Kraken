// SPDX-License-Identifier: MIT
#include "../Source/SysEx.h"
#include "../Source/FactoryPresets.h"
#include <cstdio>
using namespace kraken;
int main()
{
    std::vector<uint8_t> bank; int n = 0, bad = 0; double maxErr = 0;
    for (const auto& d : factoryDefs()) {
        Patch p = factoryPatch(d); syx::Data s; s.name = d.name; s.root = d.root; s.level = p.level; s.tuneCents = 7;
        s.values.assign(p.p.begin(), p.p.end());
        auto m = syx::encode(s); for (auto b : m) if (b != 0xF0 && b != 0xF7 && b > 0x7F) { printf("non-7bit byte!\n"); return 1; }
        bank.insert(bank.end(), m.begin(), m.end()); ++n;
    }
    auto back = syx::decodeAll(bank);
    const auto& defs = factoryDefs();
    if (back.size() != defs.size()) { printf("count mismatch %zu\n", back.size()); return 1; }
    for (size_t i = 0; i < back.size(); ++i) {
        Patch p = factoryPatch(defs[i]);
        if (back[i].name != defs[i].name || back[i].root != defs[i].root || back[i].tuneCents != 7) ++bad;
        for (size_t k = 0; k < back[i].values.size(); ++k) maxErr = std::max(maxErr, (double)std::fabs(back[i].values[k] - p.p[k]));
    }
    // corruption must be rejected
    auto one = syx::encode(syx::Data{"x", 60, 0, .5f, std::vector<float>(77, .5f)}); one[40] ^= 0x01;
    syx::Data d; bool rejected = !syx::decode(one.data(), one.size(), d);
    printf("%d patches, %zu bytes, decoded %zu, mismatches %d, max param error %.6f, corrupt rejected %d\n", n, bank.size(), back.size(), bad, maxErr, rejected);
}

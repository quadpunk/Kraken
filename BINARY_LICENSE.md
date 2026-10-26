# Licence of the compiled plugin / app (binaries)

| What | Licence |
|---|---|
| kr@ken **source code** (this repository: `Source/`, `tools/`, `tests/`, factory patches, docs) | **MIT**, see [`LICENSE`](LICENSE) |
| **Compiled binaries** of kr@ken (VST3 plugin, Standalone app) built from this repository | **GNU Affero General Public License v3 (AGPL-3.0)**, see [`LICENSE-AGPL-3.0.txt`](LICENSE-AGPL-3.0.txt) |

## Why two licences
A compiled kr@ken contains the [JUCE](https://juce.com) framework, which is dual-licensed (AGPLv3 or a commercial JUCE
licence). kr@ken uses JUCE under the **AGPLv3** terms, so the combined, compiled work is distributed under the AGPLv3.
The MIT-licensed kr@ken source code is compatible with the AGPLv3, so anyone can still use, copy and modify the
*source* under the permissive MIT terms (for example with a different framework, or with their own JUCE licence).

## Source code offer (AGPLv3 section 6)
The complete corresponding source code of every released binary is available at:

    https://github.com/<YOUR-GITHUB-USER>/<YOUR-REPOSITORY>

(the commit or tag the binary was built from; JUCE 8.0.4 is fetched from <https://github.com/juce-framework/JUCE> by
CMake, see `CMakeLists.txt`). Build instructions are in `README.md`. If you redistribute a binary, you must pass on the
same rights and offer the corresponding source in the same way.

## What this means for you
* Using the plugin to make music, including commercial music, is not restricted by the AGPLv3: the licence covers the
  software, not the audio you create with it.
* You may redistribute the binaries, and modified versions, under the AGPLv3 and with source available.
* Your own patches and exported `.krkpatch` / `.krkbank` / `.syx` files are your data.

This summary is informational, not legal advice; the licence texts are authoritative.
See also [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

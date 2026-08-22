// Port of SpriteNames.java.
//
// Every sprite name present in the .pack atlases.
//
// A tile name can exist in the tiledefs (so it has properties) without existing
// in any atlas (so it has no pixels). Such a tile writes into the lotpack
// correctly, round-trips byte-identically, and renders in game as a
// missing-texture checkerboard. Property-based tile selection never catches it;
// only joining against this set does.
//
// This is the independent check on PackFile: after the legacy-layout fix the
// name count should rise well above 45,028 and vegetation_trees_01 tiles should
// resolve.
#pragma once

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace pzformat {

struct SpriteNamesResult {
    std::unordered_set<std::string> names;
    int packsRead = 0;
    int packsTotal = 0;
    std::vector<std::string> failedPacks; // known unparsed packs (UI/effects art)
};

/// Load every sprite name from every *.pack in a directory. Throws if the set
/// is empty, since palette validation would then pass vacuously.
SpriteNamesResult loadSpriteNames(const std::filesystem::path& texturePackDir);

} // namespace pzformat

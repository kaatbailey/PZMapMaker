// Port of TileDefs.java.
//
// Tile property definitions -- the semantic layer, text form.
//
// Parsed from the plaintext `*.tiles.txt` files shipped alongside the binary
// `.tiles`. This is what tells an editor that a tile IS a wall facing south, or
// a door, or a container -- as opposed to just a sprite index.
//
// Format:
//   version = 1
//   tileset {
//       file = advertising_01
//       size = 8,16          (width, height in tiles)
//       id   = 88
//       // advertising_01_0  <- authoritative name, used here to verify indexing
//       tile {
//           xy = 0,0
//           Facing = S       <- key/value
//           solid  =         <- valueless key: a boolean flag
//       }
//   }
//
// Tile name is `file + "_" + (y * width + x)`. That formula is checked against
// the `//` comment for every tile rather than assumed.
#pragma once

#include "tile.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pzformat {

class TileDefs {
public:
    /// Parse every *.tiles.txt in a directory, in sorted order.
    static TileDefs readAll(const std::filesystem::path& mediaDir);

    /// Parse one file, appending to this instance.
    void parse(const std::filesystem::path& file);
    /// Parse from an in-memory buffer. Used by tests and to parse a file whose
    /// bytes are already mapped.
    void parseText(std::string_view text);

    const Tile* find(const std::string& name) const {
        const auto it = byName_.find(name);
        return it == byName_.end() ? nullptr : it->second;
    }
    bool contains(const std::string& name) const { return byName_.count(name) != 0; }

    const std::vector<Tileset>& tilesets() const noexcept { return tilesets_; }
    const Tileset* tilesetByFile(const std::string& file) const {
        const auto it = tilesetByFile_.find(file);
        return it == tilesetByFile_.end() ? nullptr : it->second;
    }

    /// name -> tile, insertion-ordered like the Java LinkedHashMap so dumps and
    /// joins are reproducible.
    const std::vector<std::pair<std::string, const Tile*>>& ordered() const noexcept {
        return ordered_;
    }
    std::size_t tileCount() const noexcept { return ordered_.size(); }

    int nameMismatches() const noexcept { return nameMismatches_; }
    const std::vector<std::string>& mismatchSamples() const noexcept { return mismatchSamples_; }

private:
    void finishTile(Tileset* ts, Tile&& tile, const std::string& commentName);
    void rebuildIndex(); // pointers into tilesets_ are rebuilt after any growth

    std::vector<Tileset> tilesets_;
    std::unordered_map<std::string, const Tile*> byName_;
    std::vector<std::pair<std::string, const Tile*>> ordered_;
    std::unordered_map<std::string, const Tileset*> tilesetByFile_;
    int nameMismatches_ = 0;
    std::vector<std::string> mismatchSamples_;
};

} // namespace pzformat

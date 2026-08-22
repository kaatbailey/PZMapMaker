// Port of TileBin.java.
//
// Binary `.tiles` tile definitions.
//
// Needed because mods ship the binary with no plaintext sibling; vanilla ships
// both, which is what lets us validate this parser exactly.
//
//   char[4]   "tdef"
//   int32     version           (1)
//   int32     tilesetCount
//   tileset:
//       string  name
//       string  imageFile
//       int32   width, height   (in tiles)
//       int32   id
//       int32   ??? * extraInts (0 for retail 42.20)
//       int32   tileCount       (tiles carrying properties)
//       tile:   <shape> then int32 propCount, then propCount key/value strings
//
// All strings are '\n'-terminated (LE::cString). A valueless property is a key
// followed by an empty value.
//
// The per-tile prelude shape and the extraInts count were not readable by eye;
// they were searched and scored against the text-derived truth. For retail
// 42.20 the answer is COUNT_ONLY with extraInts=0, confirmed across every file
// with a text sibling. solve() and solveAll() reproduce that search.
#pragma once

#include "tile.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace pzformat {

/// How the fields before each tile's property list are laid out.
enum class TileShape {
    IndexCount,     ///< int32 index, int32 propCount
    XYCount,        ///< int32 x, int32 y, int32 propCount
    IndexUnkCount,  ///< int32 index, int32 unknown, int32 propCount
    CountOnly,      ///< int32 propCount only  (retail 42.20)
};

const char* toString(TileShape s) noexcept;

class TileBin {
public:
    static constexpr char kMagic[4] = {'t', 'd', 'e', 'f'};

    /// Parse from bytes. `extraInts` is the count of unknown int32s between the
    /// tileset id and the tile count; 0 for retail. Throws on any inconsistency
    /// so that solve() can score a shape by whether it parses to the exact end.
    static TileBin read(std::span<const std::byte> data, TileShape shape, int extraInts);
    static TileBin read(const std::filesystem::path& file, TileShape shape, int extraInts = 0);

    std::int32_t version() const noexcept { return version_; }
    std::int32_t tilesetCount() const noexcept { return tilesetCount_; }
    const std::vector<Tileset>& tilesets() const noexcept { return tilesets_; }

    const Tile* find(const std::string& name) const {
        const auto it = byName_.find(name);
        return it == byName_.end() ? nullptr : it->second;
    }
    std::size_t tileCount() const noexcept { return byName_.size(); }
    const std::unordered_map<std::string, const Tile*>& byName() const noexcept {
        return byName_;
    }

private:
    void rebuildIndex();

    std::vector<Tileset> tilesets_;
    std::unordered_map<std::string, const Tile*> byName_;
    std::int32_t version_ = 0;
    std::int32_t tilesetCount_ = 0;
};

} // namespace pzformat

// Port of TileIndex.java.
//
// Tile semantics: what a tile IS, not which sprite it draws.
//
// Loads every binary .tiles in a media directory (binary rather than the .txt
// siblings, because mods ship only the binary) and exposes classification over
// the result.
//
// The classification here is a HYPOTHESIS about what the property vocabulary
// means. validate() (in the Java, via room geometry) tests it against real map
// data: walls should lie on room perimeters, floors should cover room
// interiors. If the numbers disagree, the hypothesis is wrong — the same
// approach that caught the x/y transposition.
//
// The single most important lesson encoded here: edgeOf() deliberately EXCLUDES
// attachedN/attachedW. An earlier version keyed off them and validated at 99.5%
// against room geometry — a correlated proxy (decoration hangs on walls, so it
// sits in the same squares), not a correct classification. decorationEdge() is
// the separate accessor for the attached side. This split must survive the port
// intact; collapsing them reintroduces the bug that passing a test hid.
#pragma once

#include "tile.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pzformat {

class TileIndex {
public:
    enum class Kind { Floor, Wall, Door, Window, Object, Vegetation, Unknown };
    /// Which edge of a square a wall occupies. PZ walls are edge-based.
    enum class Edge { North, West, Both, None };

    /// Load every *.tiles in a media directory (binary, COUNT_ONLY layout).
    /// Files that fail to parse are skipped, as in the Java.
    static TileIndex load(const std::filesystem::path& mediaDir);

    /// Build directly from parsed tiles (tests, or an already-loaded set).
    /// First writer wins on name collision (Java putIfAbsent).
    void add(const Tile& t) { byName_.emplace(t.name, t); }

    const Tile* get(const std::string& name) const {
        const auto it = byName_.find(name);
        return it == byName_.end() ? nullptr : &it->second;
    }
    std::size_t size() const noexcept { return byName_.size(); }
    /// Every tile name, unordered. For digests/iteration.
    std::vector<std::string> names() const {
        std::vector<std::string> out;
        out.reserve(byName_.size());
        for (const auto& [n, t] : byName_) out.push_back(n);
        return out;
    }
    int fileCount = 0;
    int tilesetCount = 0;

    // --- classification (the hypothesis) ---
    Kind kindOf(std::string_view tileName) const;
    Edge edgeOf(std::string_view tileName) const;
    Edge decorationEdge(std::string_view tileName) const;

    bool isStructuralWall(std::string_view tileName) const;
    bool isWallFixture(std::string_view tileName) const;
    bool isDoorway(std::string_view tileName) const;
    bool isWindowWall(std::string_view tileName) const;
    bool isOverlay(std::string_view tileName) const;
    bool blocksMovement(std::string_view tileName) const;

    /// Container category, or nullopt.
    std::optional<std::string> containerType(std::string_view tileName) const;
    /// Object rotation (N/S/E/W), not wall orientation, or nullopt.
    std::optional<std::string> facing(std::string_view tileName) const;

private:
    const Tile* find(std::string_view name) const;
    static bool has(const Tile& t, std::string_view key) { return t.props.contains(key); }

    // Keyed by std::string; lookups take string_view via a transparent hash.
    struct SvHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
    };
    struct SvEq {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
    };
    std::unordered_map<std::string, Tile, SvHash, SvEq> byName_;
};

} // namespace pzformat

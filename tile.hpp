// The semantic-layer tile model, shared by the text parser (TileDefs) and the
// binary parser (TileBin). Eighteen downstream consumers read this type, so it
// lives on its own rather than nested in either parser.
//
// A tile's identity is `tileset + "_" + index`, where index = y*width + x. Its
// meaning is the property map: keys like Facing, solid, wall, doorN, container,
// FloorMaterial. Property keys are arbitrary strings; a valueless key (e.g.
// `solid`) is a boolean flag, stored as an empty value.
//
// Insertion order matters -- the text format lists properties in a fixed order
// and TileBin's verification compares whole property maps -- so props keeps
// insertion order rather than sorting. std::map would reorder; this uses a
// small ordered vector with map-like lookup.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pzformat {

/// Insertion-ordered string->string map. Small (tiles carry a handful of
/// properties), so linear lookup is fine and keeping order lets the binary
/// parser be checked against the text parser by whole-map equality.
class PropMap {
public:
    void put(std::string key, std::string value) {
        for (auto& [k, v] : kv_) {
            if (k == key) { v = std::move(value); return; }
        }
        kv_.emplace_back(std::move(key), std::move(value));
    }

    bool contains(std::string_view key) const {
        for (const auto& [k, v] : kv_) if (k == key) return true;
        return false;
    }

    /// The Java Map.get: value if present, else nullopt. An empty value is a
    /// present flag, distinct from absent.
    std::optional<std::string> get(std::string_view key) const {
        for (const auto& [k, v] : kv_) if (k == key) return v;
        return std::nullopt;
    }

    std::size_t size()  const noexcept { return kv_.size(); }
    bool        empty() const noexcept { return kv_.empty(); }

    auto begin() const noexcept { return kv_.begin(); }
    auto end()   const noexcept { return kv_.end(); }

    /// Whole-map equality, order-independent: two tiles with the same key/value
    /// pairs are equal even if parsed in a different order. The binary and text
    /// formats need not agree on order, only on content.
    bool operator==(const PropMap& other) const {
        if (kv_.size() != other.kv_.size()) return false;
        for (const auto& [k, v] : kv_) {
            const auto o = other.get(k);
            if (!o || *o != v) return false;
        }
        return true;
    }

private:
    std::vector<std::pair<std::string, std::string>> kv_;
};

struct Tile {
    std::string name;
    std::string tileset;
    int x = 0, y = 0, index = 0;
    PropMap props;

    bool flag(std::string_view k) const { return props.contains(k); }
    std::optional<std::string> get(std::string_view k) const { return props.get(k); }

    /// Wall/object facing: N, S, E, W, or nullopt.
    std::optional<std::string> facing() const { return props.get("Facing"); }
    bool solid() const { return props.contains("solid"); }
};

struct Tileset {
    std::string file;
    int width = 0, height = 0, id = -1;
    std::vector<Tile> tiles;
};

} // namespace pzformat

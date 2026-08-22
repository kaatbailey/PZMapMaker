#include "tileindex.hpp"

#include "tilebin.hpp"

#include <algorithm>

namespace pzformat {

TileIndex TileIndex::load(const std::filesystem::path& mediaDir) {
    std::vector<std::filesystem::path> bins;
    for (const auto& e : std::filesystem::directory_iterator(mediaDir)) {
        if (e.is_regular_file() && e.path().extension() == ".tiles") {
            bins.push_back(e.path());
        }
    }
    std::sort(bins.begin(), bins.end());

    TileIndex ti;
    for (const auto& b : bins) {
        try {
            const TileBin tb = TileBin::read(b, TileShape::CountOnly, 0);
            ++ti.fileCount;
            ti.tilesetCount += static_cast<int>(tb.tilesets().size());
            for (const auto& ts : tb.tilesets()) {
                for (const auto& t : ts.tiles) {
                    ti.byName_.emplace(t.name, t); // putIfAbsent: first wins
                }
            }
        } catch (const std::exception&) {
            // skipped, as in the Java
        }
    }
    return ti;
}

const Tile* TileIndex::find(std::string_view name) const {
    const auto it = byName_.find(name);
    return it == byName_.end() ? nullptr : &it->second;
}

TileIndex::Kind TileIndex::kindOf(std::string_view tileName) const {
    const Tile* t = find(tileName);
    if (!t) return Kind::Unknown;
    if (has(*t, "doorN") || has(*t, "doorW") || has(*t, "DoorWallN") || has(*t, "DoorWallW")) {
        return Kind::Door;
    }
    if (has(*t, "WindowShape")) return Kind::Window;
    if (has(*t, "wall") || has(*t, "WallOverlay")) return Kind::Wall;
    if (has(*t, "tree") || has(*t, "bush") || has(*t, "MoveWithWind")) return Kind::Vegetation;
    if (has(*t, "attachedFloor") || has(*t, "FloorOverlay")
        || tileName.rfind("floors_", 0) == 0 || tileName.rfind("blends_", 0) == 0) {
        return Kind::Floor;
    }
    return Kind::Object;
}

TileIndex::Edge TileIndex::edgeOf(std::string_view tileName) const {
    const Tile* t = find(tileName);
    if (!t) return Edge::None;

    if (has(*t, "WallNW")) return Edge::Both;
    const bool n = has(*t, "WallN") || has(*t, "DoorWallN") || has(*t, "WindowN")
                || has(*t, "doorN") || has(*t, "windowN");
    const bool w = has(*t, "WallW") || has(*t, "DoorWallW") || has(*t, "WindowW")
                || has(*t, "doorW") || has(*t, "windowW");
    if (n && w) return Edge::Both;
    if (n) return Edge::North;
    if (w) return Edge::West;

    // attachedN/attachedW deliberately excluded — see the header note and
    // STATE §11. A correlated proxy that validated at 99.5% is not a correct
    // classification. Use decorationEdge() for the attached side.
    return Edge::None;
}

TileIndex::Edge TileIndex::decorationEdge(std::string_view tileName) const {
    const Tile* t = find(tileName);
    if (!t) return Edge::None;
    const bool an = has(*t, "attachedN"), aw = has(*t, "attachedW");
    if (an && aw) return Edge::Both;
    if (an) return Edge::North;
    if (aw) return Edge::West;
    return Edge::None;
}

bool TileIndex::isStructuralWall(std::string_view tileName) const {
    const Tile* t = find(tileName);
    if (!t || isOverlay(tileName)) return false;
    return has(*t, "WallN") || has(*t, "WallW") || has(*t, "WallNW")
        || has(*t, "DoorWallN") || has(*t, "DoorWallW")
        || has(*t, "WindowN") || has(*t, "WindowW");
}

bool TileIndex::isWallFixture(std::string_view tileName) const {
    const Tile* t = find(tileName);
    if (!t) return false;
    return has(*t, "doorN") || has(*t, "doorW") || has(*t, "windowN") || has(*t, "windowW");
}

bool TileIndex::isDoorway(std::string_view tileName) const {
    const Tile* t = find(tileName);
    return t && (has(*t, "DoorWallN") || has(*t, "DoorWallW"));
}

bool TileIndex::isWindowWall(std::string_view tileName) const {
    const Tile* t = find(tileName);
    return t && (has(*t, "WindowN") || has(*t, "WindowW"));
}

bool TileIndex::isOverlay(std::string_view tileName) const {
    const Tile* t = find(tileName);
    if (t && (has(*t, "WallOverlay") || has(*t, "FloorOverlay"))) return true;
    return tileName.rfind("overlay_", 0) == 0;
}

bool TileIndex::blocksMovement(std::string_view tileName) const {
    const Tile* t = find(tileName);
    return t && (has(*t, "solid") || has(*t, "solidtrans"));
}

std::optional<std::string> TileIndex::containerType(std::string_view tileName) const {
    const Tile* t = find(tileName);
    if (!t) return std::nullopt;
    return t->props.get("container");
}

std::optional<std::string> TileIndex::facing(std::string_view tileName) const {
    const Tile* t = find(tileName);
    if (!t) return std::nullopt;
    return t->props.get("Facing");
}

} // namespace pzformat

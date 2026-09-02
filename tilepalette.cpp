// tilepalette.cpp — see tilepalette.hpp for the byName ordering resolution and
// the ASCII collation caveat.

#include "tilepalette.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace pzformat {

namespace {

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

} // namespace

bool TilePalette::flag(const TileIndex& ti, const std::string& name, const std::string& prop) {
    const Tile* t = ti.get(name);
    return t != nullptr && t->props.contains(prop);
}

std::optional<std::string> TilePalette::prop(const TileIndex& ti, const std::string& name,
                                             const std::string& key) {
    const Tile* t = ti.get(name);
    return t == nullptr ? std::nullopt : t->props.get(key);
}

std::optional<std::string> TilePalette::first(
        const std::function<bool(const std::string&)>& ok,
        const std::vector<std::string>& prefixes) {

    for (const std::string& prefix : prefixes) {
        std::vector<std::string> hits;
        // Raw hash-map order here, exactly as Java iterates keySet(). Safe:
        // `hits` is sorted before get(0), and droppedNoSprite is a complete
        // count of the prefix, not an order-dependent one. See PORT NOTE 1/2.
        for (const std::string& n : ti_->names()) {
            if (!startsWith(n, prefix) || !ok(n)) continue;
            if (sprites_->find(n) == sprites_->end()) {
                droppedNoSprite++;
                continue;
            }
            hits.push_back(n);
        }
        if (!hits.empty()) {
            std::sort(hits.begin(), hits.end());
            return hits.front();
        }
    }
    // Java: new TreeSet<>(ti.byName.keySet()) — sorted, deduplicated, and the
    // FIRST match wins, so this must iterate in sorted order and not in
    // whatever order names() happens to return.
    //
    // NOTE the local. Writing this as
    //     std::set<std::string> s(ti_->names().begin(), ti_->names().end());
    // calls names() TWICE, producing two distinct temporary vectors, and takes
    // begin() from one and end() from the other — undefined behaviour, and it
    // segfaulted. Caught by the corpus mode that empties every prefix so this
    // fallback is the only path taken. See FINDINGS_F4 §M.
    const std::vector<std::string> allNames = ti_->names();
    const std::set<std::string> sorted(allNames.begin(), allNames.end());
    for (const std::string& n : sorted) {
        if (ok(n) && sprites_->find(n) != sprites_->end()) return n;
    }
    return std::nullopt;
}

TilePalette TilePalette::pick(const TileIndex& ti,
                              const std::unordered_set<std::string>& sprites) {
    TilePalette p;
    p.sprites_ = &sprites;
    p.ti_ = &ti;

    // Ground. `grassFloor` is a bare flag and is the only thing separating
    // grass from dirt in blends_natural_01 — CustomName and Material are both
    // absent on every tile in that sheet. `solidfloor` excludes the
    // FloorOverlay edge-blend variants, which are corner pieces rather than
    // standalone ground.
    p.floorGrass = p.first([&](const std::string& n) {
            return flag(ti, n, "grassFloor") && flag(ti, n, "solidfloor")
                && !flag(ti, n, "FloorOverlay") && !ti.isOverlay(n); },
        {"blends_natural_01_", "blends_grassoverlays_01_", "blends_"});

    // Water. blends_natural_02 carries the `water` flag on its solid tiles.
    p.floorWater = p.first([&](const std::string& n) {
            return flag(ti, n, "water") && flag(ti, n, "solidfloor")
                && !flag(ti, n, "FloorOverlay") && !ti.isOverlay(n); },
        {"blends_natural_02_", "blends_"});

    // Road surface. Same overlay exclusion; the street sheet has no
    // grass/nature flags to key off.
    p.floorRoad = p.first([&](const std::string& n) {
            return ti.kindOf(n) == TileIndex::Kind::Floor && flag(ti, n, "solidfloor")
                && !flag(ti, n, "FloorOverlay") && !ti.isOverlay(n); },
        {"blends_street_01_", "floors_exterior_street_01_", "blends_"});

    // Interior floor. Wood reads as a house; Brick is bathroom and kitchen
    // tiling. Excluding the nature and exterior flags keeps outdoor ground out
    // of the running when the prefix falls through.
    p.floorInterior = p.first([&](const std::string& n) {
            const std::optional<std::string> m = prop(ti, n, "Material");
            return ti.kindOf(n) == TileIndex::Kind::Floor && flag(ti, n, "solidfloor")
                && !flag(ti, n, "FloorOverlay") && !flag(ti, n, "natureFloor")
                && !flag(ti, n, "grassFloor") && !flag(ti, n, "exterior")
                && m.has_value() && *m == "Wood"
                && !ti.isOverlay(n) && !ti.isStructuralWall(n); },
        {"floors_interior_tilesandwood_01_", "floors_interior_", "floors_"});

    p.wallNorth = p.first([&](const std::string& n) {
            return flag(ti, n, "WallN") && !ti.isOverlay(n)
                && !flag(ti, n, "DoorWallN") && !flag(ti, n, "WindowN"); },
        {"walls_exterior_house_01_", "walls_exterior_", "walls_"});
    p.wallWest = p.first([&](const std::string& n) {
            return flag(ti, n, "WallW") && !ti.isOverlay(n)
                && !flag(ti, n, "DoorWallW") && !flag(ti, n, "WindowW"); },
        {"walls_exterior_house_01_", "walls_exterior_", "walls_"});
    p.doorWallNorth = p.first([&](const std::string& n) {
            return flag(ti, n, "DoorWallN") && !ti.isOverlay(n); },
        {"walls_exterior_house_01_", "walls_exterior_", "walls_"});
    p.doorWallWest = p.first([&](const std::string& n) {
            return flag(ti, n, "DoorWallW") && !ti.isOverlay(n); },
        {"walls_exterior_house_01_", "walls_exterior_", "walls_"});

    // Interior partitions and their doors. Same property tests as the exterior
    // pair, preferring the interior sheet.
    p.interiorWallNorth = p.first([&](const std::string& n) {
            return flag(ti, n, "WallN") && !ti.isOverlay(n)
                && !flag(ti, n, "DoorWallN") && !flag(ti, n, "WindowN"); },
        {"walls_interior_house_01_", "walls_interior_", "walls_"});
    p.interiorWallWest = p.first([&](const std::string& n) {
            return flag(ti, n, "WallW") && !ti.isOverlay(n)
                && !flag(ti, n, "DoorWallW") && !flag(ti, n, "WindowW"); },
        {"walls_interior_house_01_", "walls_interior_", "walls_"});
    p.interiorDoorNorth = p.first([&](const std::string& n) {
            return flag(ti, n, "DoorWallN") && !ti.isOverlay(n); },
        {"walls_interior_house_01_", "walls_interior_", "walls_"});
    p.interiorDoorWest = p.first([&](const std::string& n) {
            return flag(ti, n, "DoorWallW") && !ti.isOverlay(n); },
        {"walls_interior_house_01_", "walls_interior_", "walls_"});

    // Corner and pillar variants for wall-joining (A3).
    p.wallNW = p.first([&](const std::string& n) {
            return flag(ti, n, "WallNW") && !ti.isOverlay(n); },
        {"walls_exterior_house_01_", "walls_exterior_", "walls_"});
    p.wallSE = p.first([&](const std::string& n) {
            return flag(ti, n, "WallSE") && !ti.isOverlay(n); },
        {"walls_exterior_house_01_", "walls_exterior_", "walls_"});
    p.interiorWallNW = p.first([&](const std::string& n) {
            return flag(ti, n, "WallNW") && !ti.isOverlay(n); },
        {"walls_interior_house_01_", "walls_interior_", "walls_"});
    p.interiorWallSE = p.first([&](const std::string& n) {
            return flag(ti, n, "WallSE") && !ti.isOverlay(n); },
        {"walls_interior_house_01_", "walls_interior_", "walls_"});

    // Declaration order here IS the order of `all`, which a cell header
    // declares — so it is the file's tile-index numbering. Java iterates this
    // exact array; do not reorder or sort it.
    const std::optional<std::string>* const fields[] = {
        &p.floorInterior, &p.floorRoad, &p.floorGrass, &p.floorWater,
        &p.wallNorth, &p.wallWest, &p.doorWallNorth, &p.doorWallWest,
        &p.wallNW, &p.wallSE,
        &p.interiorWallNorth, &p.interiorWallWest,
        &p.interiorDoorNorth, &p.interiorDoorWest,
        &p.interiorWallNW, &p.interiorWallSE};
    for (const std::optional<std::string>* s : fields) {
        if (s->has_value()
            && std::find(p.all.begin(), p.all.end(), **s) == p.all.end()) {
            p.all.push_back(**s);
        }
    }
    return p;
}

namespace {

const std::vector<std::vector<std::string>>& skinPrefixes() {
    static const std::vector<std::vector<std::string>> kSkins{
        {"walls_exterior_house_01_", "walls_exterior_house_"},
        {"walls_exterior_house_02_", "walls_exterior_house_"},
        {"walls_exterior_wooden_01_", "walls_exterior_wooden_"},
        {"walls_exterior_wooden_02_", "walls_exterior_wooden_"},
        {"walls_exterior_house_low_01_", "walls_exterior_"},
    };
    return kSkins;
}

} // namespace

std::vector<TilePalette::WallSkin> TilePalette::discoverSkins(
        const TileIndex& ti, const std::unordered_set<std::string>& sprites) {
    std::vector<WallSkin> skins;
    TilePalette tmp;
    tmp.sprites_ = &sprites;
    tmp.ti_ = &ti;
    for (const std::vector<std::string>& prefixes : skinPrefixes()) {
        const auto wn = tmp.first([&](const std::string& n) {
                return flag(ti, n, "WallN") && !ti.isOverlay(n)
                    && !flag(ti, n, "DoorWallN") && !flag(ti, n, "WindowN"); }, prefixes);
        const auto ww = tmp.first([&](const std::string& n) {
                return flag(ti, n, "WallW") && !ti.isOverlay(n)
                    && !flag(ti, n, "DoorWallW") && !flag(ti, n, "WindowW"); }, prefixes);
        const auto nw = tmp.first([&](const std::string& n) {
                return flag(ti, n, "WallNW") && !ti.isOverlay(n); }, prefixes);
        const auto se = tmp.first([&](const std::string& n) {
                return flag(ti, n, "WallSE") && !ti.isOverlay(n); }, prefixes);
        const auto dn = tmp.first([&](const std::string& n) {
                return flag(ti, n, "DoorWallN") && !ti.isOverlay(n); }, prefixes);
        const auto dw = tmp.first([&](const std::string& n) {
                return flag(ti, n, "DoorWallW") && !ti.isOverlay(n); }, prefixes);
        if (wn && ww && nw && se && dn && dw) {
            skins.push_back(WallSkin{*wn, *ww, *nw, *se, *dn, *dw});
        }
    }
    return skins;
}

std::optional<std::string> TilePalette::wallJoin(bool north, bool west, bool interior) const {
    if (north && west) return interior ? interiorWallNW : wallNW;
    if (north)         return interior ? interiorWallNorth : wallNorth;
    if (west)          return interior ? interiorWallWest : wallWest;
    return std::nullopt;
}

bool TilePalette::complete() const {
    return floorInterior.has_value() && floorRoad.has_value() && floorGrass.has_value()
        && wallNorth.has_value() && wallWest.has_value();
}

void TilePalette::verify() const {
    std::vector<std::string> missing;
    if (!floorInterior)      missing.push_back("floorInterior");
    if (!floorRoad)          missing.push_back("floorRoad");
    if (!floorGrass)         missing.push_back("floorGrass");
    if (!floorWater)         missing.push_back("floorWater");
    if (!wallNorth)          missing.push_back("wallNorth");
    if (!wallWest)           missing.push_back("wallWest");
    if (!doorWallNorth)      missing.push_back("doorWallNorth");
    if (!doorWallWest)       missing.push_back("doorWallWest");
    if (!interiorWallNorth)  missing.push_back("interiorWallNorth");
    if (!interiorWallWest)   missing.push_back("interiorWallWest");
    if (!interiorDoorNorth)  missing.push_back("interiorDoorNorth");
    if (!interiorDoorWest)   missing.push_back("interiorDoorWest");
    if (!wallNW)             missing.push_back("wallNW");
    if (!wallSE)             missing.push_back("wallSE");
    if (!interiorWallNW)     missing.push_back("interiorWallNW");
    if (!interiorWallSE)     missing.push_back("interiorWallSE");
    if (!missing.empty()) {
        std::string joined;
        for (std::size_t i = 0; i < missing.size(); i++) {
            if (i > 0) joined += ", ";
            joined += missing[i];
        }
        throw std::runtime_error(
            "TilePalette: no tile has both the required properties and a sprite for: " + joined);
    }
}

std::string TilePalette::describe(const std::optional<std::string>& name) const {
    if (!name.has_value()) return "null";
    const Tile* t = ti_->get(*name);
    if (t == nullptr) return *name;
    const std::optional<std::string> custom = t->props.get("CustomName");
    const std::optional<std::string> material = t->props.get("Material");
    if (custom.has_value() && !custom->empty()) {
        return *name + "  (\"" + *custom + "\""
             + (material.has_value() && !material->empty() ? ", " + *material : "") + ")";
    }
    // props is a LinkedHashMap in Java and an insertion-ordered PropMap here,
    // so this flag list is in tiledef parse order on both sides. It is the
    // fourth byName-shaped question from the chunk prompt and it resolves the
    // same way: deterministic, already matched, nothing to decide.
    std::string flags;
    bool firstFlag = true;
    for (const auto& [k, v] : t->props) {
        if (v.empty()) {
            if (!firstFlag) flags += ' ';
            flags += k;
            firstFlag = false;
        }
    }
    return *name + "  [" + flags + "]";
}

std::string TilePalette::toString() const {
    return "floor=" + describe(floorInterior)
         + "\n   road=" + describe(floorRoad)
         + "\n   grass=" + describe(floorGrass)
         + "\n   water=" + describe(floorWater)
         + "\n   wallN=" + describe(wallNorth)
         + "\n   wallW=" + describe(wallWest)
         + "\n   doorN=" + describe(doorWallNorth)
         + "\n   doorW=" + describe(doorWallWest)
         + "\n   intWallN=" + describe(interiorWallNorth)
         + "\n   intWallW=" + describe(interiorWallWest)
         + "\n   intDoorN=" + describe(interiorDoorNorth)
         + "\n   intDoorW=" + describe(interiorDoorWest)
         + "\n   extNW=" + describe(wallNW)
         + "\n   extSE=" + describe(wallSE)
         + "\n   intNW=" + describe(interiorWallNW)
         + "\n   intSE=" + describe(interiorWallSE)
         + "\n   dropped (properties but no sprite): " + std::to_string(droppedNoSprite);
}

} // namespace pzformat

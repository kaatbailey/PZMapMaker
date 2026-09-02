// Port of TilePalette.java (Track F, port step 5 — the palettes). Unit 4 of 4.
//
// Picks concrete vanilla tiles for generated map features.
//
// Selection is by PROPERTY, never by hardcoded name — a hardcoded name that
// does not exist in the player's build renders as nothing, and an invisible
// tile is indistinguishable from a failed write. Name prefixes are only a
// preference among tiles that already qualify.
//
// A candidate must also exist in the .pack atlases. Tiledefs and sprite atlases
// are independent sets: 61,418 tiles carry properties but only 45,028 have
// pixels, so a tile can satisfy every semantic filter and still draw nothing.
//
// Two selection bugs this replaces, both from taking the alphabetically first
// FLOOR under a prefix: grass resolving to blends_natural_01_101 (a legal
// exterior floor with no `grassFloor` flag, so ground rendered as bare dirt),
// and interior floor resolving to "Grey Diagonal Tiles" — a real bathroom tile,
// correctly rendered, wrong for every room in a house.
//
// ---------------------------------------------------------------------------
// PORT NOTES
//
// 1. THE byName ORDERING QUESTION, resolved. `first()` has two iteration sites
//    over TileIndex's hash map, and BOTH are order-independent by construction:
//
//      line 223  raw keySet() -> collects into `hits`, then Collections.sort
//                at 234 before get(0). The RESULT does not depend on order.
//      line 238  new TreeSet<>(keySet()) -> sorted by construction.
//
//    So C++'s std::unordered_map cannot diverge here, and option 1 of the
//    chunk prompt's three (prove the order never reaches the digest) applies.
//    See FINDINGS_F4 §E for the full trace. This is NOT true of TreePalette,
//    which is deferred.
//
// 2. `droppedNoSprite` is incremented INSIDE the raw loop, so it looks
//    order-dependent and is not: the loop never breaks early, so the count is a
//    complete scan of one prefix and is a property of the set, not the order.
//    What it DOES depend on is which prefix wins — `first()` returns at the
//    earliest prefix with any hit, so later prefixes are never scanned and
//    never counted. That is prefix-array order, which is fixed source order.
//
// 3. SORT COLLATION IS A REAL DIVERGENCE RISK, and it is ASCII-unreachable.
//    Java's Collections.sort and TreeSet order Strings by UTF-16 code unit.
//    C++ orders std::string by UNSIGNED BYTE (char_traits<char>::compare ->
//    memcmp semantics). For ASCII the two agree exactly. For any name with a
//    code point above U+007F they can disagree, because C++ is comparing UTF-8
//    bytes against Java's UTF-16 units. Every vanilla tile name is ASCII, so
//    this is unreachable on real data — but it is unreachable by DATA, not by
//    construction, and a mod could ship a non-ASCII tile name. The oracle tests
//    this deliberately; see FINDINGS_F4.
//
// 4. `sprites` defaults to Java's Set.of(), whose iteration order is RANDOMISED
//    PER RUN (STATE §40). It is used only through contains(), so nothing
//    escapes — but do not add an iteration over it on either side.
//
// Dependency-free: standard library only, per CHARTER §3.
#pragma once

#include "tileindex.hpp"

#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace pzformat {

class TilePalette {
public:
    // nullopt reproduces Java's null: "no tile qualified".
    std::optional<std::string> floorInterior, floorRoad, floorGrass, floorWater;
    std::optional<std::string> wallNorth, wallWest;
    std::optional<std::string> doorWallNorth, doorWallWest;

    /// Corner (WallNW) and pillar (WallSE) — the joined variants.
    std::optional<std::string> wallNW, wallSE;

    /// Partitions between rooms. The exterior sheet reads wrong indoors.
    std::optional<std::string> interiorWallNorth, interiorWallWest;
    std::optional<std::string> interiorDoorNorth, interiorDoorWest;
    std::optional<std::string> interiorWallNW, interiorWallSE;

    std::vector<std::string> all;

    /// Candidates that had the right properties but no sprite.
    int droppedNoSprite = 0;

    static TilePalette pick(const TileIndex& ti,
                            const std::unordered_set<std::string>& sprites);

    /// True if the tile carries `prop`, as a bare flag or with a value.
    static bool flag(const TileIndex& ti, const std::string& name, const std::string& prop);
    /// Java's prop(): the value, or nullopt when tile or key is absent.
    static std::optional<std::string> prop(const TileIndex& ti, const std::string& name,
                                           const std::string& key);

    /// A complete exterior wall skin for one building.
    struct WallSkin {
        std::string wallN, wallW, wallNW, wallSE, doorN, doorW;
        std::string label() const { return wallN.substr(0, wallN.rfind('_')); }
    };

    static std::vector<WallSkin> discoverSkins(const TileIndex& ti,
                                               const std::unordered_set<std::string>& sprites);

    /// Resolve the wall configuration on a square to a single tile index.
    /// nullopt when there is no wall on this square.
    std::optional<std::string> wallJoin(bool north, bool west, bool interior) const;

    /// First qualifying tile, preferring the earliest matching prefix.
    /// A tile qualifies only if it also has a sprite.
    std::optional<std::string> first(const std::function<bool(const std::string&)>& ok,
                                     const std::vector<std::string>& prefixes);

    bool complete() const;

    /// Fail before generating anything. A bad palette entry costs a full
    /// regeneration plus an in-game load to notice.
    /// Throws std::runtime_error (Java: IllegalStateException).
    void verify() const;

    std::string toString() const;

private:
    std::string describe(const std::optional<std::string>& name) const;

    const std::unordered_set<std::string>* sprites_ = nullptr;
    const TileIndex* ti_ = nullptr;
};

} // namespace pzformat

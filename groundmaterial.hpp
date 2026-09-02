// Port of GroundMaterial.java (Track F, port step 5 — the palettes).
//
// The natural ground materials, their tile blocks, and their blend priority.
//
// Priority is MEASURED, not derived — STATE §27, over 4,065 cells of the retail
// Muldraugh map. The higher-priority material supplies the mask tile; the
// lower-priority square carries it.
//
//   Grass_Dark > Grass_Medium > Grass_Light > Sand > Dirt_Grass > Dirt > Clay
//
// It is not block-index order (0, 16, 32, 48, 64, 80, 96 against a priority
// order of 16, 32, 48, 0, 80, 64, 96) and it is not brightness — dark grass
// outranks light grass, but pale Sand outranks dark Dirt. Do not try to
// compute it. Do not re-measure it.
//
// Block layout, for E9's mask pass (STATE §26, §27):
//   B+0, B+5, B+6, B+7    solid variants, interchangeable
//   B+1..B+4              corner masks NW, ES, SW, EN
//   B+8..B+15             side masks N, W, E, S in two variant sets
//
// PORT NOTES — the three places C++ cannot mirror Java shape-for-shape:
//
//   1. Java's enum is an ordered set of singletons with identity semantics.
//      Here it is a fixed table plus a stable `ordinal`, and identity is the
//      pointer into values(). Declaration order IS the contract — Java's
//      values() order is the declaration order, and the oracle digests it.
//      Note that declaration order is NOT rank order: ROAD_04 is declared
//      before ROAD_03 but ranks 12 against 13. Anything that sorts the table
//      by rank and calls it equivalent has changed the contract.
//
//   2. `solidIndices()` returns by value, mirroring Java's `solids.clone()`.
//      Returning a reference to the table would be faster and — measured, see
//      the palettes FINDINGS — produce no digest diff, because no caller in
//      either tree mutates the result. It is kept as a copy because the Java
//      contract is a copy, not because a test forced it.
//
//   3. Java's `outranks(null)` is false; C++ has no null enum constant, so the
//      parameter is a pointer and nullptr reproduces it. Also unreachable from
//      any caller in this tree, and also kept for contract fidelity.
//
// Dependency-free: standard library only, per CHARTER §3. Measured to compile
// standalone on the Java side too (`javac` on GroundMaterial.java alone), which
// is why it is the first unit of this chunk to port.
#pragma once

#include "java_random.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace pzformat {

class GroundMaterial {
public:
    /// Tile name prefixes. Masks key on FloorMaterial, never on sheet (§27).
    static constexpr std::string_view NATURAL = "blends_natural_01_";
    static constexpr std::string_view STREET = "blends_street_01_";

    /// Stable index into values(), mirroring Java's Enum.ordinal().
    int ordinal = 0;
    /// The enum constant name, mirroring Java's Enum.name().
    std::string_view name;
    /// The FloorMaterial property value, as it appears in the tiledefs.
    std::string_view floorMaterial;
    /// Tile name prefix.
    std::string_view sheet;
    /// First index of this material's block.
    int block = 0;
    /// 2 for blends_natural_01, 1 for blends_street_01 (§27).
    int variantSets = 0;
    /// 0 is highest priority. Lower rank masks onto higher rank.
    int rank = 0;

    /// The interchangeable solid variants for this material.
    /// Returns a copy — Java's solids.clone(). See PORT NOTE 2.
    std::vector<int> solidIndices() const {
        return std::vector<int>(solids_.begin(), solids_.begin() + solidCount_);
    }

    /// Tile name of one solid variant, chosen uniformly — STATE §21.
    /// Draws exactly once from rng, as Java does. The draw count is part of
    /// the contract: a second draw here desynchronises every downstream unit
    /// sharing the stream.
    std::string solid(JavaRandom& rng) const {
        const std::int32_t i = rng.nextInt(solidCount_);
        return std::string(sheet) + std::to_string(solids_[static_cast<std::size_t>(i)]);
    }

    /// True when this material's masks are drawn onto `other`.
    /// nullptr reproduces Java's outranks(null) == false. See PORT NOTE 3.
    bool outranks(const GroundMaterial* other) const {
        return other != nullptr && rank < other->rank;
    }

    /// All fourteen materials, in Java declaration order.
    static const std::array<GroundMaterial, 14>& values();

    /// Java's byFloorMaterial: the match, or nullptr.
    static const GroundMaterial* byFloorMaterial(std::string_view s);

    // Construction is internal to the table; the type is a value in an
    // ordered set, not something callers build.
    constexpr GroundMaterial(int ord, std::string_view nm, std::string_view fm,
                             std::string_view sh, int blk,
                             std::array<int, 4> sol, int solCount,
                             int sets, int rk)
        : ordinal(ord), name(nm), floorMaterial(fm), sheet(sh), block(blk),
          rank(rk), solids_(sol), solidCount_(solCount) {
        variantSets = sets;
    }

private:
    std::array<int, 4> solids_{};
    /// ROAD_01 and ROAD_02 carry TWO solids, not four — B+6 and B+7 are
    /// spriteless in blends_street_01. This is why the count is stored rather
    /// than assumed to be 4, and why nextInt sees two different bounds.
    std::int32_t solidCount_ = 0;
};

} // namespace pzformat

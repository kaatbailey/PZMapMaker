// Port of GroundPalette.java (Track F, port step 5 — the palettes).
//
// Outdoor ground composed the way vanilla composes it.
//
// MEASURED over 262,144 squares across Muldraugh cells 27_27, 35_35, 30_30 and
// 40_40 (see pzformat.GroundSurvey). Authoring one flat tile everywhere is what
// made generated ground read as a hard rectangle against procedurally generated
// neighbours; vanilla ground is a weighted mix with a partial tuft layer.
//
// NAMING. This class writes the TUFT layer — `blends_grassoverlays_01`,
// VEGETATION, MoveWithWind, canBeRemoved. It does NOT write blend masks; those
// come from GroundRegions.addMasks and live on the base sheet with
// FloorMaterial and FloorAttachment* set. Three layers, three names: solid,
// mask, tuft (STATE §26, §27).
//
// What the survey showed:
//
//   - Base tiles fall into groups of four sharing a tuft rate. Within a group
//     the four variants occur in near-equal proportion, so the variant is
//     picked uniformly at random per square.
//   - Tuft rate is a property of the GROUP, not a global constant.
//   - Overall 43.3% of ground squares carry a tuft, and NEVER more than one —
//     0 squares out of 257,703 had two.
//   - The tuft sheet is 8 wide with only columns 0-5 usable; columns 6 and 7
//     are FLOOR-classified with no sprite. Row frequency falls off sharply.
//
// ---------------------------------------------------------------------------
// PORT NOTES
//
// 1. FLOATING POINT. This is the first unit of the chunk that computes in
//    doubles: cumulative weight tables, `nextDouble() * total`, and two
//    threshold comparisons. Java has been strict-FP since 17, and every
//    operation here is a bare add, multiply or compare — there is no `a*b+c`
//    anywhere, so there is no FMA-contraction opportunity for the compiler to
//    take. That is why this ports cleanly where §39's `minAreaRect` did not:
//    the divergence there was transcendentals, and there are none here.
//    If a future edit introduces one, this note is the reason it broke.
//
// 2. `sprites` is a HashSet in Java and used only through contains(), so no
//    iteration order reaches anything. Any set type works on this side.
//
// 3. `pick()` PRINTS to stdout when it drops tiles, and THROWS when every
//    group is dropped. Both are behaviour, both are reproduced. The throw is
//    reachable from a synthetic sprite set and the oracle exercises it.
//
// 4. `all` is the tile list a cell header declares, so its ORDER is the
//    header's tile-index numbering. It is built from GROUPS order then tuft
//    row-major order — both ArrayList, no hashing — so unlike TreePalette this
//    unit has no ordering hazard. Do not "improve" it into a set.
//
// Dependency-free: standard library only, per CHARTER §3.
#pragma once

#include "java_random.hpp"
#include "tileindex.hpp"

#include <array>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace pzformat {

class GroundPalette {
public:
    static constexpr const char* BASE_SHEET = "blends_natural_01_";
    // NOTE the sheet keeps TIS's spelling. We renamed our own identifiers from
    // "overlay" to "tuft"; the tileset is theirs and is still called
    // blends_grassoverlays_01. A blind rename ate this literal once already and
    // silently zeroed the tuft layer.
    static constexpr const char* TUFT_SHEET = "blends_grassoverlays_01_";

    struct BaseGroup {
        std::array<int, 4> indices;
        double weight;      ///< share of ground squares, as measured
        double tuftRate;    ///< probability a square of this group carries a tuft
        const char* label;
    };

    /// Grass only. The dirt groups were here at a combined 13.9% and read as
    /// bare diamonds through forest, because biomes/map/ph_forest.lua excludes
    /// those tiles from PLANT and BUSH placement and genMapSquare then deletes
    /// any vegetation on them. Weights are relative and normalised at use.
    static const std::array<BaseGroup, 3>& groups();

    /// Kept for deliberate use: tracks, yards, unpaved roads.
    static constexpr std::array<int, 4> DIRT{64, 69, 70, 71};
    static constexpr std::array<int, 4> DIRT_GRASS{80, 85, 86, 87};

    /// Per-tile weight for each row of the tuft sheet, rows 0..8.
    static constexpr std::array<double, 9> TUFT_ROW_WEIGHT{
        6.20, 4.05, 3.48, 1.07, 0.74, 0.68, 0.20, 0.16, 0.16};
    static constexpr int TUFT_ROW_WIDTH = 8;
    static constexpr int TUFT_USABLE_COLS = 6;

    /// Base tile plus an optional tuft. nullopt where the group is not overlaid.
    struct Ground {
        std::string base;
        std::optional<std::string> tuft;
    };

    /// Every tile name used, so the cell header can declare them. ORDER IS THE
    /// HEADER'S TILE NUMBERING — see PORT NOTE 4.
    std::vector<std::string> all;

    /// Keeps only tiles that both exist in the tiledefs and have a sprite. A
    /// group missing any variant is dropped whole rather than silently skewed.
    /// Throws std::runtime_error (Java: IllegalStateException) when no group
    /// survives.
    static GroundPalette pick(const TileIndex& ti,
                              const std::unordered_set<std::string>& sprites);

    static double rowWeightOf(const std::string& tuftName);

    Ground roll(JavaRandom& rng) const;

    std::string toString() const;

    std::size_t groupCount() const { return groups_.size(); }
    std::size_t tuftCount() const { return tufts_.size(); }

private:
    GroundPalette(const std::vector<BaseGroup>& keptGroups,
                  const std::vector<std::string>& keptTufts);

    std::vector<BaseGroup> groups_;
    std::vector<double> groupCumulative_;
    std::vector<std::string> tufts_;
    std::vector<double> tuftCumulative_;
};

} // namespace pzformat

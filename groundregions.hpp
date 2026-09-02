// Port of GroundRegions.java (Track F, port step 6 — second half). 308 lines Java.
//
// Assigns one ground material per square, then dithers the boundaries.
//
// WHY THIS EXISTS. Generated ground read as scattered tan diamonds because
// GroundPalette rolled Grass_Dark / Grass_Medium / Grass_Light per square from
// measured frequencies. STATE §26: those three are REGION distinctions being
// used as texture. Vanilla runs 16 identical squares in a row; we changed three
// times in eight. The fix is regions, not a different mix.
//
// WHAT THE DATA SUPPORTS.
//   NONE far from anything   -> GRASS_DARK    one region, the 58.6% majority
//   ring around BUILDING     -> SAND          §26 Q4, a fenced yard
//   verge along ROAD         -> GRASS_MEDIUM  §26, Medium as a verge at 42_40
//
// THE DITHER LAW — MEASURED, E8 Part 1. Dither is INDEPENDENT PER SQUARE, not a
// noise field: matched-distance lift is 0.95-1.14 on the boundary contour. So a
// distance transform from the region edge, then one Bernoulli draw per square at
// P[d]. No noise field, no correlation length.
//
// SEEDING. GisCells seeds its Random per cell so a cell regenerates identically
// whether or not its neighbours are written. The dither flip is therefore driven
// by a POSITION HASH, not by that sequential Random — otherwise the same world
// square dithers differently depending on which cell is being written and every
// cell boundary becomes a visible seam.
//
// ------------------------------------------------------------------------
// PORT NOTES — the five places C++ cannot mirror Java shape-for-shape.
// ------------------------------------------------------------------------
//
//   1. hash01 IS THE UB HAZARD OF THIS UNIT, and it is the same shape as the
//      java_random.hpp bug found in step 6a (FINDINGS F5).
//
//      Java computes the SplitMix64 finaliser in `long`, where every one of the
//      four constants exceeds 2^63 (so they are NEGATIVE Java longs) and every
//      multiply overflows. Java defines that as two's-complement wraparound.
//      SIGNED OVERFLOW IS UNDEFINED BEHAVIOUR IN C++, and at -O2 the compiler
//      may assume it cannot happen — exactly what deleted nextInt's rejection
//      branch and diverged 6 seeds in 20,000 before it was found.
//
//      Everything therefore runs in std::uint64_t, where wraparound is defined.
//      Java's `>>>` is a LOGICAL shift; C++ `>>` on a signed integer is
//      arithmetic, so the operand type is load-bearing here too, not incidental.
//      `(long) gx` sign-extends before the multiply — the cast chain below
//      reproduces that, and dropping it silently changes every negative
//      coordinate.
//
//      Do not "simplify" any of this to int64_t. The falsifier is in the oracle:
//      the H section feeds negative and INT_MIN/INT_MAX positions, and the
//      documented mutation is to run the arithmetic in int64_t at -O2.
//
//   2. Java's `GroundMaterial[][]` holds enum references with null meaning
//      "no material" (a BUILDING interior, which never blends). C++ has no null
//      enum constant, so a square is `const GroundMaterial*` into the table
//      returned by GroundMaterial::values(), and nullptr reproduces null.
//      IDENTITY IS THE POINTER: Java compares constants with `!=`, not equals,
//      and this mirrors that. Never copy a GroundMaterial by value into a grid.
//
//   3. `byMat` is an EnumMap (GroundRegions.java:163), which iterates in ORDINAL
//      order and is backed by an array indexed by ordinal. It is reproduced as
//      exactly that — a fixed array of 14 slots walked 0..13 — not as a
//      std::map keyed on a pointer (address order is not ordinal order) and not
//      as an unordered_map. Java's declaration order is NOT rank order
//      (ROAD_04 is declared before ROAD_03), so sorting by rank is a silent
//      behaviour change. Same resolution as MaskRule's DirSet.
//
//   4. DX/DY IS NOT MaskRule::Dir's ORDER, and the difference is observable.
//      MaskRule::Dir is N, W, E, S (ord 0..3). GroundRegions::DX/DY is
//      {0,0,-1,1}/{-1,1,0,0} — that is N, S, W, E. edgeDistance's seed loop
//      BREAKS at the first differing neighbour in this order and propagates
//      that neighbour's material into `across`, so the order reaches the
//      output. Unifying the two tables would look like a tidy-up and would
//      change the dither. Left separate deliberately.
//
//   5. `across` is an out-parameter in Java, allocated null by the caller.
//      Here it is a reference resized to n x n of nullptr on entry. The only
//      caller passes a fresh array, so this is equivalent; it is spelled out
//      because a caller passing a populated grid would differ.
//
// Dependency-free: standard library only, per CHARTER §3.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "celldata.hpp"
#include "gisimport.hpp"
#include "groundmaterial.hpp"
#include "java_random.hpp"
#include "maskrule.hpp"

namespace pzformat {

class GroundRegions {
public:
    /// One square's material, or nullptr for "none". See PORT NOTE 2.
    using Cell = const GroundMaterial*;
    /// Column-major like the Java: grid[x][y].
    using Grid = std::vector<std::vector<Cell>>;

    /// P(square flips to the neighbouring material) by distance from the edge.
    /// FITTED to four data points against vanilla 42_40, not derived — STATE
    /// §28. Vanilla's measured P(minority|d) is a different quantity and using
    /// it directly over-produced isolated squares 4x.
    static constexpr std::array<double, 4> P{0.06, 0.03, 0.01, 0.005};

    /// How far a yard extends from a building footprint, in squares.
    /// A GUESS, isolated as a constant. STATE §28 records YARD = 3 and §29
    /// OPEN 1 records that yards read wider than 3 suggests; the Java file at
    /// 022d938 says 1 and the change is undocumented. Ported as 1 because the
    /// oracle compares against that file — changing it is a behaviour change,
    /// not a port, and belongs in a chunk that measures it.
    static constexpr int YARD = 1;

    /// How far a verge extends from a road, in squares. Also a guess (§28).
    static constexpr int VERGE = 2;

    /// Computation margin. Only distances below P.size() matter to the dither,
    /// so 8 leaves ample headroom for the region assignment and the edge
    /// distance transform to be correct across the whole returned window.
    static constexpr int MARGIN = 8;

    /// Neighbour offsets. N, S, W, E — NOT MaskRule::Dir's order.
    /// See PORT NOTE 4 before touching these.
    static constexpr std::array<int, 4> DX{0, 0, -1, 1};
    static constexpr std::array<int, 4> DY{-1, 1, 0, 0};

    /// Material per square for one 256x256 cell, dithered.
    ///
    /// Reads cover at GLOBAL coordinates so regions are continuous across cell
    /// borders. Squares outside the raster are treated as open country rather
    /// than left null — leaving them empty flips the whole 8x8 chunk to
    /// procedural generation (STATE §7).
    ///
    /// Returns 258x258: the cell plus a one-square border, so a neighbour off
    /// the cell edge resolves to the adjacent cell's material.
    static Grid build(const GisImport& g, int ox, int oy, std::int64_t seed);

    /// Append this square's blend masks to its tile stack.
    ///
    /// For each distinct neighbouring material that OUTRANKS this square's, the
    /// mask rule runs independently and the results concatenate (§27).
    ///
    /// Call AFTER the solid tile and BEFORE the tuft: the solid must be first
    /// in the stack or getFloor() and cleanChunk read the wrong tile (§26).
    ///
    /// @param region bordered array from build(), so cell-local (x,y) is
    ///               region[x+1][y+1]
    static void addMasks(std::vector<std::int32_t>& stack, CellData& cell,
                         const Grid& region, int x, int y,
                         Cell self, JavaRandom& rng);

    /// One independent Bernoulli draw per square at P[distance-to-edge].
    /// Reads from a snapshot so a flipped square cannot seed further flips —
    /// dither is a property of the region edge, not a growth process.
    static void dither(Grid& mat, int ox, int oy, std::int64_t seed);

    /// 4-connected BFS distance to the nearest square of a different material,
    /// propagating WHICH material that is into `across`. -1 where unreachable.
    static std::vector<std::vector<int>> edgeDistance(const Grid& m, Grid& across);

    /// BFS distance to a cover class, over a margin-extended window.
    /// Unreached squares carry Integer.MAX_VALUE, as the Java does.
    static std::vector<std::vector<int>> coverDistance(const GisImport& g,
                                                       int ox, int oy, int margin,
                                                       GisImport::Cover target);

    /// Cover at a global position; outside the raster is NONE.
    static GisImport::Cover coverAt(const GisImport& g, int gx, int gy);

    /// Position-hashed uniform in [0,1). Depends only on world position and the
    /// world seed, never on iteration order. SEE PORT NOTE 1 — the uint64_t is
    /// the whole point.
    static double hash01(int gx, int gy, std::int64_t seed);

    /// Roads take part in masking but never in dither.
    static bool isRoad(Cell m);

private:
    GroundRegions() = delete;
};

} // namespace pzformat

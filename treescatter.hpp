// Port of TreeScatter.java (Track F, port step 7). 210 lines Java.
//
// Tree placement driven by distance from habitation. Distance to the nearest
// building or road is a proxy for how long ground has gone undisturbed, so it
// drives how DENSE the trees are and whether they start as saplings or full
// trees. Yards and verges stay open, regrowth thickens outward.
//
// THIS UNIT IS LIVE. STATE §42, 2026-09-02: the A2-gate resolved against
// deletion. With the write at GisCells.java:237-239 commented out, world
// 51381,51380 has ZERO trees in game; with it enabled, dense forest. The
// engine adds no trees of its own and does not re-scatter ours. Every tree in
// a generated map comes from here.
//
// ------------------------------------------------------------------------
// PORT NOTES
// ------------------------------------------------------------------------
//
//   1. THE RNG DRAW COUNT IS THE CONTRACT, NOT JUST THE VALUES.
//      Per square the sequence is: ONE nextDouble for the stump roll, but ONLY
//      when `hasStump` — Java's `tp.hasStump && rng.nextDouble() < P_STUMP`
//      short-circuits, so a palette with no stump tile draws a different number
//      of times for every square in the raster. Then one nextDouble for the
//      density roll, then nextInt(size) only if that passed and the square is
//      not too close. Any deviation desynchronises the whole stream and every
//      later square diverges. The oracle fingerprints the stream after each
//      pass for exactly this reason.
//
//   2. nextInt's BOUND IS THE PALETTE SIZE, AND IN PRODUCTION IT IS 8.
//      A power of two, so java.util.Random takes the high-bits fast path and
//      THE REJECTION LOOP IS NEVER ENTERED — the branch that broke at -O2 in
//      step 6a (FINDINGS F5) is unreachable from a real run. The corpus
//      therefore uses non-power-of-two palette sizes on purpose. Rejection
//      probability is (2^31 mod bound) / 2^31; a convenient bound proves
//      nothing, which is the palettes-chunk lesson (§41).
//
//   3. dx/dy HERE IS A THIRD DISTINCT DIRECTION ORDER, AND UNLIKE THE OTHER
//      TWO IT IS NOT OBSERVABLE. MaskRule::Dir is N,W,E,S; GroundRegions::DX/DY
//      is N,S,W,E; this is {1,-1,0,0}/{0,0,1,-1} = E,W,S,N. In GroundRegions
//      the order reached the output because edgeDistance BREAKS at the first
//      differing neighbour. Here it only affects the order squares enter the
//      queue, and BFS shortest-path distances are uniquely determined
//      regardless. Reproduced anyway — reordering it is a mutation that must
//      be shown null rather than assumed so, and §40's rule is that a verdict
//      in one unit does not transfer to another.
//
//   4. THE QUEUE IS SIZED w*h AND THAT IS EXACTLY SUFFICIENT, but only because
//      of an invariant worth stating. The relaxation `if (dist[nx][ny] > nd)`
//      is not a visited flag; a square could in principle be enqueued twice.
//      It cannot here: unit edge costs plus FIFO order mean distances are
//      discovered non-decreasing, so once a square's distance is set, every
//      later relaxation offers an equal or greater value and fails the test.
//      Each square is therefore enqueued at most once. A std::vector with
//      reserve() reproduces the array; the bound is not a guess.
//
//   5. `dist[x][y] + 1` CANNOT OVERFLOW even though unreached squares hold
//      Integer.MAX_VALUE. Only dequeued squares are incremented, and a square
//      is enqueued only after its distance is set to a real value. Left as
//      plain int arithmetic to mirror the Java; if that invariant ever breaks
//      this is signed overflow and therefore UB in C++, so the oracle carries
//      a no-structure case that drives every square to MAX_VALUE.
//
//   6. THE CELL ENCODING IS `x * h + y`, NOT `x * w + y`. Decoded as
//      `x = cur / h, y = cur % h`. Mirrored exactly; using w would be a silent
//      transposition on any non-square raster, which is why the corpus
//      includes rasters that are wider than tall AND taller than wide.
//
// Dependency-free: standard library only, per CHARTER §3.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "gisimport.hpp"
#include "java_random.hpp"
#include "treepalette.hpp"

namespace pzformat {

class TreeScatter {
public:
    /// Squares this close to a structure stay clear. Yards and verges.
    static constexpr int CLEAR = 3;

    /// Minimum tiles between trunks.
    static constexpr int SPACING = 2;

    /// Chance per eligible square of a stump instead of a tree.
    static constexpr double P_STUMP = 0.0010;

    struct Band {
        int maxDist;        ///< upper bound of the band
        int size;           ///< `tree` size class to author (1 sapling, 2 tree)
        double density;     ///< chance per eligible square
        const char* label;
    };

    static const std::array<Band, 4>& bands();

    /// Tile name per raster square, empty string where nothing goes.
    /// (Java returns null; an empty string is the sentinel here, and the
    /// caller tests it the same way.)
    using Placement = std::vector<std::vector<std::string>>;

    /// @param log receives the same lines Java prints to stdout, so the step 7
    ///        oracle can compare them. Java writes straight to System.out;
    ///        capturing instead keeps the unit free of I/O policy.
    static Placement place(const GisImport& g, const TreePalette& tp,
                           std::int64_t seed, std::vector<std::string>& log);

    static int bandFor(int d);

    /// BFS distance from every building or road square. Squares that ARE
    /// structure get 0, so bands measure outward from them.
    ///
    /// Wanted by BiomeMapWriter as well as by place(). STATE §20 proposed
    /// extracting it to a geometry helper; that was a precondition for
    /// DELETING this unit, and §42 removed the deletion. Left here.
    static std::vector<std::vector<int>> distanceToStructure(const GisImport& g);

    /// Building, road, or a derived wall — anything trees must keep off.
    static bool isStructure(const GisImport& g, int x, int y);

    static bool tooClose(const std::vector<std::vector<bool>>& taken,
                         int x, int y, int w, int h);

private:
    TreeScatter() = delete;
};

} // namespace pzformat

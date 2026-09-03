// Port of TreePalette.java (Track F, port step 7). 149 lines Java.
//
// Trees as map data actually stores them.
//
// CONFIRMED against vanilla Muldraugh cell 35_35, which authors exactly four
// tree tiles: vegetation_trees_01_8 through _11. No species tiles appear in
// authored map data anywhere. The engine substitutes the actual pine or maple
// at runtime from the biome, and authoring species art directly produces
// canopies lying on the grass.
//
// ------------------------------------------------------------------------
// PORT NOTES
// ------------------------------------------------------------------------
//
//   1. ITERATION IS SORTED, AND THAT IS A DELIBERATE DIVERGENCE FROM THE
//      PRE-2026-09-02 JAVA. `TileIndex.byName` is a HashMap on the Java side
//      and a std::unordered_map here; the two orders have nothing to do with
//      each other, so raw iteration could never have matched. Worse, the order
//      is load-bearing: it fixes `bySize`'s list order, which decides which
//      tile TreeScatter puts on each of ~7,700 squares.
//
//      STATE §41 proved this hazard dormant because nothing consumed raw
//      byName order. The A2-gate resolving on 2026-09-02 (§42) made TreeScatter
//      live and this call site with it. The owner's decision (§43) was to sort
//      in BOTH trees rather than clone Java's HashMap bucket layout in C++:
//      reproducing it would buy byte-identity with an arbitrary ordering that
//      means nothing and that a JDK upgrade could permute again.
//
//      Java's TreePalette.pick now sorts too. The two trees agree because they
//      both sort, not because they both hash the same way.
//
//      Tile names are ASCII, so Java's String.compareTo (UTF-16 code units) and
//      std::string's operator< (unsigned byte compare) agree. THE ORACLE
//      ASSERTS THAT rather than assuming it — the synthetic header oracle
//      deliberately carries a tile name with bytes 80 FF, so non-ASCII names
//      are not hypothetical in this format.
//
//   2. `bySize` is a TreeMap<Integer, List<String>>: ordered by key, ascending.
//      Reproduced as std::map<int, std::vector<std::string>>, which has the
//      same ordering contract. NOT unordered_map. `toString` iterates it and
//      that text reaches the generator's stdout, which the step 7 oracle
//      compares.
//
//   3. `tilesNear` returns null in Java when nothing is found. Here it returns
//      a pointer, and nullptr reproduces that. The caller checks it, so the
//      distinction is observable.
//
//   4. STUMP is appended to `all` AFTER the sorted sheet tiles, not sorted in
//      with them. `all` feeds the cell header's tile name declaration, so the
//      position matters and sorting the whole thing at the end would diverge.
//
// Dependency-free: standard library only, per CHARTER §3.
#pragma once

#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include "tileindex.hpp"

namespace pzformat {

class TreePalette {
public:
    /// The sheet vanilla authors trees from.
    static constexpr const char* SHEET = "vegetation_trees_01";

    /// A felled tree. One tile, from WorldGen's stumps feature. Has a sprite.
    static constexpr const char* STUMP = "crafted_02_86";

    /// Every tile name chosen, so the cell header can declare them.
    /// Sorted sheet tiles first, then STUMP if present. See PORT NOTE 4.
    std::vector<std::string> all;

    bool hasStump = false;

    static TreePalette pick(const TileIndex& ti,
                            const std::unordered_set<std::string>& sprites);

    /// Tiles at a size class, falling back to the nearest available.
    /// nullptr when nothing is found at any distance. See PORT NOTE 3.
    const std::vector<std::string>* tilesNear(int size) const;

    bool usable() const { return !bySize_.empty(); }

    /// Reaches the generator's stdout, which the step 7 oracle compares.
    std::string toString() const;

    /// size class -> tile names. TreeMap in Java; see PORT NOTE 2.
    const std::map<int, std::vector<std::string>>& bySize() const { return bySize_; }

private:
    std::map<int, std::vector<std::string>> bySize_;
};

} // namespace pzformat

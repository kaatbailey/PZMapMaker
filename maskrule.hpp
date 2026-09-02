// Port of MaskRule.java (Track F, port step 5 — the palettes).
//
// Which mask tiles a square carries, given what lies on its four sides.
//
// Deliberately general. This is a pure function from a direction set to tile
// offsets within a block — it knows nothing about ground, GIS, or cells. Auto
// wall-joining (A3) is the same problem shape and should use this rather than
// grow a second copy; `floors_burnt_01` is a third consumer.
//
// THE RULE — STATE §26, measured on a contiguous 9x5 rectangle of Muldraugh
// 42_40 where 21 of 21 masks were explained, and confirmed at corpus scale in
// §27.
//
// A square carries masks drawn from a NEIGHBOUR's block, never its own. The
// mask names the direction the other material lies in. With S = the set of
// orthogonal directions holding that material:
//
//   |S| = 0              nothing
//   |S| = 1              one side tile
//   |S| = 2, adjacent    ONE CORNER TILE — not two side tiles
//   |S| = 2, opposite    two side tiles
//   |S| = 3              two corner tiles, sharing the middle direction
//   |S| = 4              four corner tiles
//
// Offsets from block base B:
//
//   B+1 N+W    B+2 E+S    B+3 S+W    B+4 E+N          corners
//   B+8 N   B+9 W   B+10 E   B+11 S                   sides, variant set 1
//   B+12 N  B+13 W  B+14 E   B+15 S                   sides, variant set 2
//
// VARIANT SETS ARE NOT UNIVERSAL. `blends_natural_01` has two and vanilla uses
// both on identical geometry, so pick at random. `blends_street_01` has ONE —
// code assuming the natural shape emits `blends_street_01_12`..`_15`, which are
// not road masks (§27).
//
// Grid convention: +x East, +y South, matching STATE §10.
//
// ---------------------------------------------------------------------------
// THE PORT'S LOAD-BEARING DECISION — iteration order of the direction set.
//
// The Java takes a `Set<Dir>` and every caller passes an `EnumSet`. EnumSet
// iterates in ORDINAL order, and that order reaches the output in two places:
//
//   masks() case 2 — `d[0].opposite() == d[1]` decides corner vs two sides,
//                    and `corner(d[0], d[1])` takes them positionally.
//   masks() case 3 — `others.get(0)` and `others.get(1)` fix WHICH corner is
//                    emitted first, and that ordering is the returned array.
//
// So this is the same class of question as `TileIndex.byName` (see the chunk
// prompt), and it resolves the same way: the Java order is deterministic, so
// reproduce it exactly. DirSet is therefore a 4-bit mask iterated 0..3 in
// declaration order N, W, E, S — NOT a std::set, which would sort by whatever
// comparator it is given, and NOT an unordered container.
//
// Declaration order is N, W, E, S. It is NOT compass order and it is NOT
// N, E, S, W. Dir::ord doubles as the ordinal and as the offset within a
// variant set, so reordering the enum silently changes emitted tile indices.
//
// Dependency-free: standard library only, per CHARTER §3.
#pragma once

#include "java_random.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pzformat {

class MaskRule {
public:
    /// Grid convention: +x East, +y South (STATE §10).
    /// N and W were transposed here once; the self-test asserts this table
    /// rather than trusting it.
    enum class Dir : int { N = 0, W = 1, E = 2, S = 3 };

    static constexpr std::array<Dir, 4> kDirs{Dir::N, Dir::W, Dir::E, Dir::S};

    /// Offset within a variant set: B+8+ord for set 1, B+12+ord for set 2.
    static constexpr int ord(Dir d) { return static_cast<int>(d); }
    static constexpr int dx(Dir d) {
        switch (d) {
            case Dir::N: return 0;
            case Dir::W: return -1;
            case Dir::E: return 1;
            case Dir::S: return 0;
        }
        return 0;
    }
    static constexpr int dy(Dir d) {
        switch (d) {
            case Dir::N: return -1;
            case Dir::W: return 0;
            case Dir::E: return 0;
            case Dir::S: return 1;
        }
        return 0;
    }
    static constexpr Dir opposite(Dir d) {
        switch (d) {
            case Dir::N: return Dir::S;
            case Dir::S: return Dir::N;
            case Dir::W: return Dir::E;
            case Dir::E: return Dir::W;
        }
        return d;
    }
    static constexpr const char* name(Dir d) {
        switch (d) {
            case Dir::N: return "N";
            case Dir::W: return "W";
            case Dir::E: return "E";
            case Dir::S: return "S";
        }
        return "?";
    }

    /// Java's EnumSet<Dir>, reduced to what this unit uses of it.
    /// Iteration is ordinal order, matching EnumSet — see the header comment.
    class DirSet {
    public:
        constexpr DirSet() = default;
        constexpr explicit DirSet(std::uint8_t bits) : bits_(bits) {}

        static constexpr DirSet of() { return DirSet{}; }
        static constexpr DirSet of(Dir a) {
            return DirSet{static_cast<std::uint8_t>(1u << ord(a))};
        }
        static constexpr DirSet of(Dir a, Dir b) {
            return DirSet{static_cast<std::uint8_t>((1u << ord(a)) | (1u << ord(b)))};
        }
        static constexpr DirSet of(Dir a, Dir b, Dir c) {
            return DirSet{static_cast<std::uint8_t>(
                (1u << ord(a)) | (1u << ord(b)) | (1u << ord(c)))};
        }
        static constexpr DirSet of(Dir a, Dir b, Dir c, Dir d) {
            return DirSet{static_cast<std::uint8_t>(
                (1u << ord(a)) | (1u << ord(b)) | (1u << ord(c)) | (1u << ord(d)))};
        }
        /// Raw bit pattern, for exhaustive enumeration of all 16 sets.
        static constexpr DirSet fromBits(std::uint8_t bits) { return DirSet{bits}; }

        constexpr bool contains(Dir d) const { return (bits_ >> ord(d)) & 1u; }
        constexpr bool empty() const { return bits_ == 0; }
        constexpr std::uint8_t bits() const { return bits_; }
        constexpr std::size_t size() const {
            std::size_t n = 0;
            for (std::uint8_t b = bits_; b != 0; b >>= 1) n += (b & 1u);
            return n;
        }
        /// Members in ordinal order — the EnumSet contract this port depends on.
        std::vector<Dir> toArray() const {
            std::vector<Dir> out;
            for (const Dir d : kDirs) {
                if (contains(d)) out.push_back(d);
            }
            return out;
        }

    private:
        std::uint8_t bits_ = 0;
    };

    /// Mask offsets for one neighbouring material.
    ///
    /// @param block        that material's block base
    /// @param dirs         directions in which that material lies
    /// @param variantSets  2 for blends_natural_01, 1 for blends_street_01
    /// @param rng          used only to pick between variant sets
    static std::vector<int> masks(int block, DirSet dirs, int variantSets, JavaRandom& rng);

    /// Corner tile covering two adjacent sides. Unordered.
    static int corner(int block, Dir a, Dir b);

    /// Side tile, choosing uniformly between variant sets where two exist.
    /// Draws from rng ONLY when variantSets > 1 — the draw count is
    /// branch-dependent and part of the contract.
    static int side(int block, Dir d, int variantSets, JavaRandom& rng);

private:
    MaskRule() = delete;
};

} // namespace pzformat

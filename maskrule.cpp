// maskrule.cpp — the rule itself. See maskrule.hpp for the measurement it
// encodes (STATE §26/§27) and for why DirSet iterates in ordinal order.

#include "maskrule.hpp"

#include <stdexcept>
#include <string>

namespace pzformat {

std::vector<int> MaskRule::masks(int block, DirSet dirs, int variantSets, JavaRandom& rng) {
    if (dirs.empty()) return {};
    const std::vector<Dir> d = dirs.toArray();

    switch (dirs.size()) {
        case 1:
            return {side(block, d[0], variantSets, rng)};

        case 2: {
            if (opposite(d[0]) == d[1]) {
                // Two side tiles, and TWO draws where variantSets > 1. Java
                // evaluates the array initialiser left to right, so d[0]'s
                // draw precedes d[1]'s. C++ does not guarantee argument
                // evaluation order, so these are sequenced explicitly rather
                // than left inside one braced list.
                const int a = side(block, d[0], variantSets, rng);
                const int b = side(block, d[1], variantSets, rng);
                return {a, b};
            }
            return {corner(block, d[0], d[1])};
        }

        case 3: {
            // The missing direction's OPPOSITE is the one adjacent to both
            // others, so it appears in both corner tiles.
            Dir missing = Dir::N;
            bool found = false;
            for (const Dir k : kDirs) {
                if (!dirs.contains(k)) { missing = k; found = true; }
            }
            // Java assigns `missing` in the same loop without breaking, so the
            // LAST non-member wins. With exactly three members there is only
            // one, but the loop shape is kept identical rather than optimised.
            if (!found) throw std::invalid_argument("impossible direction set");
            const Dir mid = opposite(missing);
            std::vector<Dir> others;
            for (const Dir k : kDirs) {
                if (dirs.contains(k) && k != mid) others.push_back(k);
            }
            return {corner(block, mid, others[0]), corner(block, mid, others[1])};
        }

        case 4:
            // NW, NE, SW, SE — the order vanilla writes them in at
            // 42_40 (112,200), an isolated square with all four sides
            // differing. Nothing suggests the order is load-bearing.
            return {block + 1, block + 4, block + 3, block + 2};

        default:
            throw std::invalid_argument("impossible direction set: "
                                        + std::to_string(dirs.bits()));
    }
}

int MaskRule::corner(int block, Dir a, Dir b) {
    const DirSet s = DirSet::of(a, b);
    if (s.bits() == DirSet::of(Dir::N, Dir::W).bits()) return block + 1;
    if (s.bits() == DirSet::of(Dir::E, Dir::S).bits()) return block + 2;
    if (s.bits() == DirSet::of(Dir::S, Dir::W).bits()) return block + 3;
    if (s.bits() == DirSet::of(Dir::E, Dir::N).bits()) return block + 4;
    throw std::invalid_argument(std::string("not an adjacent pair: ")
                                + name(a) + "," + name(b));
}

int MaskRule::side(int block, Dir d, int variantSets, JavaRandom& rng) {
    const int set = (variantSets > 1 && rng.nextBoolean()) ? 12 : 8;
    return block + set + ord(d);
}

} // namespace pzformat

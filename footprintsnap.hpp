// footprintsnap.hpp — port of pzformat/FootprintSnap.java
//
// Turns a GIS footprint into an axis-aligned rectangle. Pure geometry, no RNG,
// no dependencies.
//
// WHY IT IS NOT A ROTATION (Java doc, kept because it is the justification for
// the whole approach): a room is a union of int32 x/y/w/h rectangles — no
// rotation field, no polygon. Every wall runs due N-S or E-W and the target
// orientation is 0°, fixed by the format. Over 90,827 Muldraugh rooms, 64.5%
// are exactly one rectangle and the median fill ratio is 1.000, so tracing a
// real GIS polygon would produce buildings MORE complex than vanilla's. The
// footprint is a position and an area, not a shape.
//
// THREE PORTING HAZARDS, all of which would pass a casual eyeball:
//
//  1. java.lang.Math.round(x) is floor(x + 0.5). std::round is
//     round-half-away-from-zero. They disagree on EVERY negative half-integer:
//     Java gives -2 for -2.5, std::round gives -3. Footprint centroids go
//     negative near a cell origin, so this is reachable, not theoretical.
//  2. Arrays.sort on an object array is a STABLE mergesort; std::sort is not.
//     The hull comparator ties on exact duplicate vertices, which dedupeExact
//     does not remove (it drops only the closing vertex). std::stable_sort.
//  3. Java's Math.atan2/cos/sin permit 2 ulp of error and need not match libm.
//     Nothing can be done about that here; it is why the oracle compares
//     rounded integer output over a large corpus rather than intermediate
//     doubles.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace pzformat {

/// java.lang.Math.round(double) -> long. NOT std::round.
inline std::int64_t javaRound(double d) {
    return static_cast<std::int64_t>(std::floor(d + 0.5));
}

class FootprintSnap {
public:
    struct Point {
        double x = 0.0;
        double y = 0.0;
    };

    /// The axis-aligned rectangle standing in for a footprint.
    struct Rect {
        int x = 0, y = 0, w = 0, h = 0;

        int area() const { return w * h; }
        // Java: x + w / 2 — INTEGER division, truncating toward zero.
        int cx() const { return x + w / 2; }
        int cy() const { return y + h / 2; }

        std::string toString() const {
            return "[" + std::to_string(x) + "," + std::to_string(y) + " " + std::to_string(w) +
                   "x" + std::to_string(h) + "]";
        }
    };

    /// Snap from EXACT projected coordinates. Prefer this: integer input has
    /// already lost 2-7% of the footprint's area to vertex quantisation, and
    /// the loss is always downward.
    static std::optional<Rect> snap(const std::vector<Point>& pts);

    /// Snap a projected integer ring (GisImport.project output).
    static std::optional<Rect> snapRing(const std::vector<std::pair<int, int>>& ring);

    /// Polygon area in square tiles.
    static double area(const std::vector<Point>& pts);

    /// Is every edge of this ring axis-parallel? Vanilla contains no
    /// counterexample, so a caller may refuse on false rather than warn.
    static bool isAxisAligned(const std::vector<std::pair<int, int>>& ring);

    // Internals, exposed for the oracle.
    static std::vector<Point> dedupeExact(const std::vector<Point>& p);
    static std::vector<Point> dedupe(const std::vector<std::pair<int, int>>& ring);
    static double shoelace(const std::vector<Point>& p);
    static Point centroid(const std::vector<Point>& p, double area);
    static std::vector<Point> hull(const std::vector<Point>& p);
    static double cross(const Point& o, const Point& a, const Point& b);
    /// {angle degrees, area, width, height}
    static std::array<double, 4> minAreaRect(const std::vector<Point>& h);
};

}  // namespace pzformat

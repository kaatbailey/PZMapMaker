#include "footprintsnap.hpp"

#include <array>

namespace pzformat {

std::vector<FootprintSnap::Point> FootprintSnap::dedupeExact(const std::vector<Point>& p) {
    std::size_t n = p.size();
    if (n > 1 && p[0].x == p[n - 1].x && p[0].y == p[n - 1].y) n--;
    return std::vector<Point>(p.begin(), p.begin() + static_cast<std::ptrdiff_t>(n));
}

std::vector<FootprintSnap::Point> FootprintSnap::dedupe(
    const std::vector<std::pair<int, int>>& ring) {
    std::size_t n = ring.size();
    if (n > 1 && ring[0].first == ring[n - 1].first && ring[0].second == ring[n - 1].second) n--;
    std::vector<Point> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; i++)
        out.push_back({static_cast<double>(ring[i].first), static_cast<double>(ring[i].second)});
    return out;
}

double FootprintSnap::shoelace(const std::vector<Point>& p) {
    double s = 0;
    const std::size_t n = p.size();
    for (std::size_t i = 0; i < n; i++) {
        const Point& u = p[i];
        const Point& v = p[(i + 1) % n];
        s += u.x * v.y - v.x * u.y;
    }
    return s / 2;
}

FootprintSnap::Point FootprintSnap::centroid(const std::vector<Point>& p, double /*area*/) {
    // Java takes `area` but does not use it — the denominator is recomputed
    // from shoelace(p), which is SIGNED where the caller's `area` is absolute.
    // Kept in the signature so the two implementations stay line-comparable.
    double cx = 0, cy = 0;
    const std::size_t n = p.size();
    for (std::size_t i = 0; i < n; i++) {
        const Point& u = p[i];
        const Point& v = p[(i + 1) % n];
        const double cr = u.x * v.y - v.x * u.y;
        cx += (u.x + v.x) * cr;
        cy += (u.y + v.y) * cr;
    }
    const double denom = 6 * shoelace(p);
    if (std::fabs(denom) < 1e-9) {
        // Degenerate: fall back to the vertex mean. NOTE Java accumulates onto
        // the ALREADY-NONZERO cx/cy rather than resetting them; reproduced
        // deliberately, because resetting would be a behaviour change the
        // oracle would catch as a difference.
        for (const Point& q : p) {
            cx += q.x;
            cy += q.y;
        }
        return {cx / static_cast<double>(n), cy / static_cast<double>(n)};
    }
    return {cx / denom, cy / denom};
}

double FootprintSnap::cross(const Point& o, const Point& a, const Point& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

std::vector<FootprintSnap::Point> FootprintSnap::hull(const std::vector<Point>& p) {
    if (p.size() < 3) return p;
    std::vector<Point> s = p;
    // STABLE sort: Java's Arrays.sort on an object array is a stable mergesort.
    // Duplicate vertices survive dedupeExact, so tie order is observable.
    std::stable_sort(s.begin(), s.end(), [](const Point& u, const Point& v) {
        if (u.x != v.x) return u.x < v.x;
        return u.y < v.y;
    });

    std::vector<Point> h(2 * s.size());
    std::size_t k = 0;
    for (const Point& q : s) {
        while (k >= 2 && cross(h[k - 2], h[k - 1], q) <= 0) k--;
        h[k++] = q;
    }
    const std::size_t lower = k + 1;
    for (std::size_t i = s.size() - 1; i-- > 0;) {
        const Point& q = s[i];
        while (k >= lower && cross(h[k - 2], h[k - 1], q) <= 0) k--;
        h[k++] = q;
    }
    // Java: Arrays.copyOf(h, Math.max(k - 1, 1))
    const std::size_t len = (k >= 2) ? (k - 1) : 1;
    return std::vector<Point>(h.begin(), h.begin() + static_cast<std::ptrdiff_t>(len));
}

std::array<double, 4> FootprintSnap::minAreaRect(const std::vector<Point>& h) {
    double bestArea = std::numeric_limits<double>::max();
    std::array<double, 4> best{0, 0, 0, 0};
    const std::size_t n = h.size();
    for (std::size_t i = 0; i < n; i++) {
        const Point& a = h[i];
        const Point& b = h[(i + 1) % n];
        const double ang = std::atan2(b.y - a.y, b.x - a.x);
        const double c = std::cos(-ang), s = std::sin(-ang);
        double minX = std::numeric_limits<double>::max();
        double maxX = -std::numeric_limits<double>::max();
        double minY = std::numeric_limits<double>::max();
        double maxY = -std::numeric_limits<double>::max();
        for (const Point& q : h) {
            const double x = q.x * c - q.y * s;
            const double y = q.x * s + q.y * c;
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }
        const double w = maxX - minX, ht = maxY - minY, ar = w * ht;
        if (ar < bestArea) {
            bestArea = ar;
            // Java: Math.toDegrees(ang) == ang * 180 / PI
            best = {ang * 180.0 / M_PI, ar, w, ht};
        }
    }
    return best;
}

double FootprintSnap::area(const std::vector<Point>& pts) {
    const std::vector<Point> p = dedupeExact(pts);
    return p.size() < 3 ? 0.0 : std::fabs(shoelace(p));
}

std::optional<FootprintSnap::Rect> FootprintSnap::snap(const std::vector<Point>& pts) {
    const std::vector<Point> p = dedupeExact(pts);
    if (p.size() < 3) return std::nullopt;

    const double a = std::fabs(shoelace(p));
    if (a < 1) return std::nullopt;

    const Point c = centroid(p, a);

    const std::vector<Point> h = hull(p);
    const std::array<double, 4> mar = minAreaRect(h);
    const double w = mar[2], ht = mar[3];
    if (w <= 0 || ht <= 0) return std::nullopt;

    // Match the polygon's area, not the enclosing rectangle's.
    const double scale = std::sqrt(a / (w * ht));
    const double ew = w * scale, eh = ht * scale;

    // Round the LONGER side, then derive the shorter from the area.
    int rw, rh;
    if (ew >= eh) {
        rw = std::max(static_cast<std::int64_t>(1), javaRound(ew)) > 0
                 ? static_cast<int>(std::max(static_cast<std::int64_t>(1), javaRound(ew)))
                 : 1;
        rh = static_cast<int>(std::max(static_cast<std::int64_t>(1), javaRound(a / rw)));
    } else {
        rh = static_cast<int>(std::max(static_cast<std::int64_t>(1), javaRound(eh)));
        rw = static_cast<int>(std::max(static_cast<std::int64_t>(1), javaRound(a / rh)));
    }

    return Rect{static_cast<int>(javaRound(c.x - rw / 2.0)),
                static_cast<int>(javaRound(c.y - rh / 2.0)), rw, rh};
}

std::optional<FootprintSnap::Rect> FootprintSnap::snapRing(
    const std::vector<std::pair<int, int>>& ring) {
    return snap(dedupe(ring));
}

bool FootprintSnap::isAxisAligned(const std::vector<std::pair<int, int>>& ring) {
    const std::vector<Point> p = dedupe(ring);
    if (p.size() < 3) return false;
    const std::size_t n = p.size();
    for (std::size_t i = 0; i < n; i++) {
        const Point& a = p[i];
        const Point& b = p[(i + 1) % n];
        if (a.x != b.x && a.y != b.y) return false;
    }
    return true;
}

}  // namespace pzformat

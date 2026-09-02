// gisimport.cpp — the raster core. See gisimport.hpp for the transcendental
// analysis and the three port notes.

#include "gisimport.hpp"

#include <algorithm>
#include <cmath>

namespace pzformat {

namespace {

/// Java's int-typed Math.min/max over the loop bounds, kept explicit so the
/// clamping order matches the source rather than being folded by hand.
int imin(int a, int b) { return a < b ? a : b; }
int imax(int a, int b) { return a > b ? a : b; }

std::size_t uz(int v) { return static_cast<std::size_t>(v); }

} // namespace

double GisImport::metresPerLon(double midLat) {
    return 111320.0 * std::cos(toRadians(midLat));
}

void GisImport::extentTiles(double minLon, double minLat, double maxLon, double maxLat,
                            int& outWidth, int& outHeight) {
    const double midLat = (minLat + maxLat) / 2;
    const double mPerLon = metresPerLon(midLat);
    // Java: (int) Math.ceil(...). The double->int narrowing is a C-style
    // truncation toward zero in Java, and ceil has already removed any
    // fraction, so a plain cast reproduces it for in-range values.
    outWidth = static_cast<int>(std::ceil((maxLon - minLon) * mPerLon));
    outHeight = static_cast<int>(std::ceil((maxLat - minLat) * METRES_PER_LAT));
}

void GisImport::allocate(int w, int h) {
    width = w;
    height = h;
    const std::size_t uw = uz(imax(w, 0)), uh = uz(imax(h, 0));
    cover.assign(uw, std::vector<Cover>(uh, Cover::None));
    northWall.assign(uw, std::vector<bool>(uh, false));
    westWall.assign(uw, std::vector<bool>(uh, false));
    occupancy.assign(uw, std::vector<std::optional<std::string>>(uh, std::nullopt));
}

std::vector<GisImport::Point> GisImport::projectExact(
        const std::vector<Point>& ring, double minLon, double maxLat,
        double mPerLon, double mPerLat) {
    std::vector<Point> out(ring.size());
    for (std::size_t i = 0; i < ring.size(); i++) {
        out[i].x = (ring[i].x - minLon) * mPerLon;
        out[i].y = (maxLat - ring[i].y) * mPerLat;   // north is -y
    }
    return out;
}

std::vector<GisImport::IPoint> GisImport::project(
        const std::vector<Point>& ring, double minLon, double maxLat,
        double mPerLon, double mPerLat) {
    std::vector<IPoint> out;
    out.reserve(ring.size());
    for (const Point& p : ring) {
        // javaRound is floor(d + 0.5), NOT std::round. The two differ for
        // negative arguments, which occur here whenever a vertex lies west of
        // minLon — see PORT NOTE 2. Java then narrows long -> int.
        const int x = static_cast<int>(javaRound((p.x - minLon) * mPerLon));
        const int y = static_cast<int>(javaRound((maxLat - p.y) * mPerLat));
        out.push_back(IPoint{x, y});
    }
    return out;
}

bool GisImport::fillRect(const FootprintSnap::Rect& r, const std::optional<std::string>& occ) {
    bool any = false;
    for (int x = imax(r.x, 0); x < imin(r.x + r.w, width); x++)
        for (int y = imax(r.y, 0); y < imin(r.y + r.h, height); y++) {
            if (cover[uz(x)][uz(y)] != Cover::Building) buildingTiles++;
            cover[uz(x)][uz(y)] = Cover::Building;
            occupancy[uz(x)][uz(y)] = occ;
            any = true;
        }
    return any;
}

bool GisImport::fillPolygon(const std::vector<IPoint>& ring,
                            const std::optional<std::string>& occ) {
    if (ring.size() < 3) return false;
    // Java seeds with Integer.MAX_VALUE / MIN_VALUE.
    int minY = 2147483647, maxY = -2147483647 - 1;
    for (const IPoint& p : ring) { minY = imin(minY, p.y); maxY = imax(maxY, p.y); }
    minY = imax(minY, 0);
    maxY = imin(maxY, height - 1);
    bool any = false;

    for (int y = minY; y <= maxY; y++) {
        std::vector<int> xs;
        for (std::size_t i = 0; i < ring.size(); i++) {
            const IPoint& a = ring[i];
            const IPoint& b = ring[(i + 1) % ring.size()];
            if (a.y == b.y) continue;
            const int y0 = imin(a.y, b.y), y1 = imax(a.y, b.y);
            if (y < y0 || y >= y1) continue;
            const double t = (y - a.y) / static_cast<double>(b.y - a.y);
            xs.push_back(static_cast<int>(javaRound(a.x + t * (b.x - a.x))));
        }
        // Java: Collections.sort on List<Integer> — natural int order.
        std::sort(xs.begin(), xs.end());
        for (std::size_t i = 0; i + 1 < xs.size(); i += 2)
            for (int x = imax(xs[i], 0); x <= imin(xs[i + 1], width - 1); x++) {
                if (cover[uz(x)][uz(y)] != Cover::Building) buildingTiles++;
                cover[uz(x)][uz(y)] = Cover::Building;
                occupancy[uz(x)][uz(y)] = occ;
                any = true;
            }
    }
    return any;
}

void GisImport::thickLine(IPoint a, IPoint b, int halfWidth) {
    int steps = imax(std::abs(b.x - a.x), std::abs(b.y - a.y));
    if (steps == 0) steps = 1;
    for (int i = 0; i <= steps; i++) {
        // int64 intermediate: Java wraps on int overflow, C++ signed overflow
        // is UB. See PORT NOTE 3.
        const int cx = a.x + static_cast<int>(
            static_cast<std::int64_t>(b.x - a.x) * i / steps);
        const int cy = a.y + static_cast<int>(
            static_cast<std::int64_t>(b.y - a.y) * i / steps);
        for (int dx = -halfWidth; dx <= halfWidth; dx++)
            for (int dy = -halfWidth; dy <= halfWidth; dy++) {
                if (dx * dx + dy * dy > halfWidth * halfWidth) continue;
                const int x = cx + dx, y = cy + dy;
                if (!inBounds(x, y) || cover[uz(x)][uz(y)] == Cover::Building) continue;
                if (cover[uz(x)][uz(y)] != Cover::Road) roadTiles++;
                cover[uz(x)][uz(y)] = Cover::Road;
            }
    }
}

int GisImport::waterWidth(const std::optional<std::string>& fcode) {
    if (!fcode.has_value()) return 2;               // Java: fcode == null
    const std::string& f = *fcode;
    if (f == "46006" || f == "46003") return 2;     // stream/river, perennial or intermittent
    if (f == "46000") return 3;                     // stream/river, unspecified
    if (f == "55800") return 4;                     // artificial path (large channel)
    if (f == "33600" || f == "33601" || f == "33603") return 2;  // canal/ditch
    return 2;
}

void GisImport::waterLine(IPoint a, IPoint b, int halfWidth) {
    int steps = imax(std::abs(b.x - a.x), std::abs(b.y - a.y));
    if (steps == 0) steps = 1;
    for (int i = 0; i <= steps; i++) {
        const int cx = a.x + static_cast<int>(
            static_cast<std::int64_t>(b.x - a.x) * i / steps);
        const int cy = a.y + static_cast<int>(
            static_cast<std::int64_t>(b.y - a.y) * i / steps);
        for (int dx = -halfWidth; dx <= halfWidth; dx++)
            for (int dy = -halfWidth; dy <= halfWidth; dy++) {
                if (dx * dx + dy * dy > halfWidth * halfWidth) continue;
                const int x = cx + dx, y = cy + dy;
                if (!inBounds(x, y)) continue;
                // Water loses to roads and buildings.
                if (cover[uz(x)][uz(y)] == Cover::Road
                    || cover[uz(x)][uz(y)] == Cover::Building) continue;
                if (cover[uz(x)][uz(y)] != Cover::Water) waterTiles++;
                cover[uz(x)][uz(y)] = Cover::Water;
            }
    }
}

void GisImport::deriveWalls() {
    for (int x = 0; x < width; x++)
        for (int y = 0; y < height; y++) {
            if (cover[uz(x)][uz(y)] != Cover::Building) continue;
            if (!isBuilding(x, y - 1)) northWall[uz(x)][uz(y)] = true;   // north face
            if (!isBuilding(x - 1, y)) westWall[uz(x)][uz(y)] = true;    // west face
            if (!isBuilding(x, y + 1) && inBounds(x, y + 1))
                northWall[uz(x)][uz(y + 1)] = true;                      // south face
            if (!isBuilding(x + 1, y) && inBounds(x + 1, y))
                westWall[uz(x + 1)][uz(y)] = true;                       // east face
        }
}

} // namespace pzformat

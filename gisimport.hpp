// Port of GisImport.java's RASTER CORE (Track F, port step 6 / CHUNKS F5).
//
// Turns projected GIS geometry into a Cover grid, walls and occupancy.
//
// SCOPE. This is the raster core only, per §37's step-6 definition
// ("GisImport (rasterise)"). Deliberately NOT ported here:
//   - file loading and the GeoJSON plumbing (step 1, already done)
//   - writeSchematic — it is java.awt/ImageIO, an application-layer concern,
//     and PNG byte-identity is §37's second OPEN and belongs to step 7
//   - the console progress output
// What IS here is everything that decides a tile's contents, which is what the
// oracle compares cell by cell.
//
// Buildings are rasterised as SNAPPED RECTANGLES via FootprintSnap, not as
// polygons. Polygon fill produced buildings whose room rect was a bounding box
// while their walls traced the real outline at 37-80 degrees; a room is
// x,y,w,h with no rotation field (STATE §10), so an off-axis wall is
// unrepresentable rather than merely ugly. STATE §30.
//
// Scale: one PZ tile is one metre.
//
// ---------------------------------------------------------------------------
// PORT NOTE 1 — THE ONE TRANSCENDENTAL, AND WHY IT IS NOT std::cos's FAULT.
//
// GisImport.java:124 is the only transcendental in this step:
//
//     double mPerLon = 111_320.0 * Math.cos(Math.toRadians(midLat));
//
// It is evaluated ONCE per import, but its result reaches Math.ceil for the
// grid width — so a one-ulp difference does not nudge a coordinate, it can
// resize the entire grid and diverge every cell at once.
//
// MEASURED over a 257,143-sample sweep of -90..+90 (see FINDINGS):
//
//   naive transcription  40,898 divergent (15.9%)
//   corrected            531 divergent (0.21%)
//
// The 15.9% was a TRANSCRIPTION BUG, not a cos disagreement:
// **Math.toRadians(a) is NOT a / 180.0 * PI.** Modern OpenJDK is a single
// multiply by DEGREES_TO_RADIANS. The two differ by one ulp on some inputs,
// and near +-90 degrees cos is near zero so that error is amplified ~32768x —
// exactly the bit-delta observed. Use the constant, one multiply, below.
//
// The residual 531 ARE genuine Math.cos vs std::cos disagreements, one ulp
// each, at ordinary latitudes (258 within |lat| <= 60). Do not try to fix them
// by porting fdlibm: at 41.5 degrees Math.cos and StrictMath.cos disagree while
// std::cos matches Math.cos, so chasing StrictMath makes agreement WORSE.
//
// The divergence is characterised and accepted, not eliminated. The oracle
// feeds latitudes that include known-divergent ones so we MEASURE whether one
// ulp of mPerLon reaches the Cover grid rather than assuming it cannot.
//
// PORT NOTE 2 — javaRound IS REACHABLE HERE, unlike in step 4.
//
// STATE §40 recorded std::round-for-javaRound as an unreachable mutation:
// all six step-4 sites took non-negative arguments, 116,502 calls, 0 negative.
// project() computes (p[0] - minLon) * mPerLon, which is NEGATIVE for any
// vertex west of minLon — and GisImport warns that road segments routinely run
// outside the requested area. floor(d + 0.5) and std::round differ exactly
// there. A mutation proven dead in one step is live in the next.
//
// PORT NOTE 3 — integer overflow where Java merely wraps.
//
// thickLine/waterLine compute (b[0] - a[0]) * i / steps in int. Java wraps on
// overflow; C++ signed overflow is UNDEFINED. At realistic extents the product
// stays well inside int32, but the oracle drives coordinates far outside the
// grid on purpose, so the arithmetic is done in int64 and narrowed, which
// reproduces Java's value for every non-overflowing case and does not invoke UB
// for the rest.
//
// Dependency-free: standard library only, per CHARTER §3.
#pragma once

#include "footprintsnap.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pzformat {

class GisImport {
public:
    /// What occupies a tile. Coarse on purpose.
    enum class Cover { None = 0, Water = 1, Road = 2, Building = 3 };

    /// Java's Math.toRadians — ONE multiply by DEGREES_TO_RADIANS.
    /// NOT a / 180.0 * PI; see PORT NOTE 1.
    static constexpr double DEGREES_TO_RADIANS = 0.017453292519943295;
    static double toRadians(double angdeg) { return angdeg * DEGREES_TO_RADIANS; }

    /// Metres per degree of longitude at midLat. The only transcendental.
    static double metresPerLon(double midLat);
    static constexpr double METRES_PER_LAT = 110540.0;

    int width = 0, height = 0;
    std::vector<std::vector<Cover>> cover;
    std::vector<std::vector<bool>> northWall, westWall;
    /// OCC_CLS per building tile. nullopt reproduces Java's null.
    std::vector<std::vector<std::optional<std::string>>> occupancy;

    int buildingsPlaced = 0, roadsPlaced = 0, buildingTiles = 0;
    int roadTiles = 0, waterPlaced = 0, waterTiles = 0;
    /// std::map, matching Java's TreeMap — sorted, so iteration is safe.
    std::map<std::string, int> byOccupancy;

    /// Allocate the grid and fill it with Cover::None, as the Java does.
    void allocate(int w, int h);

    /// Grid extent from a bounding box, reproducing GisImport.java:128-129
    /// including the Math.ceil that the cos result feeds.
    static void extentTiles(double minLon, double minLat, double maxLon, double maxLat,
                            int& outWidth, int& outHeight);

    struct Point { double x = 0.0, y = 0.0; };
    struct IPoint { int x = 0, y = 0; };

    /// Project without rounding. Buildings use this: quantising vertices before
    /// FootprintSnap measures them costs 2-7% of every footprint's area, always
    /// downward, and the loss cannot be recovered afterwards.
    static std::vector<Point> projectExact(const std::vector<Point>& ring,
                                           double minLon, double maxLat,
                                           double mPerLon, double mPerLat);

    /// Integer projection. Roads use this — thickLine walks tile centres.
    /// Uses javaRound, which is REACHABLE with negative arguments here.
    static std::vector<IPoint> project(const std::vector<Point>& ring,
                                       double minLon, double maxLat,
                                       double mPerLon, double mPerLat);

    bool inBounds(int x, int y) const {
        return x >= 0 && y >= 0 && x < width && y < height;
    }
    bool isBuilding(int x, int y) const {
        return inBounds(x, y) && cover[static_cast<std::size_t>(x)][static_cast<std::size_t>(y)]
               == Cover::Building;
    }

    bool fillRect(const FootprintSnap::Rect& r, const std::optional<std::string>& occ);

    /// Scanline fill, even-odd containment.
    /// RETAINED, UNUSED — buildings went through this until 2026-08-14 and now
    /// use fillRect. Kept because it is correct and a future caller that wants
    /// a real polygon (a lake, a field boundary) will want it. Ported for the
    /// same reason, and because an unused unit is exactly where a divergence
    /// hides until the day someone calls it.
    bool fillPolygon(const std::vector<IPoint>& ring, const std::optional<std::string>& occ);

    void thickLine(IPoint a, IPoint b, int halfWidth);

    /// Water half-width in tiles by NHD FCode.
    static int waterWidth(const std::optional<std::string>& fcode);

    /// Like thickLine, but water loses to roads and buildings.
    void waterLine(IPoint a, IPoint b, int halfWidth);

    /// Walls from the rasterised boundary, using the edge convention confirmed
    /// against vanilla rooms: a wall lives on the north or west edge of a
    /// square, so a building's south face sits on the square BELOW it.
    void deriveWalls();
};

} // namespace pzformat

// gisraster_oracle.cpp — C++ side of the step-6 raster oracle.
// Must emit a digest byte-identical to GisRasterOracle.java's.
//
// See GisRasterOracle.java for the corpus design. The two points worth
// repeating here:
//
//   1. The COS and EXT sections are emitted FIRST and read first. They isolate
//      the projection arithmetic from the raster logic, so a divergence there
//      is arithmetic rather than a bug in the fills — the same discipline as
//      the palettes oracle's VIN lines.
//
//   2. LAT_PROBES deliberately contains latitudes where Math.cos and std::cos
//      are KNOWN to differ by one ulp. The question is not whether they agree
//      (they do not) but whether one ulp of mPerLon reaches the Cover grid.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "footprintsnap.hpp"
#include "gisimport.hpp"
#include "java_random.hpp"

using pzformat::GisImport;
using pzformat::JavaRandom;
using pzformat::FootprintSnap;

namespace {

std::string esc(const std::string& s) {
    std::string b;
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == '\\') b += "\\\\";
        else if (c == '\t') b += "\\t";
        else if (c == '\n') b += "\\n";
        else if (c < 0x20) { char t[8]; std::snprintf(t, sizeof(t), "\\x%02x", c); b += t; }
        else b += ch;
    }
    return b;
}

std::string bits(double d) {
    std::uint64_t u = 0;
    std::memcpy(&u, &d, sizeof(u));
    char t[32];
    std::snprintf(t, sizeof(t), "%016llx", static_cast<unsigned long long>(u));
    return t;
}

std::string hex64(std::uint64_t h) {
    char t[32];
    std::snprintf(t, sizeof(t), "%016llx", static_cast<unsigned long long>(h));
    return t;
}

/// Same probe list as the Java, same order. Divergent latitudes first.
const double kLatProbes[] = {
    -59.6886, -59.2441, -58.7359, -58.5833, -58.5301, -58.3103,
    -58.0219, -57.9904, -57.9729, -56.1942, -56.0374, -55.8855,
    -55.7070, -54.8747, -54.8180, -54.6990, -54.5968, -54.4330,
    37.7, 38.0, 39.9612, 40.0, 41.5, 35.6895, 0.0, 25.0, 50.0, 60.0,
    -89.9972, 89.9972, 89.9999, -0.0001,
};
constexpr std::size_t kLatCount = sizeof(kLatProbes) / sizeof(kLatProbes[0]);

std::string gridHash(const GisImport& g) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (int y = 0; y < g.height; y++)
        for (int x = 0; x < g.width; x++) {
            const auto ux = static_cast<std::size_t>(x), uy = static_cast<std::size_t>(y);
            h ^= static_cast<std::uint64_t>(g.cover[ux][uy]);      h *= 0x100000001b3ULL;
            h ^= (g.northWall[ux][uy] ? 1u : 0u);                  h *= 0x100000001b3ULL;
            h ^= (g.westWall[ux][uy] ? 1u : 0u);                   h *= 0x100000001b3ULL;
            const auto& o = g.occupancy[ux][uy];
            if (!o.has_value()) { h ^= 0xff; h *= 0x100000001b3ULL; }
            else for (const char c : *o) {
                h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
                h *= 0x100000001b3ULL;
            }
        }
    return hex64(h);
}

void projection(std::vector<std::string>& out) {
    for (const double lat : kLatProbes) {
        const double rad = GisImport::toRadians(lat);
        const double mPerLon = 111320.0 * std::cos(rad);
        out.push_back("COS\t" + bits(lat) + "\t" + bits(rad) + "\t" + bits(mPerLon));
    }
    for (const double lat : kLatProbes) {
        for (int k = 1; k <= 40; k++) {
            const double span = k * 0.0037;
            const double mPerLon = 111320.0 * std::cos(GisImport::toRadians(lat));
            const int w = static_cast<int>(std::ceil(span * mPerLon));
            const int h = static_cast<int>(std::ceil(span * 110540.0));
            out.push_back("EXT\t" + bits(lat) + "\t" + std::to_string(k)
                          + "\t" + std::to_string(w) + "\t" + std::to_string(h));
        }
    }
}

void waterCodes(std::vector<std::string>& out) {
    const std::optional<std::string> codes[] = {
        std::nullopt, std::string("46006"), std::string("46003"), std::string("46000"),
        std::string("55800"), std::string("33600"), std::string("33601"),
        std::string("33603"), std::string(""), std::string("46007"),
        std::string("99999"), std::string("4600")};
    for (const auto& c : codes) {
        out.push_back("WW\t" + (c.has_value() ? esc(*c) : std::string("NULL"))
                      + "\t" + std::to_string(GisImport::waterWidth(c)));
    }
}

void projectionPoints(std::vector<std::string>& out, int cases) {
    for (int c = 0; c < cases; c++) {
        JavaRandom rng(static_cast<std::int64_t>(c) * 31LL + 7);
        const double lat = kLatProbes[static_cast<std::size_t>(c) % kLatCount];
        const double mPerLon = 111320.0 * std::cos(GisImport::toRadians(lat));
        const double minLon = (rng.nextInt(720000) - 360000) / 2000.0;
        const double maxLat = (rng.nextInt(340000) - 170000) / 2000.0;

        std::vector<GisImport::Point> ring;
        for (int i = 0; i < 6; i++) {
            const double px = minLon + (rng.nextInt(200000) - 100000) / 1000000.0;
            const double py = maxLat - (rng.nextInt(200000) - 100000) / 1000000.0;
            ring.push_back(GisImport::Point{px, py});
        }
        const auto ip = GisImport::project(ring, minLon, maxLat, mPerLon, 110540.0);
        const auto ep = GisImport::projectExact(ring, minLon, maxLat, mPerLon, 110540.0);
        for (std::size_t i = 0; i < ip.size(); i++) {
            out.push_back("PROJ\t" + std::to_string(c) + "\t" + std::to_string(i)
                          + "\t" + std::to_string(ip[i].x) + "," + std::to_string(ip[i].y)
                          + "\t" + bits(ep[i].x) + "\t" + bits(ep[i].y));
        }
    }
}

const char* boolStr(bool b) { return b ? "true" : "false"; }

void raster(std::vector<std::string>& out, int cases) {
    for (int c = 0; c < cases; c++) {
        JavaRandom rng(c);
        const int mode = c % 6;

        const int w = 1 + rng.nextInt(60);
        const int h = 1 + rng.nextInt(60);
        GisImport g;
        g.allocate(w, h);

        const int ops = 1 + rng.nextInt(8);
        for (int op = 0; op < ops; op++) {
            const int ax = rng.nextInt(w + 40) - 20, ay = rng.nextInt(h + 40) - 20;
            const int bx = rng.nextInt(w + 40) - 20, by = rng.nextInt(h + 40) - 20;
            const int hw = rng.nextInt(5);

            switch (mode) {
                case 0: {
                    const FootprintSnap::Rect r{ax, ay, rng.nextInt(12), rng.nextInt(12)};
                    out.push_back("FRECT\t" + std::to_string(c) + "\t" + std::to_string(op)
                                  + "\t" + std::to_string(r.x) + "," + std::to_string(r.y)
                                  + "," + std::to_string(r.w) + "," + std::to_string(r.h)
                                  + "\t" + boolStr(g.fillRect(r, "occ" + std::to_string(op % 3))));
                    break;
                }
                case 1: {
                    std::vector<GisImport::IPoint> ring{
                        {ax, ay}, {bx, ay}, {bx, by}, {bx, by}, {ax, by}};
                    out.push_back("FPOLY\t" + std::to_string(c) + "\t" + std::to_string(op)
                                  + "\t" + boolStr(g.fillPolygon(
                                        ring, "poly" + std::to_string(op % 3))));
                    break;
                }
                case 2:
                    g.thickLine({ax, ay}, {bx, by}, hw);
                    out.push_back("TLINE\t" + std::to_string(c) + "\t" + std::to_string(op)
                                  + "\t" + std::to_string(g.roadTiles));
                    break;
                case 3:
                    g.waterLine({ax, ay}, {bx, by}, hw);
                    out.push_back("WLINE\t" + std::to_string(c) + "\t" + std::to_string(op)
                                  + "\t" + std::to_string(g.waterTiles));
                    break;
                case 4: {
                    g.waterLine({ax, ay}, {bx, by}, hw);
                    g.thickLine({ay, ax}, {by, bx}, hw);
                    const FootprintSnap::Rect r{ax, ay, 4, 4};
                    g.fillRect(r, std::nullopt);
                    out.push_back("MIX\t" + std::to_string(c) + "\t" + std::to_string(op)
                                  + "\t" + std::to_string(g.waterTiles)
                                  + "\t" + std::to_string(g.roadTiles)
                                  + "\t" + std::to_string(g.buildingTiles));
                    break;
                }
                default: {
                    g.thickLine({ax, ay}, {ax, ay}, hw);
                    const std::vector<GisImport::IPoint> two{{ax, ay}, {bx, by}};
                    const bool p = g.fillPolygon(two, std::string("x"));
                    const bool f = g.fillRect(FootprintSnap::Rect{ax, ay, 0, 0}, std::string("z"));
                    out.push_back("DEGEN\t" + std::to_string(c) + "\t" + std::to_string(op)
                                  + "\t" + boolStr(p) + "\t" + boolStr(f)
                                  + "\t" + std::to_string(g.roadTiles));
                }
            }
        }

        g.deriveWalls();
        out.push_back("GRID\t" + std::to_string(c) + "\t" + std::to_string(w)
                      + "\t" + std::to_string(h) + "\t" + gridHash(g));
        out.push_back("CNT\t" + std::to_string(c) + "\t" + std::to_string(g.buildingTiles)
                      + "\t" + std::to_string(g.roadTiles)
                      + "\t" + std::to_string(g.waterTiles));

        if (c < 3) {
            const char* glyph = ".WRB";
            for (int y = 0; y < h; y++) {
                std::string row;
                for (int x = 0; x < w; x++) {
                    const auto ux = static_cast<std::size_t>(x), uy = static_cast<std::size_t>(y);
                    row += glyph[static_cast<int>(g.cover[ux][uy])];
                    row += g.northWall[ux][uy] ? 'N' : '-';
                    row += g.westWall[ux][uy] ? 'W' : '-';
                }
                out.push_back("CELL\t" + std::to_string(c) + "\t" + std::to_string(y)
                              + "\t" + row);
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: pz_gisraster_oracle <out-path> [cases]\n");
        return 2;
    }
    const int cases = argc > 2 ? std::atoi(argv[2]) : 5000;

    std::vector<std::string> out;
    projection(out);
    waterCodes(out);
    projectionPoints(out, cases);
    raster(out, cases);

    std::string blob;
    for (const std::string& line : out) { blob += line; blob += '\n'; }
    std::ofstream f(argv[1], std::ios::binary);
    if (!f) { std::fprintf(stderr, "cannot write %s\n", argv[1]); return 2; }
    f.write(blob.data(), static_cast<std::streamsize>(blob.size()));
    f.close();
    std::printf("GisRasterOracle c++ : %zu lines, %zu bytes -> %s\n",
                out.size(), blob.size(), argv[1]);
    return 0;
}

// groundregions_oracle.cpp — C++ side of the step-6b oracle for GroundRegions.
// Must emit a digest byte-identical to GroundRegionsOracle.java's.
//
// See GroundRegionsOracle.java for the corpus design. The three points worth
// repeating here:
//
//   1. The VIN section is emitted FIRST and read first. GroundRegions calls two
//      already-ported units (GroundMaterial, MaskRule); a divergence in VIN
//      says the failure is upstream and names which unit, rather than leaving
//      the reader to guess. Same discipline as PalettesOracle's VIN lines.
//
//   2. The H section is the reason this oracle exists in the shape it does.
//      hash01 is a SplitMix64 finaliser that Java computes in `long`, where
//      every multiply overflows and all four constants are negative. In C++
//      that is signed-overflow UB — the identical hazard that deleted
//      nextInt's rejection branch at -O2 one step earlier (FINDINGS F5). The
//      documented mutation is to run groundregions.cpp's hash01 in int64_t and
//      rebuild at -O2.
//
//   3. Every double leaves as RAW BITS. Comparing formatted doubles would test
//      two printf implementations rather than two ports.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "celldata.hpp"
#include "gisimport.hpp"
#include "groundmaterial.hpp"
#include "groundregions.hpp"
#include "java_random.hpp"
#include "maskrule.hpp"

using pzformat::CellData;
using pzformat::GisImport;
using pzformat::GroundMaterial;
using pzformat::GroundRegions;
using pzformat::JavaRandom;
using pzformat::MaskRule;

namespace {

std::vector<std::string> g_out;

void line(const std::string& s) { g_out.push_back(s); }

std::string hex16(std::uint64_t v) {
    char t[32];
    std::snprintf(t, sizeof(t), "%016llx", static_cast<unsigned long long>(v));
    return t;
}

/// Java's Double.doubleToRawLongBits.
std::uint64_t rawBits(double d) {
    std::uint64_t v = 0;
    std::memcpy(&v, &d, sizeof(v));
    return v;
}

std::string bits(double d) { return hex16(rawBits(d)); }

/// FNV-1a 64 over a 64-bit value, byte at a time — mirrors the Java exactly,
/// including that it feeds the value as an unsigned shift of a signed long.
std::uint64_t fnv(std::uint64_t h, std::int64_t value) {
    const std::uint64_t v = static_cast<std::uint64_t>(value);
    for (int i = 0; i < 8; i++) {
        h ^= (v >> (i * 8)) & 0xffULL;
        h *= 0x100000001b3ULL;
    }
    return h;
}

constexpr std::uint64_t kFnvInit = 0xcbf29ce484222325ULL;

std::string s(std::int64_t v) { return std::to_string(v); }
std::string s(int v) { return std::to_string(v); }

using Cell = GroundRegions::Cell;
using Grid = GroundRegions::Grid;

char glyph(Cell m) {
    return m == nullptr ? '.' : static_cast<char>('a' + m->ordinal);
}

Cell mat(char c) {
    if (c == '.') return nullptr;
    return &GroundMaterial::values()[static_cast<std::size_t>(c - 'a')];
}

/// rows[y] is a row; the array is [x][y], matching the Java unit.
Grid grid(const std::vector<std::string>& rows) {
    const std::size_t h = rows.size(), w = rows[0].size();
    Grid g(w, std::vector<Cell>(h, nullptr));
    for (std::size_t y = 0; y < h; y++)
        for (std::size_t x = 0; x < w; x++) g[x][y] = mat(rows[y][x]);
    return g;
}

// ------------------------------------------------------------------
// VIN
// ------------------------------------------------------------------

void vin() {
    for (const GroundMaterial& m : GroundMaterial::values())
        line("VIN\tMAT\t" + s(m.ordinal) + "\t" + std::string(m.name) + "\t"
             + std::string(m.floorMaterial) + "\t" + std::string(m.sheet) + "\t"
             + s(m.block) + "\t" + s(m.variantSets) + "\t" + s(m.rank));
    for (const MaskRule::Dir d : MaskRule::kDirs)
        line(std::string("VIN\tDIR\t") + MaskRule::name(d) + "\t" + s(MaskRule::ord(d))
             + "\t" + s(MaskRule::dx(d)) + "\t" + s(MaskRule::dy(d)) + "\t"
             + MaskRule::name(MaskRule::opposite(d)));
    line("VIN\tCONST\tYARD\t" + s(GroundRegions::YARD));
    line("VIN\tCONST\tVERGE\t" + s(GroundRegions::VERGE));
    line("VIN\tCONST\tMARGIN\t" + s(GroundRegions::MARGIN));
    line("VIN\tCONST\tPLEN\t" + s(static_cast<int>(GroundRegions::P.size())));
    for (std::size_t i = 0; i < GroundRegions::P.size(); i++)
        line("VIN\tP\t" + s(static_cast<int>(i)) + "\t" + bits(GroundRegions::P[i]));
    for (int k = 0; k < 4; k++)
        line("VIN\tDXY\t" + s(k) + "\t" + s(GroundRegions::DX[static_cast<std::size_t>(k)])
             + "\t" + s(GroundRegions::DY[static_cast<std::size_t>(k)]));
    const char* coverNames[] = {"NONE", "WATER", "ROAD", "BUILDING"};
    for (int i = 0; i < 4; i++)
        line(std::string("VIN\tCOVER\t") + s(i) + "\t" + coverNames[i]);
}

// ------------------------------------------------------------------
// H / HR — hash01
// ------------------------------------------------------------------

constexpr std::int32_t kIntMax = 2147483647;
constexpr std::int32_t kIntMin = -2147483647 - 1;

const std::array<int, 26> kHPos{
    0, 1, -1, 2, -2, 3, -3, 7, -7, 255, 256, -255, -256,
    51200, 51455, -51200, 65535, -65535, 1 << 20, -(1 << 20),
    1000003, -1000003, kIntMax, kIntMin, kIntMax - 1, kIntMin + 1,
};

const std::array<std::int64_t, 10> kHSeed{
    0LL, 1LL, -1LL, 12345LL, -12345LL,
    (-9223372036854775807LL - 1),                 // Long.MIN_VALUE
    9223372036854775807LL,                        // Long.MAX_VALUE
    static_cast<std::int64_t>(0x9E3779B97F4A7C15ULL),
    static_cast<std::int64_t>(0xC2B2AE3D27D4EB4FULL),
    -8675309LL,
};

void hashSection() {
    for (const std::int64_t sd : kHSeed)
        for (const int gx : kHPos)
            for (const int gy : kHPos)
                line("H\t" + s(gx) + "\t" + s(gy) + "\t" + s(sd) + "\t"
                     + bits(GroundRegions::hash01(gx, gy, sd)));

    // Positions drawn through the bound that found the F5 rejection bug.
    JavaRandom rng(0x5EEDLL);
    for (int i = 0; i < 6000; i++) {
        const int gx = rng.nextInt(720000) - 360000;
        const int gy = rng.nextInt(720000) - 360000;
        const std::int64_t sd = rng.nextLong();
        line("HR\t" + s(i) + "\t" + s(gx) + "\t" + s(gy) + "\t" + s(sd) + "\t"
             + bits(GroundRegions::hash01(gx, gy, sd)));
    }
}

// ------------------------------------------------------------------
// Grids shared by E and D — ALL SQUARE. See the Java comment: edgeDistance
// bounds both axes with m.length and throws on a non-square grid.
// ------------------------------------------------------------------

const std::vector<std::vector<std::string>> kGrids = {
    {"....", "....", "....", "...."},
    {"aaaaa", "aaaaa", "aaaaa", "aaaaa", "aaaaa"},
    {"aaaabbbb", "aaaabbbb", "aaaabbbb", "aaaabbbb",
     "aaaabbbb", "aaaabbbb", "aaaabbbb", "aaaabbbb"},
    {"aaaaaaa", "aaaaaaa", "aaaaaaa", "aaabaaa", "aaaaaaa", "aaaaaaa", "aaaaaaa"},
    {"ababab", "bababa", "ababab", "bababa", "ababab", "bababa"},
    {"aaa.bbb", "aa..bbb", "aaa.bbb", "aaaabbb", "aaa..bb", "aaaabbb", "aaaabbb"},
    {"aaaccbbb", "aaaccbbb", "aaaccbbb", "aaaccbbb",
     "aaaccbbb", "aaaccbbb", "aaaccbbb", "aaaccbbb"},
    {"a"},
    {"ab", "ba"},
    {"abbbb", "aabbb", "aaabb", "aaaab", "aaaaa"},
    {"aaahhh", "aaahhh", "aaahhh", "aaahhh", "aaahhh", "aaahhh"},
    {"aahh", "aahh", "ddhh", "ddhh"},
    {"gggddd", "gggddd", "gggddd", "gggddd", "gggddd", "gggddd"},
    {".....", ".....", "..a..", ".....", "....."},
    {"a....", ".....", ".....", ".....", "....b"},
};

void edgeSection() {
    for (std::size_t c = 0; c < kGrids.size(); c++) {
        const Grid m = grid(kGrids[c]);
        const int n = static_cast<int>(m.size());
        const int hgt = static_cast<int>(m[0].size());
        Grid across;
        const std::vector<std::vector<int>> d = GroundRegions::edgeDistance(m, across);
        for (int x = 0; x < n; x++) {
            std::string ds, as;
            for (int y = 0; y < hgt; y++) {
                const int v = d[static_cast<std::size_t>(x)][static_cast<std::size_t>(y)];
                if (v < 0) ds += '-';
                else ds += std::to_string(std::min(v, 9));
                if (v > 9) ds += '+';
                as += glyph(across[static_cast<std::size_t>(x)][static_cast<std::size_t>(y)]);
            }
            line("E\t" + s(static_cast<int>(c)) + "\t" + s(x) + "\t" + ds + "\t" + as);
        }
    }
}

const std::array<std::int64_t, 6> kDSeed{
    0LL, 1LL, 7LL, 0x5EEDLL, -1LL, (-9223372036854775807LL - 1)};

const std::array<std::array<int, 2>, 5> kDOrigin{{
    {0, 0}, {-8, -8}, {51200, 51200}, {-51200, -51200},
    {kIntMin / 2, kIntMax / 2},
}};

void ditherSection() {
    int c = 0;
    for (std::size_t gi = 0; gi < kGrids.size(); gi++)
        for (const std::int64_t sd : kDSeed)
            for (const std::array<int, 2>& o : kDOrigin) {
                Grid m = grid(kGrids[gi]);
                GroundRegions::dither(m, o[0], o[1], sd);
                std::string sb;
                for (std::size_t x = 0; x < m.size(); x++)
                    for (std::size_t y = 0; y < m[0].size(); y++) sb += glyph(m[x][y]);
                line("D\t" + s(c) + "\t" + s(static_cast<int>(gi)) + "\t" + s(sd)
                     + "\t" + s(o[0]) + "\t" + s(o[1]) + "\t" + sb);
                c++;
            }
}

// ------------------------------------------------------------------
// Synthetic rasters for C and B
// ------------------------------------------------------------------

GisImport raster(int kind, int w, int h) {
    GisImport g;
    g.width = w;
    g.height = h;
    g.cover.assign(static_cast<std::size_t>(w),
                   std::vector<GisImport::Cover>(static_cast<std::size_t>(h),
                                                 GisImport::Cover::None));
    auto set = [&g](int x, int y, GisImport::Cover c) {
        g.cover[static_cast<std::size_t>(x)][static_cast<std::size_t>(y)] = c;
    };
    switch (kind) {
        case 0:
            break;
        case 1:
            for (int x = 0; x < w; x++)
                for (int y = 0; y < h; y++) set(x, y, GisImport::Cover::Building);
            break;
        case 2:
            for (int x = 0; x < w; x++)
                for (int y = h / 2 - 3; y <= h / 2 + 3; y++)
                    if (y >= 0 && y < h) set(x, y, GisImport::Cover::Road);
            break;
        case 3:
            for (int x = 0; x < w; x++)
                for (int y = 0; y < h; y++) {
                    if (x % 37 == 0 || y % 41 == 0) set(x, y, GisImport::Cover::Road);
                    if ((x / 13) % 3 == 1 && (y / 11) % 3 == 1)
                        set(x, y, GisImport::Cover::Building);
                }
            break;
        case 4:
            for (int x = 0; x < w; x++)
                for (int y = 0; y < h; y++) set(x, y, GisImport::Cover::Water);
            break;
        case 5:
            for (int x = 0; x < w; x++)
                for (int y = 0; y < h; y++) {
                    if (x < w / 3) set(x, y, GisImport::Cover::Water);
                    else if (x < w / 3 + 5) set(x, y, GisImport::Cover::Road);
                    else if (x > w - 20 && y > h - 20) set(x, y, GisImport::Cover::Building);
                }
            break;
        case 6: {
            JavaRandom r(99LL);
            for (int x = 0; x < w; x++)
                for (int y = 0; y < h; y++) {
                    const std::int32_t v = r.nextInt(20);
                    if (v == 0) set(x, y, GisImport::Cover::Building);
                    else if (v == 1) set(x, y, GisImport::Cover::Road);
                    else if (v == 2) set(x, y, GisImport::Cover::Water);
                }
            break;
        }
        case 7:
            if (w > 100 && h > 100) set(100, 100, GisImport::Cover::Building);
            break;
        case 8:
            if (w > 100 && h > 100) set(100, 100, GisImport::Cover::Road);
            break;
        default:
            break;
    }
    return g;
}

void coverSection() {
    const std::array<std::array<int, 2>, 5> origins{{
        {0, 0}, {-40, -40}, {100, 100}, {200, 300}, {-300, 120}}};
    const std::array<GisImport::Cover, 2> targets{
        GisImport::Cover::Building, GisImport::Cover::Road};
    const char* targetNames[] = {"BUILDING", "ROAD"};
    int c = 0;
    for (int kind = 0; kind <= 8; kind++) {
        const GisImport g = raster(kind, 320, 300);
        for (const std::array<int, 2>& o : origins)
            for (std::size_t t = 0; t < 2; t++) {
                const std::vector<std::vector<int>> d = GroundRegions::coverDistance(
                    g, o[0], o[1], GroundRegions::MARGIN, targets[t]);
                std::uint64_t h = kFnvInit;
                std::int64_t zero = 0, unreached = 0, maxFinite = -1;
                for (const std::vector<int>& row : d)
                    for (const int v : row) {
                        h = fnv(h, v);
                        if (v == 0) zero++;
                        else if (v == kIntMax) unreached++;
                        if (v != kIntMax && v > maxFinite) maxFinite = v;
                    }
                line("C\t" + s(c) + "\t" + s(kind) + "\t" + s(o[0]) + "\t" + s(o[1])
                     + "\t" + targetNames[t] + "\t" + hex16(h) + "\t" + s(zero)
                     + "\t" + s(unreached) + "\t" + s(maxFinite));
                c++;
            }
    }
}

void buildSection() {
    const std::array<std::array<int, 2>, 3> origins{{{0, 0}, {64, 64}, {-100, 50}}};
    const std::array<std::int64_t, 3> seeds{0LL, 0x5EEDLL, -1LL};
    int c = 0;
    for (int kind = 0; kind <= 8; kind++) {
        const GisImport g = raster(kind, 320, 300);
        const std::array<int, 2>& o = origins[static_cast<std::size_t>(kind) % 3];
        const std::int64_t sd = seeds[static_cast<std::size_t>(kind) % 3];
        const Grid r = GroundRegions::build(g, o[0], o[1], sd);
        std::uint64_t h = kFnvInit;
        std::array<std::int64_t, 15> hist{};
        hist.fill(0);
        for (std::size_t x = 0; x < 258; x++)
            for (std::size_t y = 0; y < 258; y++) {
                const Cell m = r[x][y];
                const int ord = m == nullptr ? 14 : m->ordinal;
                h = fnv(h, ord);
                hist[static_cast<std::size_t>(ord)]++;
            }
        std::string hs;
        for (std::size_t i = 0; i < 15; i++) {
            if (i > 0) hs += ',';
            hs += std::to_string(hist[i]);
        }
        line("B\t" + s(c) + "\t" + s(kind) + "\t" + s(o[0]) + "\t" + s(o[1]) + "\t"
             + s(sd) + "\t" + hex16(h) + "\t" + hs);
        if (kind <= 2 || kind == 5) {
            for (std::size_t x = 0; x < 40; x++) {
                std::string row;
                for (std::size_t y = 0; y < 40; y++) row += glyph(r[x][y]);
                line("BW\t" + s(c) + "\t" + s(static_cast<int>(x)) + "\t" + row);
            }
        }
        c++;
    }
}

// ------------------------------------------------------------------
// M — addMasks
// ------------------------------------------------------------------

/// A 3x3 region so cell-local (0,0) is region[1][1] with four neighbours.
Grid window(Cell self, const std::array<Cell, 4>& nb) {
    Grid r(3, std::vector<Cell>(3, nullptr));
    r[1][1] = self;
    for (std::size_t i = 0; i < 4; i++) {
        const MaskRule::Dir d = MaskRule::kDirs[i];
        r[static_cast<std::size_t>(1 + MaskRule::dx(d))]
         [static_cast<std::size_t>(1 + MaskRule::dy(d))] = nb[i];
    }
    return r;
}

std::string stackNames(const std::vector<std::int32_t>& stack, const CellData& cell) {
    std::string sb;
    for (std::size_t i = 0; i < stack.size(); i++) {
        if (i > 0) sb += ',';
        sb += cell.header().tileNames[static_cast<std::size_t>(stack[i])];
    }
    return sb.empty() ? std::string("-") : sb;
}

const GroundMaterial* M(int ordinal) {
    return &GroundMaterial::values()[static_cast<std::size_t>(ordinal)];
}
// Ordinals, matching GroundMaterial's declaration order exactly.
constexpr int GRASS_DARK = 0, GRASS_MEDIUM = 1, GRASS_LIGHT = 2, SAND = 3;
constexpr int DIRT_GRASS = 4, DIRT = 5, CLAY = 6;
constexpr int ROAD_01 = 7, ROAD_02 = 8, ROAD_04 = 9;
constexpr int ROAD_05 = 11, ROAD_06 = 13;

void maskSection() {
    CellData cell = CellData::blank(CellData::newHeader({}, 0, 0), 1);
    int c = 0;

    {
        std::vector<std::int32_t> stack;
        JavaRandom rng(1LL);
        const std::array<Cell, 4> nb{M(GRASS_DARK), nullptr, nullptr, nullptr};
        GroundRegions::addMasks(stack, cell, window(nullptr, nb), 0, 0, nullptr, rng);
        line("M\t" + s(c) + "\tNULLSELF\t" + stackNames(stack, cell) + "\t"
             + s(rng.nextInt()));
        c++;
    }

    const std::array<std::array<int, 2>, 5> pairs{{
        {GRASS_MEDIUM, GRASS_DARK},
        {CLAY, SAND},
        {ROAD_04, GRASS_DARK},
        {ROAD_06, ROAD_01},
        {DIRT, DIRT_GRASS},
    }};
    for (const std::array<int, 2>& pr : pairs)
        for (int bitsv = 0; bitsv < 16; bitsv++)
            for (std::int64_t seed = 0; seed < 4; seed++) {
                std::array<Cell, 4> nb{};
                for (std::size_t i = 0; i < 4; i++)
                    nb[i] = ((bitsv >> i) & 1) != 0 ? M(pr[1]) : nullptr;
                std::vector<std::int32_t> stack;
                JavaRandom rng(seed * 1000003LL + bitsv);
                GroundRegions::addMasks(stack, cell, window(M(pr[0]), nb), 0, 0,
                                        M(pr[0]), rng);
                line("M\t" + s(c) + "\tS1\t" + std::string(M(pr[0])->name) + "\t"
                     + std::string(M(pr[1])->name) + "\t" + s(bitsv) + "\t" + s(seed)
                     + "\t" + stackNames(stack, cell) + "\t" + s(rng.nextInt()));
                c++;
            }

    const std::array<std::array<int, 3>, 4> triples{{
        {GRASS_LIGHT, GRASS_DARK, GRASS_MEDIUM},
        {CLAY, DIRT, GRASS_DARK},
        {ROAD_04, GRASS_DARK, ROAD_01},
        {ROAD_05, ROAD_04, ROAD_02},
    }};
    for (const std::array<int, 3>& tr : triples)
        for (int a = 0; a < 81; a++) {
            int v = a;
            std::array<Cell, 4> nb{};
            for (std::size_t i = 0; i < 4; i++) {
                const int k = v % 3;
                v /= 3;
                nb[i] = k == 0 ? nullptr : (k == 1 ? M(tr[1]) : M(tr[2]));
            }
            std::vector<std::int32_t> stack;
            JavaRandom rng(a * 31LL + 17LL);
            GroundRegions::addMasks(stack, cell, window(M(tr[0]), nb), 0, 0, M(tr[0]), rng);
            line("M\t" + s(c) + "\tS2\t" + std::string(M(tr[0])->name) + "\t"
                 + std::string(M(tr[1])->name) + "\t" + std::string(M(tr[2])->name)
                 + "\t" + s(a) + "\t" + stackNames(stack, cell) + "\t" + s(rng.nextInt()));
            c++;
        }

    for (const int selfOrd : {GRASS_DARK, ROAD_01}) {
        const std::array<Cell, 4> nb{M(CLAY), M(DIRT), M(ROAD_06), M(selfOrd)};
        std::vector<std::int32_t> stack;
        JavaRandom rng(5LL);
        GroundRegions::addMasks(stack, cell, window(M(selfOrd), nb), 0, 0, M(selfOrd), rng);
        line("M\t" + s(c) + "\tNOOUT\t" + std::string(M(selfOrd)->name) + "\t"
             + stackNames(stack, cell) + "\t" + s(rng.nextInt()));
        c++;
    }

    const GisImport g = raster(3, 320, 300);
    const Grid region = GroundRegions::build(g, 0, 0, 0x5EEDLL);
    JavaRandom rng(2024LL);
    const std::array<std::array<int, 2>, 12> probes{{
        {0, 0}, {0, 255}, {255, 0}, {255, 255}, {0, 128}, {255, 128},
        {128, 0}, {128, 255}, {1, 1}, {254, 254}, {64, 64}, {13, 199}}};
    for (const std::array<int, 2>& p : probes) {
        std::vector<std::int32_t> stack;
        const Cell self = region[static_cast<std::size_t>(p[0] + 1)]
                                [static_cast<std::size_t>(p[1] + 1)];
        GroundRegions::addMasks(stack, cell, region, p[0], p[1], self, rng);
        line("M\t" + s(c) + "\tEDGE\t" + s(p[0]) + "\t" + s(p[1]) + "\t"
             + std::string(1, glyph(self)) + "\t" + stackNames(stack, cell)
             + "\t" + s(rng.nextInt()));
        c++;
    }

    JavaRandom sweep(7LL);
    std::int64_t tiles = 0;
    for (int x = 0; x < 256; x++)
        for (int y = 0; y < 256; y++) {
            std::vector<std::int32_t> stack;
            GroundRegions::addMasks(stack, cell, region, x, y,
                                    region[static_cast<std::size_t>(x + 1)]
                                          [static_cast<std::size_t>(y + 1)], sweep);
            tiles += static_cast<std::int64_t>(stack.size());
        }
    line("MSWEEP\t" + s(tiles) + "\t" + s(sweep.nextInt()) + "\t"
         + s(static_cast<int>(cell.header().tileNames.size())));
    for (std::size_t i = 0; i < cell.header().tileNames.size(); i++)
        line("MTILE\t" + s(static_cast<int>(i)) + "\t" + cell.header().tileNames[i]);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: pz_groundregions_oracle <out-path>\n");
        return 2;
    }
    vin();
    hashSection();
    edgeSection();
    ditherSection();
    coverSection();
    buildSection();
    maskSection();

    std::string blob;
    for (const std::string& l : g_out) { blob += l; blob += '\n'; }
    std::ofstream f(argv[1], std::ios::binary);
    if (!f) { std::fprintf(stderr, "cannot write %s\n", argv[1]); return 2; }
    f.write(blob.data(), static_cast<std::streamsize>(blob.size()));
    f.close();
    std::printf("GroundRegionsOracle c++ : %zu lines, %zu bytes -> %s\n",
                g_out.size(), blob.size(), argv[1]);
    return 0;
}

// treescatter_oracle.cpp — C++ side of the step-7 oracle for TreePalette and
// TreeScatter. Must emit a digest byte-identical to TreeScatterOracle.java's.
//
// See TreeScatterOracle.java for the corpus design. Three points repeated here:
//
//   1. The PL section's stream fingerprint is the draw-count check. Java's
//      `tp.hasStump && rng.nextDouble() < P_STUMP` short-circuits, so a palette
//      without a stump tile draws a different number of times for EVERY square.
//      A port that emits identical tiles while drawing a different number of
//      times diverges only there.
//
//   2. nextInt's rejection loop is NOT exercised here and the Java comment says
//      why: the bound is the palette size, production uses 8 (a power of two),
//      and every palette in this corpus gives a rejection probability below
//      1e-5. Stating the expectation is the point — §41's palettes corpus
//      expected 0.02 rejections, fired none, and passed on luck. The loop is
//      covered by pz_rng_oracle with adversarial bounds.
//
//   3. Doubles leave as raw bits. Comparing formatted doubles would test two
//      printf implementations rather than two ports.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

#include "gisimport.hpp"
#include "java_random.hpp"
#include "tile.hpp"
#include "tileindex.hpp"
#include "treepalette.hpp"
#include "treescatter.hpp"

using pzformat::GisImport;
using pzformat::JavaRandom;
using pzformat::Tile;
using pzformat::TileIndex;
using pzformat::TreePalette;
using pzformat::TreeScatter;

namespace {

std::vector<std::string> g_out;
void line(const std::string& s) { g_out.push_back(s); }

std::string hex16(std::uint64_t v) {
    char t[32];
    std::snprintf(t, sizeof(t), "%016llx", static_cast<unsigned long long>(v));
    return t;
}

std::string bits(double d) {
    std::uint64_t v = 0;
    std::memcpy(&v, &d, sizeof(v));
    return hex16(v);
}

std::uint64_t fnv(std::uint64_t h, std::int64_t value) {
    const std::uint64_t v = static_cast<std::uint64_t>(value);
    for (int i = 0; i < 8; i++) { h ^= (v >> (i * 8)) & 0xffULL; h *= 0x100000001b3ULL; }
    return h;
}
constexpr std::uint64_t kFnvInit = 0xcbf29ce484222325ULL;

std::string s(std::int64_t v) { return std::to_string(v); }
std::string s(int v) { return std::to_string(v); }
std::string tf(bool b) { return b ? "true" : "false"; }

constexpr std::int32_t kIntMax = 2147483647;
constexpr std::int32_t kIntMin = -2147483647 - 1;

Tile tile(const std::string& name, const std::string& tileset,
          const std::vector<std::pair<std::string, std::string>>& kv) {
    Tile t;
    t.name = name;
    t.tileset = tileset;
    for (const auto& [k, v] : kv) t.props.put(k, v);
    return t;
}

// ------------------------------------------------------------------

void vin() {
    line("VIN\tCONST\tCLEAR\t" + s(TreeScatter::CLEAR));
    line("VIN\tCONST\tSPACING\t" + s(TreeScatter::SPACING));
    line("VIN\tCONST\tP_STUMP\t" + bits(TreeScatter::P_STUMP));
    line(std::string("VIN\tSHEET\t") + TreePalette::SHEET);
    line(std::string("VIN\tSTUMP\t") + TreePalette::STUMP);
    const auto& bs = TreeScatter::bands();
    for (std::size_t i = 0; i < bs.size(); i++)
        line("VIN\tBAND\t" + s(static_cast<int>(i)) + "\t" + s(bs[i].maxDist) + "\t"
             + s(bs[i].size) + "\t" + bits(bs[i].density) + "\t" + bs[i].label);
}

// ------------------------------------------------------------------

const std::vector<std::string> kTreeValues = {
    "2", " 2 ", "+2", "-2", "007", "0", "1", "3", "12",
    "2.0", "", "   ", "abc", "0x2", "1_0", "2 3",
    "2147483647", "2147483648", "-2147483648", "-2147483649",
    std::string("2\0", 2), "\v2", "\t\n2\r", "1\xc2\xa0", "  -4  ", "+0",
    "+", "-", "+-3", "-+3", "++2",
};

std::string esc(const std::string& v) {
    // Java iterates CHARS; the C++ string is UTF-8 bytes. The only non-ASCII
    // value in the corpus is U+00A0, which Java escapes as <a0> from one char
    // and which arrives here as the two bytes c2 a0. Decoded so both sides
    // print <a0>. Anything wider than Latin-1 would need more, and cannot
    // occur — the falsifier in the Java side forbids non-ASCII digits.
    std::string b;
    for (std::size_t i = 0; i < v.size(); i++) {
        unsigned char c = static_cast<unsigned char>(v[i]);
        unsigned int cp = c;
        if (c == 0xC2 && i + 1 < v.size()) { cp = static_cast<unsigned char>(v[++i]); }
        else if (c == 0xC3 && i + 1 < v.size()) {
            cp = 0x40u + static_cast<unsigned char>(v[++i]);
        }
        if (cp < 0x20 || cp > 0x7e) {
            char t[8];
            std::snprintf(t, sizeof(t), "<%02x>", cp);
            b += t;
        } else {
            b += static_cast<char>(cp);
        }
    }
    return b.empty() ? "<empty>" : b;
}

void parseSection() {
    for (std::size_t i = 0; i < kTreeValues.size(); i++) {
        const std::string& v = kTreeValues[i];
        TileIndex ti;
        const std::string n = std::string(TreePalette::SHEET) + "_" + s(static_cast<int>(i));
        ti.add(tile(n, TreePalette::SHEET, {{"tree", v}, {"solid", ""}}));
        const TreePalette p = TreePalette::pick(ti, {});
        std::string sizes;
        for (const auto& [k, tiles] : p.bySize()) {
            (void)tiles;
            if (!sizes.empty()) sizes += ',';
            sizes += s(k);
        }
        line("PI\t" + s(static_cast<int>(i)) + "\t" + esc(v) + "\t"
             + s(static_cast<int>(p.all.size())) + "\t"
             + (sizes.empty() ? "-" : sizes) + "\t" + tf(p.usable()));
    }
}

// ------------------------------------------------------------------

constexpr int kTileSets = 9;

TileIndex tileSet(int kind) {
    TileIndex ti;
    const std::string S = TreePalette::SHEET;
    switch (kind) {
        case 0: break;
        case 1:
            for (int i = 8; i < 16; i++)
                ti.add(tile(S + "_" + s(i), S, {{"tree", "2"}, {"solid", ""}}));
            break;
        case 2:
            ti.add(tile(S + "_1", S, {{"tree", "2"}, {"solid", ""}}));
            ti.add(tile("other_01_1", "other_01", {{"tree", "2"}, {"solid", ""}}));
            ti.add(tile(S + "_3", S, {{"solid", ""}}));
            ti.add(tile(S + "_4", S, {{"tree", ""}, {"solid", ""}}));
            ti.add(tile(S + "_5", S, {{"tree", "2"}}));
            ti.add(tile(S + "_6", S, {{"tree", "x"}, {"solid", ""}}));
            ti.add(tile(S + "_7", S, {{"tree", "2"}, {"solid", ""}, {"wall", ""}}));
            ti.add(tile(S + "_9", S, {{"tree", "2"}, {"solid", ""}, {"DoorWallN", ""}}));
            ti.add(tile(S + "_2", S, {{"MoveWithWind", ""}, {"solid", ""}}));
            break;
        case 3:
            ti.add(tile(S + "_10", S, {{"tree", "1"}, {"solid", ""}}));
            ti.add(tile(S + "_11", S, {{"tree", "3"}, {"solid", ""}}));
            ti.add(tile(S + "_12", S, {{"tree", "7"}, {"solid", ""}}));
            ti.add(tile(S + "_13", S, {{"tree", "-2"}, {"solid", ""}}));
            break;
        case 4:
            ti.add(tile(S + "_20", S, {{"tree", "1"}, {"solid", ""}}));
            break;
        case 5:
            for (int i = 30; i < 33; i++)
                ti.add(tile(S + "_" + s(i), S, {{"tree", "2"}, {"solid", ""}}));
            break;
        case 6:
            for (int i = 40; i < 47; i++)
                ti.add(tile(S + "_" + s(i), S, {{"tree", "2"}, {"solid", ""}}));
            break;
        case 8:
            ti.add(tile(S + "_50", S, {{"tree", "20"}, {"solid", ""}}));
            break;
        case 7:
            for (const char* suf : {"_9", "_10", "_8", "_11", "_100", "_2"})
                ti.add(tile(S + suf, S, {{"tree", "2"}, {"solid", ""}}));
            break;
        default: break;
    }
    return ti;
}

std::string join(const std::vector<std::string>* xs) {
    if (xs == nullptr) return "<null>";
    if (xs->empty()) return "<empty>";
    std::string b;
    for (std::size_t i = 0; i < xs->size(); i++) { if (i > 0) b += ','; b += (*xs)[i]; }
    return b;
}

void paletteSection() {
    for (int k = 0; k < kTileSets; k++)
        for (int st = 0; st < 2; st++) {
            std::unordered_set<std::string> sprites;
            if (st == 1) sprites.insert(TreePalette::STUMP);
            const TreePalette p = TreePalette::pick(tileSet(k), sprites);
            line("TP\t" + s(k) + "\t" + s(st) + "\tusable=" + tf(p.usable())
                 + "\thasStump=" + tf(p.hasStump) + "\tall=" + join(&p.all));
            line("TPS\t" + s(k) + "\t" + s(st) + "\t" + p.toString());
            for (const auto& [size, tiles] : p.bySize()) {
                (void)tiles;
                line("TPB\t" + s(k) + "\t" + s(st) + "\t" + s(size) + "\t"
                     + join(p.tilesNear(size)));
            }
        }
}

void tilesNearSection() {
    for (int k = 0; k < kTileSets; k++) {
        const TreePalette p = TreePalette::pick(tileSet(k), {});
        for (int size = -20; size <= 20; size++)
            line("TN\t" + s(k) + "\t" + s(size) + "\t" + join(p.tilesNear(size)));
    }
}

void bandSection() {
    for (int d = -5; d <= 60; d++) line("BF\t" + s(d) + "\t" + s(TreeScatter::bandFor(d)));
    line("BF\t" + s(kIntMax) + "\t" + s(TreeScatter::bandFor(kIntMax)));
    line("BF\t" + s(kIntMin) + "\t" + s(TreeScatter::bandFor(kIntMin)));
}

// ------------------------------------------------------------------

GisImport raster(int kind, int w, int h) {
    GisImport g;
    g.width = w;
    g.height = h;
    const std::size_t uw = static_cast<std::size_t>(w), uh = static_cast<std::size_t>(h);
    g.cover.assign(uw, std::vector<GisImport::Cover>(uh, GisImport::Cover::None));
    g.northWall.assign(uw, std::vector<bool>(uh, false));
    g.westWall.assign(uw, std::vector<bool>(uh, false));
    auto C = [&g](int x, int y) -> GisImport::Cover& {
        return g.cover[static_cast<std::size_t>(x)][static_cast<std::size_t>(y)];
    };
    switch (kind) {
        case 0: break;
        case 1:
            for (int x = 0; x < w; x++)
                for (int y = h / 2 - 1; y <= h / 2 + 1; y++)
                    if (y >= 0 && y < h) C(x, y) = GisImport::Cover::Road;
            break;
        case 2:
            for (int x = w / 4; x < w / 4 + 6 && x < w; x++)
                for (int y = h / 4; y < h / 4 + 6 && y < h; y++)
                    C(x, y) = GisImport::Cover::Building;
            break;
        case 3:
            for (int x = 0; x < w; x += 7)
                for (int y = 0; y < h; y++)
                    g.northWall[static_cast<std::size_t>(x)][static_cast<std::size_t>(y)] = true;
            for (int y = 0; y < h; y += 11)
                for (int x = 0; x < w; x++)
                    g.westWall[static_cast<std::size_t>(x)][static_cast<std::size_t>(y)] = true;
            break;
        case 4:
            for (int x = 0; x < w; x++)
                for (int y = 0; y < h; y++) C(x, y) = GisImport::Cover::Building;
            break;
        case 5:
            for (int x = 0; x < w; x++)
                for (int y = 0; y < h; y++) C(x, y) = GisImport::Cover::Water;
            break;
        case 6: {
            JavaRandom r(31LL);
            for (int x = 0; x < w; x++)
                for (int y = 0; y < h; y++) {
                    const std::int32_t v = r.nextInt(50);
                    if (v == 0) C(x, y) = GisImport::Cover::Building;
                    else if (v == 1) C(x, y) = GisImport::Cover::Road;
                    else if (v == 2)
                        g.northWall[static_cast<std::size_t>(x)][static_cast<std::size_t>(y)] = true;
                }
            break;
        }
        case 7: C(0, 0) = GisImport::Cover::Road; break;
        case 8: C(w - 1, h - 1) = GisImport::Cover::Building; break;
        default: break;
    }
    return g;
}

const std::array<std::array<int, 2>, 6> kShapes{{
    {40, 40}, {57, 23}, {23, 57}, {1, 30}, {30, 1}, {2, 2}}};

void distanceSection() {
    for (int kind = 0; kind <= 8; kind++)
        for (const auto& sh : kShapes) {
            const GisImport g = raster(kind, sh[0], sh[1]);
            const std::vector<std::vector<int>> d = TreeScatter::distanceToStructure(g);
            std::uint64_t hsh = kFnvInit;
            std::int64_t zero = 0, unreached = 0, maxFinite = -1;
            for (const auto& col : d)
                for (const int v : col) {
                    hsh = fnv(hsh, v);
                    if (v == 0) zero++;
                    else if (v == kIntMax) unreached++;
                    if (v != kIntMax && v > maxFinite) maxFinite = v;
                }
            line("DS\t" + s(kind) + "\t" + s(sh[0]) + "x" + s(sh[1]) + "\t" + hex16(hsh)
                 + "\t" + s(zero) + "\t" + s(unreached) + "\t" + s(maxFinite));
            if (sh[0] == 40 && sh[1] == 40 && kind <= 3)
                for (int x = 0; x < 40; x++) {
                    std::string row;
                    for (int y = 0; y < 40; y++) {
                        const int v = d[static_cast<std::size_t>(x)][static_cast<std::size_t>(y)];
                        if (v == kIntMax) row += "  .";
                        else {
                            char t[16];
                            std::snprintf(t, sizeof(t), "%3d", std::min(v, 999));
                            row += t;
                        }
                    }
                    line("DSW\t" + s(kind) + "\t" + s(x) + "\t" + row);
                }
        }
}

void tooCloseSection() {
    const int w = 9, h = 7;
    for (int mode = 0; mode < 4; mode++) {
        std::vector<std::vector<bool>> taken(static_cast<std::size_t>(w),
                                             std::vector<bool>(static_cast<std::size_t>(h), false));
        switch (mode) {
            case 0: break;
            case 1: taken[0][0] = true; break;
            case 2: taken[static_cast<std::size_t>(w - 1)][static_cast<std::size_t>(h - 1)] = true; break;
            default:
                for (int i = 0; i < w; i++)
                    taken[static_cast<std::size_t>(i)][static_cast<std::size_t>(h / 2)] = true;
                break;
        }
        for (int x = 0; x < w; x++) {
            std::string row;
            for (int y = 0; y < h; y++)
                row += TreeScatter::tooClose(taken, x, y, w, h) ? 'X' : '.';
            line("TC\t" + s(mode) + "\t" + s(x) + "\t" + row);
        }
    }
}

// ------------------------------------------------------------------

const std::array<std::int64_t, 5> kSeeds{
    0LL, 1LL, 0x5EEDLL, -1LL, (-9223372036854775807LL - 1)};

void placeSection() {
    int c = 0;
    for (int kind = 0; kind <= 8; kind++)
        for (int ts = 0; ts < kTileSets; ts++)
            for (int st = 0; st < 2; st++) {
                const std::int64_t seed =
                    kSeeds[static_cast<std::size_t>((kind + ts + st) % 5)];
                const auto& sh = kShapes[static_cast<std::size_t>((kind + ts) % 6)];
                const GisImport g = raster(kind, sh[0], sh[1]);
                std::unordered_set<std::string> sprites;
                if (st == 1) sprites.insert(TreePalette::STUMP);
                const TreePalette tp = TreePalette::pick(tileSet(ts), sprites);

                std::vector<std::string> log;
                const TreeScatter::Placement out = TreeScatter::place(g, tp, seed, log);

                std::uint64_t hsh = kFnvInit;
                std::int64_t placed = 0, stumps = 0;
                std::vector<std::string> names;
                for (int x = 0; x < sh[0]; x++)
                    for (int y = 0; y < sh[1]; y++) {
                        const std::string& n =
                            out[static_cast<std::size_t>(x)][static_cast<std::size_t>(y)];
                        hsh = fnv(hsh, n.empty() ? 0 : 1);
                        if (n.empty()) continue;
                        for (const char ch : n)
                            hsh = fnv(hsh, static_cast<unsigned char>(ch));
                        placed++;
                        if (n == TreePalette::STUMP) stumps++;
                        names.push_back(s(x) + ":" + s(y) + ":" + n);
                    }
                line("PL\t" + s(c) + "\t" + s(kind) + "\t" + s(ts) + "\t" + s(st) + "\t"
                     + s(seed) + "\t" + s(sh[0]) + "x" + s(sh[1]) + "\t" + hex16(hsh)
                     + "\t" + s(placed) + "\t" + s(stumps));
                for (const std::string& l : log) line("PLLOG\t" + s(c) + "\t" + l);
                for (const std::string& n : names) line("PLT\t" + s(c) + "\t" + n);
                c++;
            }

    JavaRandom shared(2026LL);
    for (int kind = 0; kind <= 8; kind++)
        for (int ts = 0; ts < kTileSets; ts++) {
            const GisImport g = raster(kind, 31, 19);
            std::unordered_set<std::string> sprites{TreePalette::STUMP};
            const TreePalette tp = TreePalette::pick(tileSet(ts), sprites);
            const std::int64_t seed = shared.nextLong();
            std::vector<std::string> log;
            TreeScatter::place(g, tp, seed, log);
            line("PLSTREAM\t" + s(kind) + "\t" + s(ts) + "\t" + s(seed) + "\t"
                 + s(shared.nextInt()));
        }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: pz_treescatter_oracle <out-path>\n");
        return 2;
    }
    vin();
    parseSection();
    paletteSection();
    tilesNearSection();
    bandSection();
    distanceSection();
    tooCloseSection();
    placeSection();

    std::string blob;
    for (const std::string& l : g_out) { blob += l; blob += '\n'; }
    std::ofstream f(argv[1], std::ios::binary);
    if (!f) { std::fprintf(stderr, "cannot write %s\n", argv[1]); return 2; }
    f.write(blob.data(), static_cast<std::streamsize>(blob.size()));
    f.close();
    std::printf("TreeScatterOracle c++ : %zu lines, %zu bytes -> %s\n",
                g_out.size(), blob.size(), argv[1]);
    return 0;
}

// biomemap_oracle.cpp — C++ side of the step-7 oracle for BiomeMapWriter and
// the PNG encoder. Must emit a digest byte-identical to BiomeMapOracle.java's.
//
// See BiomeMapOracle.java for the corpus design. The PNG section is the
// STANDING FALSIFIER for the encoder: Java's ImageIO and this port agree only
// because Java's Deflater is zlib and the level (4) was measured rather than
// documented. Re-run it whenever either toolchain moves.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "biomemapwriter.hpp"
#include "gisimport.hpp"
#include "java_random.hpp"
#include "pzpng.hpp"
#include "treescatter.hpp"

using pzformat::BiomeMapWriter;
using pzformat::GisImport;
using pzformat::JavaRandom;
using pzformat::TreeScatter;
using pzformat::writePngRgb;

namespace fs = std::filesystem;

namespace {

std::vector<std::string> g_out;
void line(const std::string& s) { g_out.push_back(s); }

std::uint64_t fnv(std::uint64_t h, std::int64_t value) {
    const std::uint64_t v = static_cast<std::uint64_t>(value);
    for (int i = 0; i < 8; i++) { h ^= (v >> (i * 8)) & 0xffULL; h *= 0x100000001b3ULL; }
    return h;
}
constexpr std::uint64_t kFnvInit = 0xcbf29ce484222325ULL;

std::uint64_t fnvBytes(const std::vector<unsigned char>& b) {
    std::uint64_t h = kFnvInit;
    // Java feeds `x & 0xff` from a signed byte; unsigned char is already that.
    for (const unsigned char x : b) h = fnv(h, x);
    return h;
}

std::string hex(std::uint64_t v) {
    char t[32];
    std::snprintf(t, sizeof(t), "%016llx", static_cast<unsigned long long>(v));
    return t;
}

std::string s(int v) { return std::to_string(v); }
std::string s(std::int64_t v) { return std::to_string(v); }
std::string s(std::size_t v) { return std::to_string(v); }

inline std::size_t uz(int v) { return static_cast<std::size_t>(v); }

// ------------------------------------------------------------------

void vin() {
    line("VIN\tTOWN\t" + s(BiomeMapWriter::TOWN));
    line("VIN\tFARM\t" + s(BiomeMapWriter::FARM));
    line("VIN\tFARMLAND\t" + s(BiomeMapWriter::FARMLAND));
    line("VIN\tFARM_FOREST\t" + s(BiomeMapWriter::FARM_FOREST));
    line("VIN\tPH_FOREST\t" + s(BiomeMapWriter::PH_FOREST));
    line("VIN\tBIRCH_FOREST\t" + s(BiomeMapWriter::BIRCH_FOREST));
    line("VIN\tDEEP_FOREST\t" + s(BiomeMapWriter::DEEP_FOREST));
    line("VIN\tDIRT\t" + s(BiomeMapWriter::DIRT));
    line("VIN\tTOWN_RADIUS\t" + s(BiomeMapWriter::TOWN_RADIUS));
    line("VIN\tEDGE_RADIUS\t" + s(BiomeMapWriter::EDGE_RADIUS));
    line("VIN\tFOREST_RADIUS\t" + s(BiomeMapWriter::FOREST_RADIUS));
}

// ------------------------------------------------------------------

std::vector<unsigned char> sweepBuffer(JavaRandom& r, int mode) {
    std::vector<unsigned char> buf(256u * 256u * 3u, 0);
    const int c1 = r.nextInt(256), c2 = r.nextInt(256), c3 = r.nextInt(256);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            int v;
            switch (mode) {
                case 0: v = c1; break;
                case 1: v = ((x / 16 + y / 16) % 2 == 0) ? c1 : c2; break;
                case 2: v = (x * c1 + y * c2) & 0xff; break;
                case 3: v = r.nextInt(256); break;
                default: {
                    const int d = std::max(std::abs(x - c1), std::abs(y - c2));
                    v = d <= 10 ? c1 : d <= 28 ? c2 : d <= 70 ? c3 : 255;
                }
            }
            const std::size_t i = (uz(y) * 256u + uz(x)) * 3u;
            const unsigned char b = static_cast<unsigned char>(v);
            buf[i] = b; buf[i + 1] = b; buf[i + 2] = b;
        }
    return buf;
}

void pngSection() {
    JavaRandom r(4242LL);
    for (int k = 0; k < 200; k++) {
        const std::vector<unsigned char> rgb = sweepBuffer(r, k % 5);
        const std::vector<unsigned char> png = writePngRgb(rgb, 256, 256);
        line("PNG\t" + s(k) + "\t" + s(png.size()) + "\t" + hex(fnvBytes(png)));
    }
}

// ------------------------------------------------------------------

GisImport raster(int kind, int w, int h) {
    GisImport g;
    g.width = w; g.height = h;
    const std::size_t uw = uz(w), uh = uz(h);
    g.cover.assign(uw, std::vector<GisImport::Cover>(uh, GisImport::Cover::None));
    g.northWall.assign(uw, std::vector<bool>(uh, false));
    g.westWall.assign(uw, std::vector<bool>(uh, false));
    auto C = [&g](int x, int y) -> GisImport::Cover& { return g.cover[uz(x)][uz(y)]; };
    switch (kind) {
        case 0: break;
        case 1:
            for (int x = 0; x < w; x++)
                for (int y = h / 2 - 1; y <= h / 2 + 1; y++)
                    if (y >= 0 && y < h) C(x, y) = GisImport::Cover::Road;
            break;
        case 2:
            for (int x = w / 4; x < w / 4 + 8 && x < w; x++)
                for (int y = h / 4; y < h / 4 + 8 && y < h; y++)
                    C(x, y) = GisImport::Cover::Building;
            break;
        case 3:
            C(0, 0) = GisImport::Cover::Building;
            break;
        default: {
            JavaRandom r(7LL);
            for (int x = 0; x < w; x++)
                for (int y = 0; y < h; y++) {
                    const std::int32_t v = r.nextInt(400);
                    if (v == 0) C(x, y) = GisImport::Cover::Building;
                    else if (v == 1) C(x, y) = GisImport::Cover::Road;
                }
            break;
        }
    }
    return g;
}

const std::array<std::array<int, 4>, 6> kCases{{
    {200, 150, 1, 1}, {512, 512, 2, 2}, {600, 400, 3, 2},
    {400, 600, 2, 3}, {256, 256, 1, 1}, {300, 300, 2, 2},
}};

void biomeSection() {
    for (std::size_t ci = 0; ci < kCases.size(); ci++) {
        const auto& c = kCases[ci];
        for (int kind = 0; kind <= 4; kind++) {
            const GisImport g = raster(kind, c[0], c[1]);
            const std::vector<std::vector<int>> dist = TreeScatter::distanceToStructure(g);
            for (int cy = 0; cy < c[3]; cy++)
                for (int cx = 0; cx < c[2]; cx++) {
                    std::int64_t t = 0, e = 0, f = 0, d = 0, o = 0;
                    const std::vector<unsigned char> rgb =
                        BiomeMapWriter::cellPixels(g, dist, cx, cy, t, e, f, d, o);
                    std::array<std::int64_t, 256> hist{};
                    hist.fill(0);
                    for (std::size_t i = 0; i < rgb.size(); i += 3) hist[rgb[i]]++;
                    std::string hs;
                    for (const int v : {BiomeMapWriter::TOWN, BiomeMapWriter::FARM_FOREST,
                                        BiomeMapWriter::PH_FOREST, BiomeMapWriter::DEEP_FOREST}) {
                        if (!hs.empty()) hs += ',';
                        hs += s(hist[uz(v)]);
                    }
                    line("BM\t" + s(static_cast<int>(ci)) + "\t" + s(kind) + "\t" + s(cx)
                         + "\t" + s(cy) + "\t" + hex(fnvBytes(rgb)) + "\t" + hs);
                    const std::vector<unsigned char> png = writePngRgb(rgb, 256, 256);
                    line("BMP\t" + s(static_cast<int>(ci)) + "\t" + s(kind) + "\t" + s(cx)
                         + "\t" + s(cy) + "\t" + s(png.size()) + "\t" + hex(fnvBytes(png)));
                }
        }
    }
}

// ------------------------------------------------------------------

void writeSection() {
    int c = 0;
    const std::array<std::array<int, 2>, 3> origins{{{200, 200}, {0, 0}, {17, 993}}};
    for (const auto& cs : kCases)
        for (int kind = 0; kind <= 4; kind += 2)
            for (const auto& origin : origins) {
                const GisImport g = raster(kind, cs[0], cs[1]);
                const fs::path dir = fs::temp_directory_path()
                                     / ("bmo_" + s(c) + "_" + s(::getpid()));
                fs::create_directories(dir);
                std::vector<std::string> log;
                const int n = BiomeMapWriter::write(g, dir, cs[2], cs[3],
                                                    origin[0], origin[1], log);
                std::vector<std::string> names;
                for (const auto& ent : fs::directory_iterator(dir / "maps"))
                    names.push_back(ent.path().filename().string());
                std::sort(names.begin(), names.end());
                std::uint64_t fh = kFnvInit;
                for (const std::string& nm : names) {
                    std::ifstream in(dir / "maps" / nm, std::ios::binary);
                    const std::vector<unsigned char> b(
                        (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                    fh = fnv(fh, static_cast<std::int64_t>(fnvBytes(b)));
                }
                std::string joined;
                for (std::size_t i = 0; i < names.size(); i++) {
                    if (i > 0) joined += ',';
                    joined += names[i];
                }
                line("BMW\t" + s(c) + "\t" + s(n) + "\t" + s(origin[0]) + "_"
                     + s(origin[1]) + "\t" + joined + "\t" + hex(fh));
                for (const std::string& l : log) line("BMWLOG\t" + s(c) + "\t" + l);
                fs::remove_all(dir);
                c++;
            }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: pz_biomemap_oracle <out-path>\n");
        return 2;
    }
    vin();
    pngSection();
    biomeSection();
    writeSection();

    std::string blob;
    for (const std::string& l : g_out) { blob += l; blob += '\n'; }
    std::ofstream f(argv[1], std::ios::binary);
    if (!f) { std::fprintf(stderr, "cannot write %s\n", argv[1]); return 2; }
    f.write(blob.data(), static_cast<std::streamsize>(blob.size()));
    f.close();
    std::printf("BiomeMapOracle c++ : %zu lines, %zu bytes -> %s\n",
                g_out.size(), blob.size(), argv[1]);
    return 0;
}

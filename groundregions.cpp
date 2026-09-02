// groundregions.cpp — port of GroundRegions.java. See groundregions.hpp for
// the five port notes; the ones that bite are hash01's uint64_t arithmetic
// (PORT NOTE 1) and DX/DY's ordering (PORT NOTE 4).

#include "groundregions.hpp"

#include <cstddef>
#include <deque>
#include <limits>
#include <string>

namespace pzformat {

namespace {

inline std::size_t uz(int v) { return static_cast<std::size_t>(v); }

} // namespace

GroundRegions::Grid GroundRegions::build(const GisImport& g, int ox, int oy,
                                         std::int64_t seed) {
    // Work over a margin so distances are correct right to the cell edge, then
    // hand back the cell plus a one-square border. Squares in the border belong
    // to the neighbouring cell; because the dither is a position hash rather
    // than a sequential draw, that cell computes the same material for them and
    // the masks either side of a cell boundary agree.
    const int m = MARGIN;
    const int n = 256 + 2 * m;

    const std::vector<std::vector<int>> dB =
        coverDistance(g, ox, oy, m, GisImport::Cover::Building);
    const std::vector<std::vector<int>> dR =
        coverDistance(g, ox, oy, m, GisImport::Cover::Road);

    const std::array<GroundMaterial, 14>& mats = GroundMaterial::values();
    const Cell kGrassDark = &mats[0];    // GRASS_DARK
    const Cell kGrassMedium = &mats[1];  // GRASS_MEDIUM
    const Cell kSand = &mats[3];         // SAND
    const Cell kRoad01 = &mats[7];       // ROAD_01

    Grid wide(uz(n), std::vector<Cell>(uz(n), nullptr));
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            const int gx = ox - m + i, gy = oy - m + j;
            const GisImport::Cover c = coverAt(g, gx, gy);
            if (c == GisImport::Cover::Building)
                continue;              // nullptr: interior, never blends
            if (c == GisImport::Cover::Road) {
                // The road joins the array so neighbouring grass can mask onto
                // it (§27: Grass_Dark > Road_04 at n=284,583). GisCells still
                // writes the road tile itself; this is only for the neighbour
                // rule. Must match the tile GisCells writes —
                // blends_street_01_0 is block 0, i.e. Road_01.
                wide[uz(i)][uz(j)] = kRoad01;
                continue;
            }
            // NOTE: Cover::Water falls through to here and becomes ground.
            // That is what the Java does. WATER was added to the enum on
            // 2026-08-21, after this dispatch was written, and no branch was
            // added for it. Reproduced, not corrected.
            if (dB[uz(i)][uz(j)] <= YARD) wide[uz(i)][uz(j)] = kSand;
            else if (dR[uz(i)][uz(j)] <= VERGE) wide[uz(i)][uz(j)] = kGrassMedium;
            else wide[uz(i)][uz(j)] = kGrassDark;
        }

    dither(wide, ox - m, oy - m, seed);

    Grid out(258, std::vector<Cell>(258, nullptr));
    for (int x = 0; x < 258; x++)
        for (int k = 0; k < 258; k++)
            out[uz(x)][uz(k)] = wide[uz(m - 1 + x)][uz(m - 1 + k)];
    return out;
}

void GroundRegions::addMasks(std::vector<std::int32_t>& stack, CellData& cell,
                             const Grid& region, int x, int y,
                             Cell self, JavaRandom& rng) {
    if (self == nullptr) return;

    // EnumMap<GroundMaterial, EnumSet<Dir>>: an array indexed by ordinal,
    // iterated 0..13. See PORT NOTE 3.
    // DirSet is immutable (it is a verified unit and is not being edited here),
    // so the EnumSet is accumulated as its bit pattern and wrapped at the end.
    // fromBits + ord reproduces EnumSet.add exactly: EnumSet is itself a bit
    // vector indexed by ordinal.
    std::array<std::uint8_t, 14> bits{};
    std::array<bool, 14> present{};
    bits.fill(0);
    present.fill(false);

    for (const MaskRule::Dir d : MaskRule::kDirs) {
        const Cell other = region[uz(x + 1 + MaskRule::dx(d))][uz(y + 1 + MaskRule::dy(d))];
        if (other == nullptr || !other->outranks(self)) continue;
        const std::size_t o = uz(other->ordinal);
        present[o] = true;
        bits[o] = static_cast<std::uint8_t>(bits[o] | (1u << MaskRule::ord(d)));
    }

    // 2 variant sets: blends_natural_01 carries B+8..11 and B+12..15.
    // blends_street_01 has only one and would take 1 here (§27).
    const std::array<GroundMaterial, 14>& mats = GroundMaterial::values();
    for (std::size_t o = 0; o < 14; o++) {
        if (!present[o]) continue;
        const GroundMaterial& key = mats[o];
        const MaskRule::DirSet dirs = MaskRule::DirSet::fromBits(bits[o]);
        for (const int idx : MaskRule::masks(key.block, dirs, key.variantSets, rng))
            stack.push_back(cell.tileIndex(std::string(key.sheet) + std::to_string(idx)));
    }
}

void GroundRegions::dither(Grid& mat, int ox, int oy, std::int64_t seed) {
    const int n = static_cast<int>(mat.size());
    Grid src(mat);

    Grid across(uz(n), std::vector<Cell>(uz(n), nullptr));
    const std::vector<std::vector<int>> dist = edgeDistance(src, across);

    for (int x = 0; x < n; x++)
        for (int y = 0; y < n; y++) {
            if (src[uz(x)][uz(y)] == nullptr) continue;
            const int d = dist[uz(x)][uz(y)];
            if (d < 0 || d >= static_cast<int>(P.size())) continue;
            if (hash01(ox + x, oy + y, seed) >= P[uz(d)]) continue;
            const Cell other = across[uz(x)][uz(y)];
            if (other == nullptr || other == src[uz(x)][uz(y)]) continue;
            // Never dither across a road boundary. Roads are in the array so
            // grass can MASK onto them; interleaving them would put grass
            // squares in the carriageway and road squares in the field. A road
            // edge is a hard edge, softened by masks only.
            if (isRoad(other) || isRoad(src[uz(x)][uz(y)])) continue;
            mat[uz(x)][uz(y)] = other;
        }
}

std::vector<std::vector<int>> GroundRegions::edgeDistance(const Grid& m, Grid& across) {
    const int n = static_cast<int>(m.size());
    across.assign(uz(n), std::vector<Cell>(uz(n), nullptr));  // PORT NOTE 5

    std::vector<std::vector<int>> d(uz(n), std::vector<int>(uz(n), -1));
    std::deque<std::array<int, 2>> q;

    for (int x = 0; x < n; x++)
        for (int y = 0; y < n; y++) {
            if (m[uz(x)][uz(y)] == nullptr) continue;
            for (int k = 0; k < 4; k++) {
                const int nx = x + DX[uz(k)], ny = y + DY[uz(k)];
                if (nx < 0 || ny < 0 || nx >= n || ny >= n) continue;
                if (m[uz(nx)][uz(ny)] != nullptr && m[uz(nx)][uz(ny)] != m[uz(x)][uz(y)]) {
                    d[uz(x)][uz(y)] = 0;
                    across[uz(x)][uz(y)] = m[uz(nx)][uz(ny)];
                    q.push_back({x, y});
                    break;   // first differing neighbour in DX/DY order wins
                }
            }
        }

    while (!q.empty()) {
        const std::array<int, 2> p = q.front();
        q.pop_front();
        for (int k = 0; k < 4; k++) {
            const int nx = p[0] + DX[uz(k)], ny = p[1] + DY[uz(k)];
            if (nx < 0 || ny < 0 || nx >= n || ny >= n) continue;
            if (m[uz(nx)][uz(ny)] == nullptr || d[uz(nx)][uz(ny)] >= 0) continue;
            d[uz(nx)][uz(ny)] = d[uz(p[0])][uz(p[1])] + 1;
            across[uz(nx)][uz(ny)] = across[uz(p[0])][uz(p[1])];
            q.push_back({nx, ny});
        }
    }
    return d;
}

std::vector<std::vector<int>> GroundRegions::coverDistance(const GisImport& g,
                                                           int ox, int oy, int margin,
                                                           GisImport::Cover target) {
    const int n = 256 + 2 * margin;
    const int kMax = std::numeric_limits<int>::max();   // Java Integer.MAX_VALUE

    std::vector<std::vector<int>> d(uz(n), std::vector<int>(uz(n), kMax));
    std::deque<std::array<int, 2>> q;

    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            if (coverAt(g, ox - margin + i, oy - margin + j) == target) {
                d[uz(i)][uz(j)] = 0;
                q.push_back({i, j});
            }

    while (!q.empty()) {
        const std::array<int, 2> p = q.front();
        q.pop_front();
        for (int k = 0; k < 4; k++) {
            const int ni = p[0] + DX[uz(k)], nj = p[1] + DY[uz(k)];
            if (ni < 0 || nj < 0 || ni >= n || nj >= n) continue;
            if (d[uz(ni)][uz(nj)] != kMax) continue;
            d[uz(ni)][uz(nj)] = d[uz(p[0])][uz(p[1])] + 1;
            q.push_back({ni, nj});
        }
    }
    return d;
}

GisImport::Cover GroundRegions::coverAt(const GisImport& g, int gx, int gy) {
    if (gx < 0 || gy < 0 || gx >= g.width || gy >= g.height)
        return GisImport::Cover::None;
    return g.cover[uz(gx)][uz(gy)];
}

double GroundRegions::hash01(int gx, int gy, std::int64_t seed) {
    // PORT NOTE 1. Every step below is uint64_t on purpose. Java computes this
    // in `long` where the multiplies overflow and the constants are negative;
    // transcribing that into int64_t is undefined behaviour and -O2 is entitled
    // to exploit it, which is precisely the bug FINDINGS F5 found in nextInt.
    // The `(long) gx` sign-extension is reproduced by the int64_t cast first.
    std::uint64_t h = static_cast<std::uint64_t>(seed);
    h ^= static_cast<std::uint64_t>(static_cast<std::int64_t>(gx)) * 0x9E3779B97F4A7C15ULL;
    h ^= static_cast<std::uint64_t>(static_cast<std::int64_t>(gy)) * 0xC2B2AE3D27D4EB4FULL;
    h ^= h >> 33; h *= 0xFF51AFD7ED558CCDULL;   // Java >>> is logical
    h ^= h >> 33; h *= 0xC4CEB9FE1A85EC53ULL;
    h ^= h >> 33;
    // (h >>> 11) has its top 11 bits clear, so it is below 2^53 and converts to
    // double exactly — the same identity java.util.Random.nextDouble relies on.
    return static_cast<double>(h >> 11) * 0x1.0p-53;
}

bool GroundRegions::isRoad(Cell m) {
    return m != nullptr && m->sheet == GroundMaterial::STREET;
}

} // namespace pzformat

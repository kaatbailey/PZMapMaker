// treescatter.cpp — port of TreeScatter.java. See the header for the six port
// notes; the ones that bite are NOTE 1 (draw count) and NOTE 6 (x*h+y).

#include "treescatter.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>

namespace pzformat {

namespace {
inline std::size_t uz(int v) { return static_cast<std::size_t>(v); }
} // namespace

const std::array<TreeScatter::Band, 4>& TreeScatter::bands() {
    // Bands by BFS distance in tiles from any building or road square:
    //   0 .. CLEAR   nothing. Yards and road verges stay open.
    //   .. 9         sparse saplings. Planted, kept small.
    //   .. 22        regrowth.
    //   .. 45        woodland.
    //   beyond       dense.
    static const std::array<Band, 4> kBands{{
        {9, 1, 0.020, "roadside"},
        {22, 2, 0.035, "regrowth"},
        {45, 2, 0.070, "woodland"},
        {std::numeric_limits<int>::max(), 2, 0.080, "dense"},
    }};
    return kBands;
}

TreeScatter::Placement TreeScatter::place(const GisImport& g, const TreePalette& tp,
                                          std::int64_t seed,
                                          std::vector<std::string>& log) {
    const int w = g.width, h = g.height;
    Placement out(uz(w), std::vector<std::string>(uz(h)));

    if (!tp.usable()) {
        log.push_back("trees: no usable tree tiles; skipping scatter");
        return out;
    }

    const std::vector<std::vector<int>> dist = distanceToStructure(g);
    JavaRandom rng(seed);
    std::vector<std::vector<bool>> taken(uz(w), std::vector<bool>(uz(h), false));

    const std::array<Band, 4>& kBands = bands();
    std::array<std::int64_t, 4> perBand{};
    perBand.fill(0);
    std::int64_t stumps = 0;

    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            if (isStructure(g, x, y)) continue;
            const int d = dist[uz(x)][uz(y)];
            if (d <= CLEAR) continue;

            const int bi = bandFor(d);
            const Band& band = kBands[uz(bi)];

            // A stump is a tree that used to be here, so it competes for the
            // same square and respects the same spacing.
            //
            // PORT NOTE 1: the && short-circuits. With no stump tile this draw
            // does not happen AT ALL, for any square, and the whole stream
            // shifts. Do not hoist the nextDouble out of the condition.
            if (tp.hasStump && rng.nextDouble() < P_STUMP) {
                if (!tooClose(taken, x, y, w, h)) {
                    out[uz(x)][uz(y)] = TreePalette::STUMP;
                    taken[uz(x)][uz(y)] = true;
                    stumps++;
                }
                // `continue` either way: a stump roll that wins but lands too
                // close leaves the square EMPTY rather than falling through to
                // the tree roll. Observable, and it costs one draw.
                continue;
            }

            if (rng.nextDouble() >= band.density) continue;
            if (tooClose(taken, x, y, w, h)) continue;

            const std::vector<std::string>* variants = tp.tilesNear(band.size);
            if (variants == nullptr || variants->empty()) continue;

            // PORT NOTE 2: bound is the palette size, 8 in production — a power
            // of two, so the rejection loop is unreachable from a real run.
            out[uz(x)][uz(y)] =
                (*variants)[uz(rng.nextInt(static_cast<std::int32_t>(variants->size())))];
            taken[uz(x)][uz(y)] = true;
            perBand[uz(bi)]++;
        }
    }

    std::int64_t total = 0;
    for (const std::int64_t n : perBand) total += n;

    // Text is compared by the step 7 oracle, so the formatting is part of the
    // contract, down to Java's "%-10s size %d  %6d%n".
    log.push_back("trees: " + std::to_string(total) + " placed, "
                  + std::to_string(stumps) + " stumps");
    for (std::size_t i = 0; i < kBands.size(); i++) {
        char line[128];
        std::snprintf(line, sizeof(line), "   %-10s size %d  %6lld",
                      kBands[i].label, kBands[i].size,
                      static_cast<long long>(perBand[i]));
        log.push_back(line);
    }
    log.push_back("   (species and mature size are chosen by the engine"
                  " at runtime; the renderer cannot preview these)");
    return out;
}

int TreeScatter::bandFor(int d) {
    const std::array<Band, 4>& kBands = bands();
    for (std::size_t i = 0; i < kBands.size(); i++)
        if (d <= kBands[i].maxDist) return static_cast<int>(i);
    return static_cast<int>(kBands.size()) - 1;
}

std::vector<std::vector<int>> TreeScatter::distanceToStructure(const GisImport& g) {
    const int w = g.width, h = g.height;
    const int kMax = std::numeric_limits<int>::max();   // Java Integer.MAX_VALUE

    std::vector<std::vector<int>> dist(uz(w), std::vector<int>(uz(h), kMax));

    // PORT NOTE 4: w*h is exactly sufficient, not a guess.
    std::vector<int> queue;
    queue.reserve(uz(w) * uz(h));
    std::size_t head = 0;

    for (int x = 0; x < w; x++)
        for (int y = 0; y < h; y++) {
            if (isStructure(g, x, y)) {
                dist[uz(x)][uz(y)] = 0;
                queue.push_back(x * h + y);        // PORT NOTE 6: x*h+y, not x*w+y
            } else {
                dist[uz(x)][uz(y)] = kMax;
            }
        }

    // No structure anywhere: everything is the densest band.
    if (queue.empty()) return dist;

    // PORT NOTE 3: E, W, S, N. Not MaskRule's order, not GroundRegions'.
    static constexpr std::array<int, 4> dx{1, -1, 0, 0};
    static constexpr std::array<int, 4> dy{0, 0, 1, -1};

    while (head < queue.size()) {
        const int cur = queue[head++];
        const int x = cur / h, y = cur % h;
        const int nd = dist[uz(x)][uz(y)] + 1;      // PORT NOTE 5: cannot overflow
        for (std::size_t k = 0; k < 4; k++) {
            const int nx = x + dx[k], ny = y + dy[k];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            if (dist[uz(nx)][uz(ny)] > nd) {
                dist[uz(nx)][uz(ny)] = nd;
                queue.push_back(nx * h + ny);
            }
        }
    }
    return dist;
}

bool TreeScatter::isStructure(const GisImport& g, int x, int y) {
    return g.cover[uz(x)][uz(y)] == GisImport::Cover::Building
           || g.cover[uz(x)][uz(y)] == GisImport::Cover::Road
           || g.northWall[uz(x)][uz(y)] || g.westWall[uz(x)][uz(y)];
}

bool TreeScatter::tooClose(const std::vector<std::vector<bool>>& taken,
                           int x, int y, int w, int h) {
    const int x0 = std::max(0, x - SPACING), x1 = std::min(w - 1, x + SPACING);
    const int y0 = std::max(0, y - SPACING), y1 = std::min(h - 1, y + SPACING);
    for (int i = x0; i <= x1; i++)
        for (int j = y0; j <= y1; j++)
            if (taken[uz(i)][uz(j)]) return true;
    return false;
}

} // namespace pzformat

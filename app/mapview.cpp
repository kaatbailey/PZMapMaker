#include "mapview.hpp"

#include "celldata.hpp"

#include <QOpenGLFunctions>

#include <cstdio>

namespace pzmm {

namespace {
// The harness priced the bounding sprite of an iso tile. PZ iso tiles render as
// ~64x32 diamonds; the sprite quad that carries them is 64x32 = 2048px. We use
// the same unit so this census is directly comparable to the harness numbers
// (which used square sprites: 18px -> 324 frag). Half the diamond bbox is
// transparent, so true drawn fragments are ~half; we report the bounding figure
// (conservative, matches harness accounting) and note the ~0.5 factor in prose.
constexpr double kSpriteFragments = 64.0 * 32.0;   // 2048, bounding quad
} // namespace

MapView::MapView(QWindow* parent) : QOpenGLWindow(NoPartialUpdate, parent) {}
MapView::~MapView() = default;

CellCensus MapView::censusOf(const pzformat::CellData& cell) {
    CellCensus c;
    c.minLevel = cell.minLevel();
    c.maxLevel = cell.maxLevel();
    const int n = cell.cellSize();

    // Match CellData's own traversal (see encodeChunk / nonEmptySquares):
    // actual z runs minLevel..maxLevel; each non-empty square holds a vector of
    // tile indices, and every tile index is one sprite that would be drawn.
    for (int z = cell.minLevel(); z <= cell.maxLevel(); ++z) {
        for (int x = 0; x < n; ++x) {
            for (int y = 0; y < n; ++y) {
                const auto tiles = cell.tilesAt(x, y, z);
                if (tiles.empty()) continue;
                ++c.squares;
                c.instances += static_cast<long>(tiles.size());
            }
        }
    }
    c.fragmentsAt1to1 = double(c.instances) * kSpriteFragments;
    return c;
}

void MapView::setCell(const pzformat::CellData& cell) {
    census_ = censusOf(cell);
    haveCell_ = true;
    emit censusReady(census_);

    // Also print to stdout — this is a measurement, and the terminal is where
    // the harness numbers live, so keep them in the same place.
    std::printf(
        "[MapView census] instances=%ld  squares=%ld  levels=%d..%d\n"
        "  fragments@1:1 = %.1fM (bounding 64x32 quad; ~0.5x drawn after diamond alpha)\n",
        census_.instances, census_.squares, census_.minLevel, census_.maxLevel,
        census_.fragmentsAt1to1 / 1e6);
    std::fflush(stdout);

    if (isExposed()) update();
}

void MapView::clearCell() {
    census_ = CellCensus{};
    haveCell_ = false;
    if (isExposed()) update();
}

void MapView::initializeGL() {
    auto* f = QOpenGLWindow::context()->functions();
    f->glClearColor(0.09f, 0.09f, 0.11f, 1.0f);   // near-black slate
    // No depth test yet: nothing is drawn. The opaque pre-pass (next step) will
    // enable depth write for opaque tiles; translucent pass will disable it.
}

void MapView::resizeGL(int w, int h) {
    auto* f = QOpenGLWindow::context()->functions();
    f->glViewport(0, 0, w, h);
}

void MapView::paintGL() {
    auto* f = QOpenGLWindow::context()->functions();
    f->glClear(GL_COLOR_BUFFER_BIT);
    // Step 1 draws nothing. The cleared surface on Garuda/Wayland is the proof
    // the QOpenGLWindow context path is live. Sprites arrive in the next step,
    // after the census tells us what overdraw the pre-pass must handle.
}

} // namespace pzmm

// MapView — the C3 interactive viewport, step 1: GL shell + overdraw census.
//
// A QOpenGLWindow (NOT QOpenGLWidget) hosted via QWidget::createWindowContainer.
// C1_ARCHITECTURE §1.2 line 60: QOpenGLWidget on Wayland introduces a copy path
// that kills frame time; QOpenGLWindow avoids it. (That copy-path cost is itself
// unmeasured — we take the safe path rather than verify it, since the safe path
// costs nothing. If QOpenGLWidget is ever wanted, measure the copy cost first.)
//
// WHAT THIS STEP DOES
//   - Brings up a 4.5 core context and clears to a background colour. Proves the
//     GL surface is live on Garuda/KDE/Wayland before any rendering is built.
//   - When a cell is set, walks its CellData and computes the ONE number the
//     500k-instance harness could not supply: the real instance count and
//     on-screen fragment footprint of a dense cell at 1:1 zoom. Printed, not
//     drawn. (harness/FINDINGS_harness_2026-08-22.md: "First C3 measurement:
//     decode one real dense cell to instances and read its on-screen fragment
//     count." That gates the opaque-pre-pass design, so it comes before sprites.)
//
// WHAT THIS STEP DOES NOT DO
//   No atlas, no textures, no sprite drawing, no pan/zoom. Those follow once the
//   overdraw number is known and the pre-pass strategy is chosen against it.
#pragma once

#include <QOpenGLWindow>

#include <cstdint>

namespace pzformat { class CellData; }

namespace pzmm {

// Result of the dense-cell instance census — the C3 step-1 measurement.
struct CellCensus {
    long instances = 0;        // one instance per (square, tile) that would draw
    long squares = 0;          // non-empty squares walked
    int  minLevel = 0;
    int  maxLevel = 0;
    // Fragment footprint at 1:1. Iso tiles are ~64x32 diamonds; we price the
    // bounding sprite (the harness's unit) so the number is comparable to it.
    double fragmentsAt1to1 = 0.0;   // instances * spriteFragments
    double overdrawAtView(int viewW, int viewH) const {
        const double surface = double(viewW) * double(viewH);
        return surface > 0 ? fragmentsAt1to1 / surface : 0.0;
    }
};

class MapView : public QOpenGLWindow {
    Q_OBJECT
public:
    explicit MapView(QWindow* parent = nullptr);
    ~MapView() override;

    // Hand the viewport a loaded cell. Non-owning: MapView reads it during the
    // census and does not retain it. Runs the census immediately and emits
    // censusReady. Rendering (later steps) will retain a reference instead.
    void setCell(const pzformat::CellData& cell);
    void clearCell();

    const CellCensus& lastCensus() const noexcept { return census_; }

signals:
    void censusReady(const CellCensus& c);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    // Walk the cell and fill census_. Pure CPU; no GL. Static so it is unit-
    // testable off-window if we ever want to (takes only CellData).
    static CellCensus censusOf(const pzformat::CellData& cell);

    CellCensus census_;
    bool haveCell_ = false;
};

} // namespace pzmm

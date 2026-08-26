// MapView — the C3 interactive viewport.
//
// QOpenGLWidget base. We first tried QOpenGLWindow-in-createWindowContainer per
// C1 §1.2's note that QOpenGLWidget has a Wayland copy-path cost — but on this
// Garuda/KDE/Wayland setup that path rendered nothing visible: draws executed
// (timed, no GL errors, geometry on-screen) but the container's native surface
// and the GL context's surface diverged, so nothing was ever presented. Readback
// of the draw target returned all-zero including the clear colour. QOpenGLWidget
// presents correctly here; the copy-path cost it warns about is measured, not
// assumed — see STATE. If that cost ever bites, revisit with real numbers.
//
// STEP 1 (done): GL shell live on Wayland + dense-cell instance census.
// STEP 2 (this): textured instanced draw with the opaque pre-pass built in,
//   plus per-pass GPU timing. This replaces the census's bounding-quad estimate
//   with a MEASURED fragment cost on a real cell — the number STATE flagged as
//   +/-2x until real tiles are drawn.
//
// The two-pass structure is the finding from the harness + census made real:
// the bound is overdraw, so the opaque floor layer is drawn first front-to-back
// with depth write (early-Z rejects occluded fragments), and only translucent
// tiles take the blended back-to-front pass. Placeholder textures for now
// (1x1 solid tint per tile name); the real atlas is a later step. The draw-call
// structure and the GPU timing are real regardless of texture content.
//
// NOT in this step: pan/zoom (camera is fixed, cell drawn at 1:1 from origin),
// the real atlas, picking/editing. Those follow.
#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_4_5_Core>
#include <QPoint>
#include <QStringList>
#include <QPair>
#include <QVector>

#include "spriteatlas.hpp"

#include <cstdint>
#include <vector>

namespace pzformat { class CellData; }

namespace pzmm {

// Result of the dense-cell instance census — the C3 step-1 measurement.
struct CellCensus {
    long instances = 0;
    long squares = 0;
    int  minLevel = 0;
    int  maxLevel = 0;
    double fragmentsAt1to1 = 0.0;
    double overdrawAtView(int viewW, int viewH) const {
        const double surface = double(viewW) * double(viewH);
        return surface > 0 ? fragmentsAt1to1 / surface : 0.0;
    }
};

// Measured GPU time of the two passes, in milliseconds. Filled each frame a
// cell is drawn; this is the step-2 measurement that supersedes the census
// estimate. -1 means "not yet timed".
struct PassTiming {
    double opaqueMs = -1.0;      // pass 1: floors, depth write, no blend
    double translucentMs = -1.0; // pass 2: everything else, blended
    long   opaqueInstances = 0;
    long   translucentInstances = 0;
    double totalMs() const { return opaqueMs + translucentMs; }
};

class MapView : public QOpenGLWidget, protected QOpenGLFunctions_4_5_Core {
    Q_OBJECT
public:
    explicit MapView(QWidget* parent = nullptr);
    ~MapView() override;

    // Hand the viewport a loaded cell. MapView copies out the instance data it
    // needs (position, layer, opacity) and does not retain the CellData. Safe to
    // call before or after GL init; GL upload is deferred to the next paint.
    void setCell(const pzformat::CellData& cell);
    // Rebuild instances from edited cell data WITHOUT resetting camera or level.
    // Used after an in-place edit (setFloor etc.) so the view stays put.
    void refreshCell(const pzformat::CellData& cell);
    // Clear the persistent selection diamond (call when a new cell loads).
    void clearSelection() { selectedTile_ = {-1,-1}; update(); }
    void clearCell();

    // Hand the viewport real sprite pixels for the current cell, in tileNames
    // order (layers[i] is the sprite for tile-name index i; found=false means
    // no atlas sprite — drawn as a flagged placeholder). Triggers a re-upload
    // of the GL atlas on the next paint. Call after setCell.
    void setSprites(std::vector<SpriteAtlas::Layer> layers);

    // Level selector (Model B): show all tiles with z <= maxLevel. Clamped to
    // the loaded cell's [minLevel, maxLevel]. Emits maxLevelChanged so a UI
    // control can stay in sync when keys change it.
    void setMaxLevel(int z);
    int  maxLevel() const noexcept { return maxLevel_; }
    int  cellMinLevel() const noexcept { return census_.minLevel; }
    int  cellMaxLevel() const noexcept { return census_.maxLevel; }

    const CellCensus& lastCensus() const noexcept { return census_; }
    const PassTiming& lastTiming() const noexcept { return timing_; }

    // Stamp brush: load a tile name as the active brush (footprint preview
    // + left-click/drag to paint). Empty string clears the brush.
    void setBrush(const QString& tileName, const SpriteAtlas* atlas = nullptr);
    void clearBrush();
    // True when a brush is loaded.
    bool hasBrush() const noexcept { return !brushName_.isEmpty(); }

signals:
    void censusReady(const CellCensus& c);
    void timingReady(const PassTiming& t);
    void maxLevelChanged(int z);
    // Emitted on a click (not drag) when a cell is loaded.
    // tx/ty are cell-local tile coordinates; names are all tile names
    // stacked at that square for z <= maxLevel_.
    // Each entry is (z_level, tile_name), in painter's order (low z first).
    void tileClicked(int tx, int ty, QVector<QPair<int,QString>> tiles);
    // Emitted when the user right-clicks a tile that has a floor — the floor
    // tile name is picked up as the active stamp brush. z is the game-coordinate
    // level the tile was found at, so the spinbox can be synced to paint there.
    void floorPickedUp(const QString& tileName, int z);
    // Emitted when the brush should paint a tile at (tx, ty) at the current
    // working z. Connected in MainWindow to the setFloor + refresh logic.
    void paintTile(int tx, int ty);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    // Interactive camera (step 3).
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    // One instance per (square, tile). Built on the CPU from CellData, uploaded
    // to the GPU as the per-instance vertex stream. 16 bytes, tightly packed.
    struct SpriteInstance {
        float         wx, wy;     // world tile position (pre-projection)
        std::uint32_t layer;      // atlas array layer = tile-name index
        std::uint32_t packed;     // bit0: opaque(floor). bits8..: actual z+128
    };

    static CellCensus censusOf(const pzformat::CellData& cell);

    // Build the instance list + placeholder atlas layers from a cell. Splits
    // opaque (floor) from translucent so the two passes can draw contiguous
    // ranges. Pure CPU; no GL.
    void buildInstances(const pzformat::CellData& cell);

    // GL helpers.
    void ensureProgram();
    void uploadInstances();          // (re)fill the instance VBO + atlas
    void makePlaceholderAtlas(int layers);
    void uploadSpriteAtlas();        // upload real sprite pixels + per-layer meta
    double timedDraw(GLuint query, long first, long count, bool opaquePass);

    // Camera: compute the fit-to-window zoom+pan that frames the whole cell.
    // Called on setCell and resize so a fresh cell always starts framed.
    void fitToWindow();
    void resetView1to1();

    // C4 picking: convert a screen pixel (widget coords) to a cell-local tile
    // coordinate, using the inverse of the PZ iso transform. Returns (-1,-1) if
    // outside the cell or no cell loaded.
    QPoint screenToTile(float cx, float cy) const;
    // Brush placement helper: convert the tile under the cursor to the origin
    // of the visible green footprint box. Paint and overlay must both use this,
    // or multi-tile brushes drift apart from their preview.
    QPoint brushBoxOriginForCursor(const QPoint& cursorTile) const;
    void ensureOverlayProgram();
    void drawHoverOverlay();

    // --- CPU-side state ---
    CellCensus census_;
    PassTiming timing_;
    std::vector<SpriteInstance> opaque_;      // pass 1 instances (floors)
    std::vector<SpriteInstance> translucent_; // pass 2 instances (rest)
    std::vector<SpriteAtlas::Layer> sprites_; // real sprite pixels, tileNames order
    bool  haveSprites_ = false;               // sprites_ set; upload on next paint
    bool  spritesDirty_ = false;
    int   atlasW_ = 1, atlasH_ = 1;           // GL atlas array layer dimensions
    int   atlasLayers_ = 0;
    int   usableLayers_ = 0;                  // min(atlasLayers_, GL array cap)
    bool  haveCell_ = false;
    bool  dirtyUpload_ = false;               // instance data changed, re-upload
    bool  glReady_ = false;                   // 4.5 core functions resolved

    // C4: retained cell data for picking.
    // squareTiles_[z_slot][x][y] = tile-index list for that square.
    // z_slot = z - census_.minLevel. Populated in buildInstances.
    // cellTileNames_ mirrors cell.header().tileNames so we can resolve names
    // without retaining the CellData pointer.
    int cellMinZ_ = 0;
    int squareDim_ = 256;   // cell.cellSize() (same as cellSize_; kept for clarity)
    // Flat storage: squareTiles_[z_slot * squareDim_ * squareDim_ + x * squareDim_ + y]
    std::vector<std::vector<std::int32_t>> squareTiles_;
    std::vector<std::string> cellTileNames_;
    // Press position for drag-vs-click detection (C4).
    int pressX_ = 0, pressY_ = 0;
    // C4 hover: tile under the cursor, or (-1,-1) when outside the cell.
    QPoint hoverTile_    = {-1, -1};
    // C4 selection: last clicked tile, persists until another tile is clicked.
    QPoint selectedTile_ = {-1, -1};

    // --- GL overlay (hover diamond) ---
    GLuint overlayProg_ = 0;
    GLuint overlayVao_  = 0;
    GLuint overlayVbo_  = 0;    // 4 × vec2 screen-space NDC vertices, updated each frame
    GLint  uoColor_     = -1;   // vec4 uniform
    GLint  uoSurface_   = -1;   // vec2 surface size (same as uSurface_ but own program)

    // --- Stamp brush state ---
    // brushName_ non-empty = paint mode.  brushW_/brushD_ = footprint in tiles
    // (fx/64 wide, fy/128 deep from the sprite metadata).
    QString brushName_;
    int     brushW_ = 1, brushD_ = 1;
    // Last square painted during a stroke (avoids redundant writes on same tile).
    QPoint  lastPainted_ = {-1, -1};

    // --- Camera state (step 3). zoom_ multiplies the base tile size: 1.0 == 1:1
    // (tiles at full 64x32). pan_ is a pixel offset added to the projection
    // origin. Driven by: wheel (zoom-at-cursor), middle-drag, Alt+left-drag.
    float zoom_ = 1.0f;
    float panX_ = 0.0f, panY_ = 0.0f;
    bool  dragging_    = false;   // left-button drag (inspect or paint stroke)
    bool  midDragging_ = false;   // middle-button drag (always pan)
    bool  altHeld_     = false;   // Alt key currently held
    bool  needsFit_ = false;                  // frame the cell on next paint
    int   lastMouseX_ = 0, lastMouseY_ = 0;
    int   maxLevel_ = 7;                       // level selector: show z <= this

    // --- GL-side state (0 until initializeGL) ---
    GLuint prog_ = 0;
    GLuint vao_ = 0;
    GLuint quadVbo_ = 0, quadEbo_ = 0;
    GLuint instVbo_ = 0;                       // opaque_ then translucent_, packed
    GLuint atlas_ = 0;
    GLuint layerMeta_ = 0;   // RGBA32F texture: per-layer (uvW,uvH,ox,oy)
    GLuint queryOpaque_ = 0, queryTranslucent_ = 0;
    GLint  uSurface_ = -1, uTileSize_ = -1, uOpaquePass_ = -1;
    GLint  uScale_ = -1, uOrigin_ = -1;
    GLint  uLayerMeta_ = -1, uAtlasDims_ = -1;
    GLint  uMaxLevel_ = -1;
    GLint  uLayerCount_ = -1;
    int    viewW_ = 1, viewH_ = 1;
    int    cellSize_ = 256;   // tiles per side of the loaded cell, for fit math
};

} // namespace pzmm

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
    void clearCell();

    // Hand the viewport real sprite pixels for the current cell, in tileNames
    // order (layers[i] is the sprite for tile-name index i; found=false means
    // no atlas sprite — drawn as a flagged placeholder). Triggers a re-upload
    // of the GL atlas on the next paint. Call after setCell.
    void setSprites(std::vector<SpriteAtlas::Layer> layers);

    const CellCensus& lastCensus() const noexcept { return census_; }
    const PassTiming& lastTiming() const noexcept { return timing_; }

signals:
    void censusReady(const CellCensus& c);
    void timingReady(const PassTiming& t);

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
    bool  haveCell_ = false;
    bool  dirtyUpload_ = false;               // instance data changed, re-upload
    bool  glReady_ = false;                   // 4.5 core functions resolved

    // --- Camera state (step 3). zoom_ multiplies the base tile size: 1.0 == 1:1
    // (tiles at full 64x32). pan_ is a pixel offset added to the projection
    // origin. Driven by wheel (zoom-at-cursor) and left-drag (pan).
    float zoom_ = 1.0f;
    float panX_ = 0.0f, panY_ = 0.0f;
    bool  dragging_ = false;
    bool  needsFit_ = false;                  // frame the cell on next paint
    int   lastMouseX_ = 0, lastMouseY_ = 0;

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
    int    viewW_ = 1, viewH_ = 1;
    int    cellSize_ = 256;   // tiles per side of the loaded cell, for fit math
};

} // namespace pzmm

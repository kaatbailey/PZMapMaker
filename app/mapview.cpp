#include "mapview.hpp"

#include "celldata.hpp"
#include "tileindex.hpp"

#include <QSurfaceFormat>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace pzmm {

namespace {

// Placeholder census sprite unit (kept for continuity with step-1 numbers).
constexpr double kSpriteFragments = 64.0 * 32.0;

// Isometric tile footprint in pixels at 1:1. PZ tiles are 64x32 diamonds.
constexpr float kTileW = 64.0f;
constexpr float kTileH = 32.0f;

// A tile is opaque (floor pass) if its name marks it a floor. We do NOT have a
// TileIndex here necessarily, so use the cheap name test that matches
// TileIndex::kindOf's floor rule without needing the media dir loaded: PZ floor
// tiles live in tilesets whose names begin "floors_" or are the blends_natural
// ground set. This is a conservative approximation: anything we misclassify as
// translucent just goes through the slower blended pass (correct, not faster).
bool looksLikeFloor(const std::string& tileName) {
    return tileName.rfind("floors_", 0) == 0
        || tileName.rfind("blends_natural_", 0) == 0
        || tileName.rfind("blends_street", 0) == 0;
}

// Distinct placeholder colour per atlas layer, from the tile-name hash, so the
// output is visually legible before the real atlas exists.
void tintForLayer(std::uint32_t h, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) {
    h = h * 2654435761u;
    r = static_cast<std::uint8_t>(60 + (h & 0x7F));
    g = static_cast<std::uint8_t>(60 + ((h >> 8) & 0x7F));
    b = static_cast<std::uint8_t>(60 + ((h >> 16) & 0x7F));
}

} // namespace

MapView::MapView(QWidget* parent) : QOpenGLWidget(parent) {
    // Request 4.5 core so QOpenGLFunctions_4_5_Core resolves. QOpenGLWidget
    // honours setFormat if called before the widget is first shown.
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setVersion(4, 5);
    fmt.setDepthBufferSize(24);
    setFormat(fmt);
}
MapView::~MapView() = default;

CellCensus MapView::censusOf(const pzformat::CellData& cell) {
    CellCensus c;
    c.minLevel = cell.minLevel();
    c.maxLevel = cell.maxLevel();
    const int n = cell.cellSize();
    for (int z = cell.minLevel(); z <= cell.maxLevel(); ++z)
        for (int x = 0; x < n; ++x)
            for (int y = 0; y < n; ++y) {
                const auto tiles = cell.tilesAt(x, y, z);
                if (tiles.empty()) continue;
                ++c.squares;
                c.instances += static_cast<long>(tiles.size());
            }
    c.fragmentsAt1to1 = double(c.instances) * kSpriteFragments;
    return c;
}

void MapView::buildInstances(const pzformat::CellData& cell) {
    opaque_.clear();
    translucent_.clear();
    cellSize_ = cell.cellSize();
    const auto& names = cell.header().tileNames;
    atlasLayers_ = static_cast<int>(names.size());
    if (atlasLayers_ < 1) atlasLayers_ = 1;

    const int n = cell.cellSize();
    // Reserve roughly: census already ran, so use its counts if present.
    opaque_.reserve(static_cast<size_t>(census_.instances / 2 + 1));
    translucent_.reserve(static_cast<size_t>(census_.instances / 2 + 1));

    for (int z = cell.minLevel(); z <= cell.maxLevel(); ++z) {
        for (int x = 0; x < n; ++x) {
            for (int y = 0; y < n; ++y) {
                const auto tiles = cell.tilesAt(x, y, z);
                if (tiles.empty()) continue;
                for (std::int32_t ti : tiles) {
                    if (ti < 0 || ti >= static_cast<std::int32_t>(names.size()))
                        continue;
                    const bool floor = looksLikeFloor(names[ti]);
                    SpriteInstance in{};
                    in.wx = static_cast<float>(x);
                    in.wy = static_cast<float>(y);
                    in.layer = static_cast<std::uint32_t>(ti);
                    in.packed = (floor ? 1u : 0u)
                              | (static_cast<std::uint32_t>(z + 128) << 8);
                    (floor ? opaque_ : translucent_).push_back(in);
                }
            }
        }
    }
    dirtyUpload_ = true;
}

void MapView::setCell(const pzformat::CellData& cell) {
    census_ = censusOf(cell);
    buildInstances(cell);
    haveCell_ = true;
    needsFit_ = true;   // frame the new cell on the next paint (dims known then)
    emit censusReady(census_);

    std::printf(
        "[MapView census] instances=%ld  squares=%ld  levels=%d..%d\n"
        "  opaque(floor)=%zu  translucent=%zu  atlasLayers=%d\n",
        census_.instances, census_.squares, census_.minLevel, census_.maxLevel,
        opaque_.size(), translucent_.size(), atlasLayers_);
    std::fflush(stdout);

    if (isValid()) update();
}

void MapView::clearCell() {
    census_ = CellCensus{};
    timing_ = PassTiming{};
    opaque_.clear();
    translucent_.clear();
    haveCell_ = false;
    dirtyUpload_ = true;
    if (isValid()) update();
}

void MapView::ensureProgram() {
    if (prog_) return;

    // Isometric projection in the vertex shader. World tile (wx,wy) plus the
    // per-corner quad offset -> screen pixels. Standard 2:1 iso:
    //   sx = (wx - wy) * tileW/2
    //   sy = (wx + wy) * tileH/2
    // z lifts the tile up by one tile-height per level. No pan/zoom yet: the
    // cell origin is parked near the top-left with a fixed margin.
    const char* vs = R"(#version 450 core
layout(location=0) in vec2 aCorner;   // unit quad 0..1
layout(location=1) in vec2 iWorld;    // tile x,y
layout(location=2) in uint iLayer;
layout(location=3) in uint iPacked;   // bit0 opaque, bits8.. z+128

uniform vec2  uSurface;   // pixels
uniform float uTileSize;  // tileW at 1:1 (64)
uniform float uScale;     // fit-to-window zoom (<1 shrinks the whole cell)
uniform vec2  uOrigin;    // pixel offset to centre the cell in the viewport

flat out uint vLayer;
out vec2 vUV;

void main() {
    float tw = uTileSize * uScale;
    float th = tw * 0.5;
    int zlev = int((iPacked >> 8) & 0xFFu) - 128;

    // Iso placement of this tile's diamond bounding box. corner (0..1) spans one
    // tile's 64x32 box (scaled). Screen-space, y-down.
    vec2 corner = aCorner;
    float px = (iWorld.x - iWorld.y) * (tw * 0.5) + corner.x * tw;
    float py = (iWorld.x + iWorld.y) * (th * 0.5) + corner.y * th
             - float(zlev) * th * 3.0;

    px += uOrigin.x;
    py += uOrigin.y;

    vec2 ndc = vec2(px / uSurface.x, py / uSurface.y) * 2.0 - 1.0;
    ndc.y = -ndc.y;                       // y-down screen -> GL y-up
    // Map depth into a safe interior range. Depth buffer clears to 1.0 and the
    // opaque pass uses GL_LESS, so depths must stay below 1.0 or every fragment
    // is rejected. Higher (x+y+level) = "further back" = larger depth, but kept
    // within [0.2, 0.8] so nothing lands on the clear value or the near plane.
    float order = (float(zlev) * 512.0 + iWorld.x + iWorld.y) / 8192.0; // 0..~1
    float depth = 0.2 + clamp(order, 0.0, 1.0) * 0.6;
    gl_Position = vec4(ndc, depth, 1.0);
    vLayer = iLayer;
    vUV = corner;
}
)";

    const char* fs = R"(#version 450 core
uniform sampler2DArray uAtlas;
uniform int uOpaquePass;      // 1: floors, discard nothing; 0: blended
flat in uint vLayer;
in vec2 vUV;
out vec4 fragColor;
void main() {
    vec4 t = texture(uAtlas, vec3(vUV, float(vLayer)));
    if (uOpaquePass == 1) {
        fragColor = vec4(t.rgb, 1.0);        // opaque, writes depth
    } else {
        fragColor = vec4(t.rgb, 0.85);       // translucent pass
    }
}
)";

    auto compile = [&](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[2048]; glGetShaderInfoLog(s, 2048, nullptr, log);
                   std::fprintf(stderr, "[MapView] shader error:\n%s\n", log); }
        return s;
    };
    GLuint v = compile(GL_VERTEX_SHADER, vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs);
    prog_ = glCreateProgram();
    glAttachShader(prog_, v); glAttachShader(prog_, f);
    glLinkProgram(prog_);
    GLint ok = 0; glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) { char log[2048]; glGetProgramInfoLog(prog_, 2048, nullptr, log);
               std::fprintf(stderr, "[MapView] link error:\n%s\n", log); }
    glDeleteShader(v); glDeleteShader(f);

    uSurface_    = glGetUniformLocation(prog_, "uSurface");
    uTileSize_   = glGetUniformLocation(prog_, "uTileSize");
    uOpaquePass_ = glGetUniformLocation(prog_, "uOpaquePass");
    uScale_      = glGetUniformLocation(prog_, "uScale");
    uOrigin_     = glGetUniformLocation(prog_, "uOrigin");
}

void MapView::makePlaceholderAtlas(int layers) {
    if (layers < 1) layers = 1;
    if (atlas_) { glDeleteTextures(1, &atlas_); atlas_ = 0; }
    glGenTextures(1, &atlas_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, atlas_);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 1, 1, layers, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    std::uint8_t px[4] = {0,0,0,255};
    for (int L = 0; L < layers; ++L) {
        tintForLayer(static_cast<std::uint32_t>(L) * 2654435761u,
                     px[0], px[1], px[2]);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, L, 1, 1, 1,
                        GL_RGBA, GL_UNSIGNED_BYTE, px);
    }
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

void MapView::uploadInstances() {
    // Pack opaque_ then translucent_ contiguously so each pass draws a range.
    const size_t nO = opaque_.size(), nT = translucent_.size();
    const size_t total = nO + nT;
    glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(total * sizeof(SpriteInstance)),
                 nullptr, GL_STATIC_DRAW);
    if (nO) glBufferSubData(GL_ARRAY_BUFFER, 0,
                 static_cast<GLsizeiptr>(nO * sizeof(SpriteInstance)),
                 opaque_.data());
    if (nT) glBufferSubData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(nO * sizeof(SpriteInstance)),
                 static_cast<GLsizeiptr>(nT * sizeof(SpriteInstance)),
                 translucent_.data());

    makePlaceholderAtlas(atlasLayers_);
    dirtyUpload_ = false;
}

double MapView::timedDraw(GLuint query, long first, long count, bool opaquePass) {
    if (count <= 0) return 0.0;
    glUniform1i(uOpaquePass_, opaquePass ? 1 : 0);
    if (opaquePass) {
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
    } else {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
    }

    // Point the instanced attributes at this pass's slice by re-specifying the
    // attribute pointers with a byte offset, then draw with plain
    // glDrawElementsInstanced. This avoids glDrawElementsInstancedBaseInstance,
    // whose function pointer can fail to resolve on some drivers (segfault with
    // no link error). Plain instanced draw is the exact call the C1 §1.2 harness
    // proved works on this machine's NVIDIA driver.
    const GLintptr base = static_cast<GLintptr>(first) * sizeof(SpriteInstance);
    glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteInstance),
                          reinterpret_cast<void*>(base + offsetof(SpriteInstance, wx)));
    glVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, sizeof(SpriteInstance),
                           reinterpret_cast<void*>(base + offsetof(SpriteInstance, layer)));
    glVertexAttribIPointer(3, 1, GL_UNSIGNED_INT, sizeof(SpriteInstance),
                           reinterpret_cast<void*>(base + offsetof(SpriteInstance, packed)));

    glBeginQuery(GL_TIME_ELAPSED, query);
    glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr,
                            static_cast<GLsizei>(count));
    glEndQuery(GL_TIME_ELAPSED);
    GLuint64 ns = 0;
    // NOTE: GL_QUERY_RESULT blocks until the GPU finishes — a full pipeline
    // stall every frame. Correct HERE because step 2's whole point is the
    // measurement, wrong as a steady-state path. When the viewport becomes
    // interactive (step 3+), switch to GL_QUERY_RESULT_NO_WAIT and read the
    // previous frame's timer, or drop per-frame timing entirely.
    glGetQueryObjectui64v(query, GL_QUERY_RESULT, &ns);
    return double(ns) / 1.0e6;
}

void MapView::initializeGL() {
    if (!initializeOpenGLFunctions()) {
        // The context is not 4.5 core — the function table is null and any GL
        // call from here would segfault. Report the context we actually got so
        // the mismatch is diagnosable, and refuse to draw rather than crash.
        auto* ctx = QOpenGLWidget::context();
        const auto f = ctx ? ctx->format() : QSurfaceFormat();
        std::fprintf(stderr,
            "[MapView] FATAL: could not resolve GL 4.5 core functions. "
            "Got context %d.%d, profile %d. The viewport needs 4.5 core.\n",
            f.majorVersion(), f.minorVersion(), int(f.profile()));
        std::fflush(stderr);
        glReady_ = false;
        return;
    }
    glReady_ = true;
    glClearColor(0.09f, 0.09f, 0.11f, 1.0f);

    ensureProgram();

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    // Unit quad 0..1, two triangles.
    const float quad[] = { 0.f,0.f, 1.f,0.f, 1.f,1.f, 0.f,1.f };
    const std::uint32_t idx[] = { 0,1,2, 0,2,3 };
    glGenBuffers(1, &quadVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), (void*)0);

    glGenBuffers(1, &quadEbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadEbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);

    // Instance buffer + attributes (divisor 1).
    glGenBuffers(1, &instVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteInstance),
                          (void*)offsetof(SpriteInstance, wx));
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, sizeof(SpriteInstance),
                           (void*)offsetof(SpriteInstance, layer));
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 1, GL_UNSIGNED_INT, sizeof(SpriteInstance),
                           (void*)offsetof(SpriteInstance, packed));
    glVertexAttribDivisor(3, 1);

    glGenQueries(1, &queryOpaque_);
    glGenQueries(1, &queryTranslucent_);

    if (haveCell_) dirtyUpload_ = true;
}

void MapView::resizeGL(int w, int h) {
    viewW_ = w > 0 ? w : 1;
    viewH_ = h > 0 ? h : 1;
    glViewport(0, 0, viewW_, viewH_);
    // Re-frame on resize only if we haven't been panned/zoomed by the user yet
    // would be nicer, but simplest correct behaviour: keep the camera as-is on
    // resize. A fresh cell calls fitToWindow explicitly.
}

void MapView::fitToWindow() {
    // Whole cell at 1:1 iso spans (cellSize*tileW) wide, (cellSize*tileH) tall.
    // Pick the scale that fits with a margin, and centre it. This is what step 2
    // computed every frame; now it just seeds the camera once per cell.
    const float isoW = float(cellSize_) * kTileW;
    const float isoH = float(cellSize_) * (kTileW * 0.5f);
    const float margin = 0.92f;
    zoom_ = margin * std::min(float(viewW_) / isoW, float(viewH_) / isoH);
    panX_ = float(viewW_) * 0.5f;
    panY_ = float(viewH_) * 0.5f - (isoH * zoom_) * 0.5f;
}

void MapView::wheelEvent(QWheelEvent* e) {
    // Zoom anchored at the cursor: the world point under the pointer stays put.
    // screen = worldIso*zoom + pan. Hold screen fixed at the cursor while zoom
    // changes: pan' = cursor - (cursor - pan) * (zoom'/zoom).
    const float cx = float(e->position().x());
    const float cy = float(e->position().y());
    const float step = (e->angleDelta().y() > 0) ? 1.15f : (1.0f / 1.15f);
    const float newZoom = std::clamp(zoom_ * step, 0.01f, 8.0f);
    const float k = newZoom / zoom_;
    panX_ = cx - (cx - panX_) * k;
    panY_ = cy - (cy - panY_) * k;
    zoom_ = newZoom;
    update();
    e->accept();
}

void MapView::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        dragging_ = true;
        lastMouseX_ = int(e->position().x());
        lastMouseY_ = int(e->position().y());
        e->accept();
    }
}

void MapView::mouseMoveEvent(QMouseEvent* e) {
    if (dragging_) {
        panX_ += float(int(e->position().x()) - lastMouseX_);
        panY_ += float(int(e->position().y()) - lastMouseY_);
        lastMouseX_ = int(e->position().x());
        lastMouseY_ = int(e->position().y());
        update();
        e->accept();
    }
}

void MapView::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) { dragging_ = false; e->accept(); }
}

void MapView::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
    case Qt::Key_F:   // re-frame the whole cell
        fitToWindow();
        update();
        break;
    case Qt::Key_1: { // jump to exact 1:1, keeping the view centre fixed.
        // This is the step-3 measurement trigger: full-resolution tiles with
        // real overdraw — the case step 2's fit-to-window could not reach.
        const float cx = float(viewW_) * 0.5f, cy = float(viewH_) * 0.5f;
        const float k = 1.0f / zoom_;
        panX_ = cx - (cx - panX_) * k;
        panY_ = cy - (cy - panY_) * k;
        zoom_ = 1.0f;
        std::printf("[MapView] jumped to 1:1 — next draw is the real overdraw case\n");
        std::fflush(stdout);
        update();
        break;
    }
    default:
        QOpenGLWidget::keyPressEvent(e);
        return;
    }
    e->accept();
}

void MapView::paintGL() {
    if (!glReady_) return;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!haveCell_ || (opaque_.empty() && translucent_.empty())) return;

    if (dirtyUpload_) uploadInstances();
    if (needsFit_) { fitToWindow(); needsFit_ = false; }

    glUseProgram(prog_);
    glBindVertexArray(vao_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, atlas_);
    glUniform1i(glGetUniformLocation(prog_, "uAtlas"), 0);
    glUniform2f(uSurface_, float(viewW_), float(viewH_));
    glUniform1f(uTileSize_, kTileW);

    // Camera-driven transform (step 3). zoom_ is the absolute scale (was the
    // fit scale at load; wheel changes it); pan_ is the absolute pixel origin
    // (centering at load; drag changes it). fitToWindow() seeds both.
    glUniform1f(uScale_, zoom_);
    glUniform2f(uOrigin_, panX_, panY_);

    const long nO = static_cast<long>(opaque_.size());
    const long nT = static_cast<long>(translucent_.size());

    // Pass 1: opaque floors, front-to-back, depth write. Pass 2: the rest.
    timing_.opaqueMs      = timedDraw(queryOpaque_, 0,  nO, /*opaque*/true);
    timing_.translucentMs = timedDraw(queryTranslucent_, nO, nT, /*opaque*/false);
    timing_.opaqueInstances = nO;
    timing_.translucentInstances = nT;

    std::printf("[MapView draw] opaque %ld inst %.3fms | translucent %ld inst %.3fms | total %.3fms\n",
                nO, timing_.opaqueMs, nT, timing_.translucentMs, timing_.totalMs());

    // Keep a cheap error check; the heavy readback instrumentation is removed
    // now that the QOpenGLWidget present path is confirmed working.
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR)
        std::printf("[MapView GLERR] 0x%04X\n", err);

    std::fflush(stdout);
    emit timingReady(timing_);
}

} // namespace pzmm

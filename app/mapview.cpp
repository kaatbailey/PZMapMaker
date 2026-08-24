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

    // Diagnostic: what is this cell actually made of? Count instances by tile-
    // name prefix so "wilderness vs buildings" is a fact, not a guess.
    {
        const auto& names = cell.header().tileNames;
        long blendsNat = 0, floors = 0, walls = 0, roofs = 0, veg = 0,
             street = 0, other = 0;
        auto pref = [](const std::string& s, const char* p) {
            return s.rfind(p, 0) == 0; };
        const int n = cell.cellSize();
        for (int z = cell.minLevel(); z <= cell.maxLevel(); ++z)
            for (int x = 0; x < n; ++x)
                for (int y = 0; y < n; ++y)
                    for (std::int32_t ti : cell.tilesAt(x, y, z)) {
                        if (ti < 0 || ti >= (int)names.size()) continue;
                        const std::string& nm = names[ti];
                        if (pref(nm,"blends_natural_")) ++blendsNat;
                        else if (pref(nm,"blends_street")||pref(nm,"street_")) ++street;
                        else if (pref(nm,"floors_")) ++floors;
                        else if (pref(nm,"walls_")||pref(nm,"fencing_")) ++walls;
                        else if (pref(nm,"roofs_")) ++roofs;
                        else if (pref(nm,"vegetation_")||pref(nm,"jumbo_")) ++veg;
                        else ++other;
                    }
        std::printf("[MapView content] blends_natural=%ld street=%ld floors=%ld "
                    "walls=%ld roofs=%ld vegetation=%ld other=%ld\n",
                    blendsNat, street, floors, walls, roofs, veg, other);
    }

    haveCell_ = true;
    needsFit_ = true;   // frame the new cell on the next paint (dims known then)
    maxLevel_ = census_.maxLevel;   // show all levels by default
    emit maxLevelChanged(maxLevel_);
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

void MapView::setSprites(std::vector<SpriteAtlas::Layer> layers) {
    sprites_ = std::move(layers);
    haveSprites_ = true;
    spritesDirty_ = true;
    if (isValid()) update();
}

void MapView::setMaxLevel(int z) {
    const int lo = census_.minLevel, hi = census_.maxLevel;
    const int clamped = std::clamp(z, lo, hi);
    if (clamped == maxLevel_) return;
    maxLevel_ = clamped;
    emit maxLevelChanged(maxLevel_);
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

uniform vec2  uSurface;    // viewport pixels
uniform float uTileSize;   // iso tile width at 1:1 (64)
uniform float uScale;      // zoom (pixels-per-1:1-pixel)
uniform vec2  uOrigin;     // pixel origin (pan)
uniform sampler2D uLayerMeta;  // per-layer (uvW,uvH,ox,oy), Nx1
uniform vec2  uAtlasDims;   // atlas layer size in px (max sprite w,h)

flat out uint vLayer;
flat out int  vZ;
out vec2 vUV;

void main() {
    vec4 meta = texelFetch(uLayerMeta, ivec2(int(iLayer), 0), 0);
    float uvW = meta.x, uvH = meta.y;      // fraction of atlas layer used
    float ox  = meta.z, oy  = meta.w;      // sprite draw offset (1:1 px)

    // Sprite pixel size at 1:1.
    float spW = uvW * uAtlasDims.x;
    float spH = uvH * uAtlasDims.y;

    int zlev = int((iPacked >> 8) & 0xFFu) - 128;
    float tw = uTileSize;                   // 64 at 1:1

    // Iso anchor: screen position of this tile's origin (before zoom/pan).
    // Standard 2:1 iso; z lifts by ~half a tile height per level.
    float ax = (iWorld.x - iWorld.y) * (tw * 0.5);
    float ay = (iWorld.x + iWorld.y) * (tw * 0.25) - float(zlev) * (tw * 0.75);

    // The sprite quad spans spW x spH, anchored so its BOTTOM-CENTRE sits at the
    // tile anchor, shifted by the authored offset (ox,oy). corner.y=0 is bottom.
    float lx = ax - spW * 0.5 + ox + aCorner.x * spW;
    float ly = ay - spH      + oy + aCorner.y * spH;

    // Apply zoom about the origin, then translate by pan.
    float px = lx * uScale + uOrigin.x;
    float py = ly * uScale + uOrigin.y;

    vec2 ndc = vec2(px / uSurface.x, py / uSurface.y) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float order = (float(zlev) * 512.0 + iWorld.x + iWorld.y) / 8192.0;
    float depth = 0.2 + clamp(order, 0.0, 1.0) * 0.6;
    gl_Position = vec4(ndc, depth, 1.0);
    vLayer = iLayer;
    vZ = zlev;
    // Sample only the used sub-rect of the layer (sprite sits bottom-left).
    vUV = vec2(aCorner.x * uvW, aCorner.y * uvH);
}
)";

    const char* fs = R"(#version 450 core
uniform sampler2DArray uAtlas;
uniform int uOpaquePass;      // 1: floors, alpha-test; 0: blended
uniform int uMaxLevel;        // hide tiles with z > this (level selector)
uniform int uLayerCount;      // atlas array depth; sprites beyond it are blank
flat in uint vLayer;
flat in int  vZ;
in vec2 vUV;
out vec4 fragColor;
void main() {
    if (vZ > uMaxLevel) discard;   // level selector: show z <= uMaxLevel
    if (int(vLayer) >= uLayerCount) discard;   // sprite past array cap: blank
    vec4 t = texture(uAtlas, vec3(vUV, float(vLayer)));
    if (uOpaquePass == 1) {
        // Opaque pass writes depth, so transparent sprite pixels must be
        // discarded or they punch depth holes. Alpha-test.
        if (t.a < 0.5) discard;
        fragColor = vec4(t.rgb, 1.0);
    } else {
        // Translucent pass: use the sprite's own alpha; skip fully-empty texels.
        if (t.a < 0.02) discard;
        fragColor = t;
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
    uLayerMeta_  = glGetUniformLocation(prog_, "uLayerMeta");
    uAtlasDims_  = glGetUniformLocation(prog_, "uAtlasDims");
    uMaxLevel_   = glGetUniformLocation(prog_, "uMaxLevel");
    uLayerCount_ = glGetUniformLocation(prog_, "uLayerCount");
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

void MapView::uploadSpriteAtlas() {
    if (sprites_.empty()) return;
    const int nLayers = static_cast<int>(sprites_.size());

    // Array layer dims = max sprite size across the cell. Sprites vary
    // (64x128 floors .. 192x256 objects); each blits into the bottom-left of
    // its layer and the shader samples only its (uvW,uvH) region.
    int maxW = 1, maxH = 1;
    for (const auto& L : sprites_) {
        if (L.found) { maxW = std::max(maxW, L.w); maxH = std::max(maxH, L.h); }
    }
    atlasW_ = maxW; atlasH_ = maxH;

    // A texture ARRAY has a hard layer cap (GL_MAX_ARRAY_TEXTURE_LAYERS, 2048 on
    // most NVIDIA). A downtown cell can use ~4000 distinct sprites, exceeding it
    // -> glTexImage3D fails with GL_INVALID_VALUE and the atlas is blank. Clamp
    // for now so most of the cell renders; sprites past the cap draw blank.
    // PROPER FIX (later): pack many sprites per 2D layer instead of one-per-layer.
    GLint maxLayers = 2048;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxLayers);
    if (nLayers > maxLayers) {
        std::printf("[MapView] WARNING: cell needs %d sprite layers but GPU caps "
                    "arrays at %d. Sprites %d+ will be blank until atlas packing "
                    "is implemented.\n", nLayers, maxLayers, maxLayers);
        std::fflush(stdout);
    }
    const int usableLayers = std::min(nLayers, int(maxLayers));
    atlasLayers_ = nLayers;   // instances still index full range; clamp storage
    usableLayers_ = usableLayers;

    if (atlas_) { glDeleteTextures(1, &atlas_); atlas_ = 0; }
    glGenTextures(1, &atlas_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, atlas_);
    while (glGetError() != GL_NO_ERROR) {}   // clear stale errors
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, atlasW_, atlasH_, usableLayers, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    if (GLenum e = glGetError(); e != GL_NO_ERROR) {
        std::printf("[MapView] ATLAS ALLOC FAILED: %dx%d x %d layers = %.1f GB, "
                    "GL error 0x%04X. Sprites will be blank.\n",
                    atlasW_, atlasH_, usableLayers,
                    double(atlasW_)*atlasH_*usableLayers*4/1e9, e);
        std::fflush(stdout);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Per-layer meta: (uvW, uvH, ox, oy) as RGBA32F, one texel per layer.
    std::vector<float> meta(static_cast<size_t>(nLayers) * 4, 0.0f);

    for (int L = 0; L < nLayers; ++L) {
        if (L >= usableLayers) break;   // array can't hold more; rest stay blank
        const auto& S = sprites_[L];
        if (S.found && !S.rgba.empty()) {
            // Blit the w*h sprite into the bottom-left of this layer.
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, L, S.w, S.h, 1,
                            GL_RGBA, GL_UNSIGNED_BYTE, S.rgba.data());
            meta[L*4+0] = float(S.w) / float(atlasW_);   // uvW
            meta[L*4+1] = float(S.h) / float(atlasH_);   // uvH
            meta[L*4+2] = float(S.ox);
            meta[L*4+3] = float(S.oy);
        } else {
            // Missing sprite: fully transparent. These are the ~10 no-sprite
            // vegetation tiles (species substituted at load in-game). Markers
            // just added noise that outnumbered the real content, so hide them.
            const int mW = std::min(2, atlasW_), mH = std::min(2, atlasH_);
            std::vector<std::uint8_t> mark(static_cast<size_t>(mW) * mH * 4, 0);
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, L, mW, mH, 1,
                            GL_RGBA, GL_UNSIGNED_BYTE, mark.data());
            meta[L*4+0] = float(mW) / float(atlasW_);
            meta[L*4+1] = float(mH) / float(atlasH_);
            meta[L*4+2] = 0.0f; meta[L*4+3] = 0.0f;
        }
    }
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Upload per-layer meta as an Nx1 RGBA32F texture, sampled by layer index.
    if (layerMeta_) { glDeleteTextures(1, &layerMeta_); layerMeta_ = 0; }
    glGenTextures(1, &layerMeta_);
    glBindTexture(GL_TEXTURE_2D, layerMeta_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, nLayers, 1, 0,
                 GL_RGBA, GL_FLOAT, meta.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    spritesDirty_ = false;
    std::printf("[MapView] atlas uploaded: %d layers, %dx%d each\n",
                nLayers, atlasW_, atlasH_);
    std::fflush(stdout);
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

    // Placeholder tint atlas only until real sprites arrive. Once setSprites()
    // has been called, uploadSpriteAtlas() owns atlas_ and this would clobber it.
    if (!haveSprites_) makePlaceholderAtlas(atlasLayers_);
    dirtyUpload_ = false;
}

double MapView::timedDraw(GLuint query, long first, long count, bool opaquePass) {
    if (count <= 0) return 0.0;
    glUniform1i(uOpaquePass_, opaquePass ? 1 : 0);
    // DEPTH-OFF TEST: both passes now run with depth test disabled, relying on
    // painter's order (opaque floors first, then translucent, both back-to-front
    // by draw order). If this fixes the post-pan vanishing, the bug was the depth
    // buffer (QOpenGLWidget FBO depth attachment not clearing/persisting on this
    // Wayland setup). The opaque pass keeps blend OFF + alpha-test discard so
    // transparent sprite pixels don't paint; the translucent pass blends.
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    if (opaquePass) {
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
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
    // Pick the scale that fits with a margin, and centre it. Bound to F: a
    // whole-cell overview. NOT the default view — at this zoom a 256x256 cell
    // is unreadable confetti. The default (resetView1to1) opens at real size.
    const float isoW = float(cellSize_) * kTileW;
    const float isoH = float(cellSize_) * (kTileW * 0.5f);
    const float margin = 0.92f;
    zoom_ = margin * std::min(float(viewW_) / isoW, float(viewH_) / isoH);
    panX_ = float(viewW_) * 0.5f;
    panY_ = float(viewH_) * 0.5f - (isoH * zoom_) * 0.5f;
}

void MapView::resetView1to1() {
    // Default view: 1:1 (tiles at full 64px), centred on the MIDDLE of the cell,
    // where buildings usually are. This is the readable view — you can tell
    // grass from road from floor. Use F for the zoomed-out whole-cell overview.
    zoom_ = 1.0f;
    const float mid = float(cellSize_) * 0.5f;
    // Iso position of the cell-centre tile, at zoom 1.
    const float cx = (mid - mid) * (kTileW * 0.5f);          // = 0
    const float cy = (mid + mid) * (kTileW * 0.25f);         // centre row depth
    // Put that iso point at the middle of the viewport.
    panX_ = float(viewW_) * 0.5f - cx;
    panY_ = float(viewH_) * 0.5f - cy;
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
    case Qt::Key_Backslash: { // jump to exact 1:1, keeping the view centre fixed
        const float cx = float(viewW_) * 0.5f, cy = float(viewH_) * 0.5f;
        const float k = 1.0f / zoom_;
        panX_ = cx - (cx - panX_) * k;
        panY_ = cy - (cy - panY_) * k;
        zoom_ = 1.0f;
        update();
        break;
    }
    case Qt::Key_BracketLeft:   // lower the visible ceiling one level
        setMaxLevel(maxLevel_ - 1);
        break;
    case Qt::Key_BracketRight:  // raise it one level
        setMaxLevel(maxLevel_ + 1);
        break;
    case Qt::Key_0: case Qt::Key_1: case Qt::Key_2: case Qt::Key_3:
    case Qt::Key_4: case Qt::Key_5: case Qt::Key_6: case Qt::Key_7:
        // Digit sets the visible ceiling directly to that level. (Key_1 also
        // used to mean "zoom 1:1"; level selection is the more useful binding
        // now that the map renders. Use F to reframe; wheel to zoom.)
        setMaxLevel(e->key() - Qt::Key_0);
        break;
    default:
        QOpenGLWidget::keyPressEvent(e);
        return;
    }
    e->accept();
}

void MapView::paintGL() {
    if (!glReady_) return;
    // Re-assert the viewport every frame. On this QOpenGLWidget/Wayland setup the
    // GL viewport is not reliably preserved between paints — a camera-only
    // update() (pan, F) left it wrong, so draws landed off the visible surface
    // and most tiles vanished; only resizeGL()'s glViewport call fixed it. Setting
    // it here makes every frame correct regardless of what Qt did in between.
    glViewport(0, 0, viewW_, viewH_);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!haveCell_ || (opaque_.empty() && translucent_.empty())) return;

    if (dirtyUpload_) uploadInstances();
    if (haveSprites_ && spritesDirty_) uploadSpriteAtlas();
    if (needsFit_) { resetView1to1(); needsFit_ = false; }

    glUseProgram(prog_);
    glBindVertexArray(vao_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, atlas_);
    glUniform1i(glGetUniformLocation(prog_, "uAtlas"), 0);
    // Per-layer meta on unit 1.
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, layerMeta_);
    glUniform1i(uLayerMeta_, 1);
    glUniform2f(uAtlasDims_, float(atlasW_), float(atlasH_));
    glUniform2f(uSurface_, float(viewW_), float(viewH_));
    glUniform1f(uTileSize_, kTileW);

    // Camera-driven transform (step 3). zoom_ is the absolute scale (was the
    // fit scale at load; wheel changes it); pan_ is the absolute pixel origin
    // (centering at load; drag changes it). fitToWindow() seeds both.
    glUniform1f(uScale_, zoom_);
    glUniform2f(uOrigin_, panX_, panY_);
    glUniform1i(uMaxLevel_, maxLevel_);
    glUniform1i(uLayerCount_, usableLayers_);

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

// instance_harness.cpp — the C1 §1.2 falsifier for the instanced-draw viewport.
//
// WHAT THIS TESTS
// ---------------
// C1 decided the C3 viewport draws every sprite as an instance in one batched
// draw call, and named a falsifier: push 500k instances of a single atlas at
// 1440p and measure GPU frame time. If it is not comfortably under 4 ms (leaving
// 12 ms of a 16.6 ms frame for everything else at 60 fps), instanced draw is the
// wrong architecture and we fall back to chunked merged geometry — and we want
// to know that in a day, not after C3 is half-built.
//
// This is a THROWAWAY. It is not part of libpzformat, links no project code,
// and answers exactly one question: what does 500k blended isometric sprite
// instances cost on this machine's GPU?
//
// WHY THESE CHOICES MAKE IT AN HONEST TEST (not a rigged-fast one)
//   - Alpha blending ON, depth test OFF. C1: depth testing does not save us with
//     alpha-blended sprites; painter's-order back-to-front is the real path.
//     Blending is the expensive part; turning it off would fake a good number.
//   - A real GL_TEXTURE_2D_ARRAY sampled per fragment, indexed by the instance's
//     tile_id, so texture bandwidth is in the measurement.
//   - Real overdraw: 500k sprites at ~18 px scattered over 2560x1440 overlap
//     heavily, as isometric tiles do.
//   - GPU timing via GL_TIME_ELAPSED, not wall clock: isolates the draw from
//     CPU, swap, and vsync. Vsync is off. We report the median over many frames.
//
// WHAT WOULD FALSIFY THE ARCHITECTURE
//   Median GPU time per frame >= 4 ms at 500k instances on the target hardware.
//   The program prints PASS/FAIL against that threshold so the result is not a
//   matter of interpretation.
//
// It is deliberately ONE draw call: glDrawElementsInstanced. C3's real path is
// glMultiDrawElementsIndirect, but that is the same GPU vertex-pull + fragment
// path; the multi-draw indirection batches across chunks and does not add
// per-instance cost. The instance count is the variable under test, so a single
// instanced draw is the faithful minimal case. (A --multidraw variant could be
// added later if the indirect path is ever suspected; it is not the risk C1
// named.)
//
// BUILD (standalone, needs Qt6 for context/window only — no project code):
//   see harness/CMakeLists.txt, or compile directly:
//     g++ -std=c++20 instance_harness.cpp -lQt6OpenGL -lQt6Gui -lQt6Core -lGL
//   Easiest: cmake -S harness -B harness/build -G Ninja && cmake --build harness/build
//
// RUN:
//   ./harness/build/instance_harness                # 500k, default
//   ./harness/build/instance_harness 1000000        # stress: 1M instances
//   ./harness/build/instance_harness 500000 2048    # explicit count + atlas layers
//
// Reads back nothing from the project. Prints a table and a verdict.

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLVersionFunctionsFactory>
#include <QSurfaceFormat>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

namespace {

// ---- Test parameters (C1 §1.2 target) --------------------------------------
constexpr int   kSurfaceW      = 2560;
constexpr int   kSurfaceH      = 1440;
constexpr int   kDefaultCount  = 500'000;   // the number C1 named
constexpr int   kAtlasLayers   = 256;       // texture array depth; tile_id % layers
constexpr int   kAtlasTexSize  = 64;        // px per layer; small, we want many
constexpr int   kWarmupFrames  = 30;
constexpr int   kMeasureFrames  = 300;
constexpr double kBudgetMs     = 4.0;       // FAIL at or above this

// A quad, two triangles, unit square centred on origin. The vertex shader
// scales it to sprite size and offsets by the instance world position.
constexpr float kQuad[] = {
    -0.5f, -0.5f, 0.0f, 0.0f,
     0.5f, -0.5f, 1.0f, 0.0f,
     0.5f,  0.5f, 1.0f, 1.0f,
    -0.5f,  0.5f, 0.0f, 1.0f,
};
constexpr std::uint32_t kQuadIdx[] = {0, 1, 2, 0, 2, 3};

// Per-instance data. Matches C1's SpriteInstance shape closely enough to price
// it: a world position, a tile id (selects atlas layer), a level (affects tint
// so the fragment shader actually reads it). 20 bytes, tightly packed.
struct Instance {
    float    x, y;        // world position in pixels on the surface
    std::uint32_t tileId; // -> atlas layer = tileId % kAtlasLayers
    std::uint32_t level;  // 0..7, used for a per-level tint so it is not dead
};

const char* kVert = R"(#version 450 core
layout(location = 0) in vec2 aPos;      // unit quad corner
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec2 iWorld;    // instance position (pixels)
layout(location = 3) in uint iTileId;
layout(location = 4) in uint iLevel;

uniform vec2 uSurface;     // (width, height) in pixels
uniform float uSprite;     // sprite size in pixels

flat out uint vLayer;
flat out uint vLevel;
out vec2 vUV;

void main() {
    vec2 px = iWorld + aPos * uSprite;          // corner in pixel space
    vec2 ndc = (px / uSurface) * 2.0 - 1.0;     // to clip space
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aUV;
    vLayer = iTileId % ATLAS_LAYERS_U;
    vLevel = iLevel;
}
)";

const char* kFrag = R"(#version 450 core
uniform sampler2DArray uAtlas;

flat in uint vLayer;
flat in uint vLevel;
in vec2 vUV;
out vec4 fragColor;

void main() {
    vec4 t = texture(uAtlas, vec3(vUV, float(vLayer)));
    // Per-level tint so vLevel is a real input the compiler can't fold away.
    float k = 0.6 + 0.05 * float(vLevel);
    fragColor = vec4(t.rgb * k, t.a * 0.85);   // 0.85: force the blend path
}
)";

QOpenGLFunctions_4_5_Core* gl = nullptr;

GLuint compile(GLenum type, const char* src) {
    GLuint s = gl->glCreateShader(type);
    gl->glShaderSource(s, 1, &src, nullptr);
    gl->glCompileShader(s);
    GLint ok = 0;
    gl->glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        gl->glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "shader compile failed:\n%s\n", log);
        std::exit(2);
    }
    return s;
}

GLuint link(GLuint vs, GLuint fs) {
    GLuint p = gl->glCreateProgram();
    gl->glAttachShader(p, vs);
    gl->glAttachShader(p, fs);
    gl->glLinkProgram(p);
    GLint ok = 0;
    gl->glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        gl->glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "link failed:\n%s\n", log);
        std::exit(2);
    }
    return p;
}

} // namespace

int main(int argc, char** argv) {
    // Args: [instances] [spritePx] [blend0|blend1]
    //   instances : instance count (default 500k, the C1 stress figure)
    //   spritePx  : sprite edge in pixels (default 18). Halving this quarters
    //               the fragment count; if frame time also quarters at fixed
    //               instance count, the harness is FILL-RATE bound, not
    //               instancing-bound — the discriminating experiment.
    //   blend     : 1 (default) real blended path; 0 disables blending so the
    //               opaque-ground case (depth-reject / no RMW) can be priced.
    const int   count    = (argc > 1) ? std::atoi(argv[1]) : kDefaultCount;
    const float spritePx = (argc > 2) ? float(std::atof(argv[2])) : 18.0f;
    const bool  useBlend = (argc > 3) ? (std::atoi(argv[3]) != 0) : true;

    // Request 4.5 core — C1's stated floor (4.6 on the dev NVIDIA box, 4.5 as
    // the portability target for Mesa/AMD).
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setVersion(4, 5);
    fmt.setSwapInterval(0); // vsync OFF — we are timing the GPU, not the display
    QSurfaceFormat::setDefaultFormat(fmt);

    QGuiApplication app(argc, argv);

    QOffscreenSurface surface;
    surface.setFormat(fmt);
    surface.create();
    if (!surface.isValid()) {
        std::fprintf(stderr, "offscreen surface invalid\n");
        return 2;
    }

    QOpenGLContext ctx;
    ctx.setFormat(fmt);
    if (!ctx.create() || !ctx.makeCurrent(&surface)) {
        std::fprintf(stderr, "could not create/make-current a 4.5 core context\n");
        return 2;
    }

    gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_5_Core>(&ctx);
    if (!gl || !gl->initializeOpenGLFunctions()) {
        std::fprintf(stderr, "could not resolve 4.5 core functions\n");
        return 2;
    }

    std::printf("GL vendor:   %s\n", gl->glGetString(GL_VENDOR));
    std::printf("GL renderer: %s\n", gl->glGetString(GL_RENDERER));
    std::printf("GL version:  %s\n", gl->glGetString(GL_VERSION));
    std::printf("instances:   %d\n", count);
    {
        const double frags = double(count) * double(spritePx) * double(spritePx);
        const double surface = double(kSurfaceW) * double(kSurfaceH);
        std::printf("surface:     %dx%d, sprite ~%.0fpx, blend %s depth OFF, vsync OFF\n",
                    kSurfaceW, kSurfaceH, spritePx, useBlend ? "ON" : "OFF");
        std::printf("overdraw:    %.1fM fragments = %.1fx the surface\n\n",
                    frags / 1e6, frags / surface);
    }

    // --- Render target: a 1440p FBO with an RGBA colour texture -------------
    GLuint fboTex = 0;
    gl->glGenTextures(1, &fboTex);
    gl->glBindTexture(GL_TEXTURE_2D, fboTex);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSurfaceW, kSurfaceH, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    GLuint fbo = 0;
    gl->glGenFramebuffers(1, &fbo);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, fboTex, 0);
    if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "FBO incomplete\n");
        return 2;
    }
    gl->glViewport(0, 0, kSurfaceW, kSurfaceH);

    // --- Atlas: a GL_TEXTURE_2D_ARRAY of noise, one distinct tint per layer -
    GLuint atlas = 0;
    gl->glGenTextures(1, &atlas);
    gl->glBindTexture(GL_TEXTURE_2D_ARRAY, atlas);
    gl->glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8,
                     kAtlasTexSize, kAtlasTexSize, kAtlasLayers, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    {
        std::vector<std::uint8_t> layer(kAtlasTexSize * kAtlasTexSize * 4);
        std::mt19937 rng(1234);
        for (int L = 0; L < kAtlasLayers; ++L) {
            const std::uint8_t base = static_cast<std::uint8_t>(40 + (L % 200));
            for (size_t i = 0; i < layer.size(); i += 4) {
                layer[i + 0] = static_cast<std::uint8_t>(base ^ (rng() & 0x3F));
                layer[i + 1] = static_cast<std::uint8_t>((base + 30) & 0xFF);
                layer[i + 2] = static_cast<std::uint8_t>((base + 60) & 0xFF);
                layer[i + 3] = 255;
            }
            gl->glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, L,
                                kAtlasTexSize, kAtlasTexSize, 1,
                                GL_RGBA, GL_UNSIGNED_BYTE, layer.data());
        }
    }
    gl->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // --- Geometry + instance buffers ---------------------------------------
    GLuint vao = 0, vboQuad = 0, ebo = 0, vboInst = 0;
    gl->glGenVertexArrays(1, &vao);
    gl->glBindVertexArray(vao);

    gl->glGenBuffers(1, &vboQuad);
    gl->glBindBuffer(GL_ARRAY_BUFFER, vboQuad);
    gl->glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                              reinterpret_cast<void*>(0));
    gl->glEnableVertexAttribArray(1);
    gl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                              reinterpret_cast<void*>(2 * sizeof(float)));

    gl->glGenBuffers(1, &ebo);
    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kQuadIdx), kQuadIdx, GL_STATIC_DRAW);

    // Build the instance set: scatter across the surface, random tile + level.
    std::vector<Instance> inst(static_cast<size_t>(count));
    {
        std::mt19937 rng(987654321u);
        std::uniform_real_distribution<float> fx(0.0f, float(kSurfaceW));
        std::uniform_real_distribution<float> fy(0.0f, float(kSurfaceH));
        std::uniform_int_distribution<std::uint32_t> tid(0, 100000);
        std::uniform_int_distribution<std::uint32_t> lvl(0, 7);
        for (auto& in : inst) {
            in.x = fx(rng);
            in.y = fy(rng);
            in.tileId = tid(rng);
            in.level = lvl(rng);
        }
    }

    gl->glGenBuffers(1, &vboInst);
    gl->glBindBuffer(GL_ARRAY_BUFFER, vboInst);
    gl->glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(inst.size() * sizeof(Instance)),
                     inst.data(), GL_STATIC_DRAW);
    // iWorld
    gl->glEnableVertexAttribArray(2);
    gl->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Instance),
                              reinterpret_cast<void*>(offsetof(Instance, x)));
    gl->glVertexAttribDivisor(2, 1);
    // iTileId
    gl->glEnableVertexAttribArray(3);
    gl->glVertexAttribIPointer(3, 1, GL_UNSIGNED_INT, sizeof(Instance),
                               reinterpret_cast<void*>(offsetof(Instance, tileId)));
    gl->glVertexAttribDivisor(3, 1);
    // iLevel
    gl->glEnableVertexAttribArray(4);
    gl->glVertexAttribIPointer(4, 1, GL_UNSIGNED_INT, sizeof(Instance),
                               reinterpret_cast<void*>(offsetof(Instance, level)));
    gl->glVertexAttribDivisor(4, 1);

    // --- Program + uniforms -------------------------------------------------
    // Single-source the atlas layer count: substitute kAtlasLayers into the
    // vertex shader's modulo so the C++ constant and the GLSL literal can never
    // drift apart.
    std::string vsrc = kVert;
    {
        const std::string needle = "ATLAS_LAYERS_U";
        const std::string repl = std::to_string(kAtlasLayers) + "u";
        for (size_t p = vsrc.find(needle); p != std::string::npos;
             p = vsrc.find(needle, p + repl.size())) {
            vsrc.replace(p, needle.size(), repl);
        }
    }

    GLuint prog = link(compile(GL_VERTEX_SHADER, vsrc.c_str()),
                        compile(GL_FRAGMENT_SHADER, kFrag));
    gl->glUseProgram(prog);
    gl->glUniform2f(gl->glGetUniformLocation(prog, "uSurface"),
                    float(kSurfaceW), float(kSurfaceH));
    gl->glUniform1f(gl->glGetUniformLocation(prog, "uSprite"), spritePx);
    gl->glUniform1i(gl->glGetUniformLocation(prog, "uAtlas"), 0);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D_ARRAY, atlas);

    // The real viewport path: alpha blend on, depth test off, painter's order.
    // --blend0 disables it to price the opaque-ground case separately.
    if (useBlend) {
        gl->glEnable(GL_BLEND);
        gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        gl->glDisable(GL_BLEND);
    }
    gl->glDisable(GL_DEPTH_TEST);

    // --- GPU timer ----------------------------------------------------------
    GLuint query = 0;
    gl->glGenQueries(1, &query);

    auto drawOnce = [&]() {
        gl->glClearColor(0.05f, 0.05f, 0.07f, 1.0f);
        gl->glClear(GL_COLOR_BUFFER_BIT);
        gl->glBindVertexArray(vao);
        gl->glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT,
                                    nullptr, count);
    };

    for (int i = 0; i < kWarmupFrames; ++i) {
        drawOnce();
        gl->glFinish();
    }

    std::vector<double> ms;
    ms.reserve(kMeasureFrames);
    for (int i = 0; i < kMeasureFrames; ++i) {
        gl->glBeginQuery(GL_TIME_ELAPSED, query);
        drawOnce();
        gl->glEndQuery(GL_TIME_ELAPSED);
        GLuint64 ns = 0;
        gl->glGetQueryObjectui64v(query, GL_QUERY_RESULT, &ns); // blocks
        ms.push_back(double(ns) / 1.0e6);
    }

    std::sort(ms.begin(), ms.end());
    const double med  = ms[ms.size() / 2];
    const double p95  = ms[static_cast<size_t>(ms.size() * 0.95)];
    const double mn   = ms.front();
    const double mx   = ms.back();

    std::printf("GPU frame time over %d frames:\n", kMeasureFrames);
    std::printf("  min    %.3f ms\n", mn);
    std::printf("  median %.3f ms\n", med);
    std::printf("  p95    %.3f ms\n", p95);
    std::printf("  max    %.3f ms\n", mx);
    std::printf("\nbudget: median < %.1f ms (C1 §1.2, leaves 12 ms of a 16.6 ms frame)\n",
                kBudgetMs);

    const bool pass = med < kBudgetMs;
    std::printf("\n==> %s at %d instances: median %.3f ms %s %.1f ms\n",
                pass ? "PASS" : "FAIL", count, med,
                pass ? "<" : ">=", kBudgetMs);
    if (pass) {
        std::printf("    Instanced draw is viable. C3 viewport may proceed on this path.\n");
    } else {
        std::printf("    Instanced draw exceeds budget on this GPU. C1 §1.2 fallback:\n");
        std::printf("    chunked merged geometry per chunk, not per-sprite instances.\n");
    }

    // headroom estimate: how many instances fit in the 4 ms budget, linearly
    const double perInst = med / double(count);
    if (perInst > 0.0) {
        const long budgetInst = static_cast<long>(kBudgetMs / perInst);
        std::printf("    ~%.3f ns/instance -> ~%ld instances fit the 4 ms budget.\n",
                    perInst * 1.0e6, budgetInst);
    }

    ctx.doneCurrent();
    return pass ? 0 : 1;
}

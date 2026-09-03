// pzpng — write a PNG that java.imageio produces byte for byte.
//
// ------------------------------------------------------------------------
// WHY THIS EXISTS RATHER THAN QImage
// ------------------------------------------------------------------------
//
// Step 7's oracle is byte-identical mod output, and `biomemap_X_Y.png` is
// shipped mod content — the engine reads it through BiomeMap.getRaster and
// BiomeMapConfig.lua, so it is not a diagnostic that could be skipped.
//
// MEASURED 2026-09-02: Qt's QImage does NOT reproduce Java's ImageIO, and no
// amount of configuration would make it. Four independent differences on
// identical pixel buffers:
//
//   IHDR            same: 8-bit, colour type 2, no interlace
//   ancillary       Qt writes pHYs; Java writes none
//   row filters     Java uses None on EVERY row; Qt picks per row
//   zlib header     Java 785e (FLEVEL 1); Qt 789c (FLEVEL 2)
//   IDAT chunking   Java 32768 bytes; Qt 8192
//
// Decoded pixels are identical in every case, so the GAME does not care — the
// oracle does.
//
// Java's PNGImageWriter for TYPE_INT_RGB turns out to be exactly reproducible:
// filter None on every row, zlib LEVEL 4 with the default strategy,
// 32768-byte IDAT chunks, no ancillary chunks. Measured against 200 varied
// 256x256 buffers (uniform, blocked, gradient, pure noise, and the four-band
// shape BiomeMapWriter actually produces): 200 of 200 byte-identical.
//
// ------------------------------------------------------------------------
// TWO THINGS THIS DEPENDS ON, NEITHER OF THEM A SPECIFICATION
// ------------------------------------------------------------------------
//
//   1. LEVEL 4 IS MEASURED, NOT DOCUMENTED. The zlib header only narrows it to
//      FLEVEL 1, which spans levels 2-5. Level 4 is what actually reproduces
//      the bytes, found by trying all nine levels against all five strategies.
//
//   2. THE AGREEMENT IS BETWEEN TWO IMPLEMENTATIONS, NOT A STANDARD. Java's
//      Deflater IS zlib, so the match is real rather than coincidental, but
//      zlib's output is not formally guaranteed stable across versions.
//      Measured against zlib 1.3 and OpenJDK 21.0.12.
//
//      STANDING FALSIFIER, and it is cheap: re-run the 200-buffer sweep
//      whenever either toolchain moves. `pz_png_oracle` is that sweep.
//
// ------------------------------------------------------------------------
// LAYERING
// ------------------------------------------------------------------------
//
// This is its own target (`pzpng`) rather than part of `pzgen`, because zlib
// is a dependency and `pzgen` is dependency-free. Owner decision 2026-09-02,
// on the same reasoning that put `pzgen` beside `pzformat` instead of inside
// it: keep the line crisp even where the exception would be harmless.
//
// It must NOT pull in Qt. `pzgen` building with no Qt at all is a standing
// falsifier (VERIFY.md §4) and `BiomeMapWriter` sits behind it.
#pragma once

#include <cstdint>
#include <vector>

namespace pzformat {

/// Encode a 24-bit RGB buffer as a PNG, byte-identical to
/// `ImageIO.write(BufferedImage.TYPE_INT_RGB, "png", …)`.
///
/// @param rgb  w*h*3 bytes, row-major, R then G then B per pixel
/// @return the complete PNG file
std::vector<unsigned char> writePngRgb(const std::vector<unsigned char>& rgb,
                                       int width, int height);

} // namespace pzformat

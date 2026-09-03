// pzpng.cpp — see pzpng.hpp for why this exists rather than QImage, and for
// the two things the byte-identity depends on.

#include "pzpng.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstddef>

namespace pzformat {

namespace {

void be32(std::vector<unsigned char>& o, std::uint32_t v) {
    o.push_back(static_cast<unsigned char>(v >> 24));
    o.push_back(static_cast<unsigned char>(v >> 16));
    o.push_back(static_cast<unsigned char>(v >> 8));
    o.push_back(static_cast<unsigned char>(v));
}

/// length, type, data, CRC over type+data.
void chunk(std::vector<unsigned char>& o, const char* type,
           const unsigned char* data, std::size_t n) {
    be32(o, static_cast<std::uint32_t>(n));
    const std::size_t start = o.size();
    o.insert(o.end(), type, type + 4);
    if (n != 0) o.insert(o.end(), data, data + n);
    be32(o, static_cast<std::uint32_t>(
        crc32(crc32(0L, Z_NULL, 0), o.data() + start, static_cast<uInt>(4 + n))));
}

/// Java's PNGImageWriter emits filter None on every row for TYPE_INT_RGB.
/// Measured across 200 buffers including pure noise, where an adaptive writer
/// would certainly have picked something else.
constexpr unsigned char kFilterNone = 0;

/// Measured. See PORT NOTE 1 in the header: the zlib header narrows this only
/// to 2..5, and 4 is what reproduces the bytes.
constexpr int kZlibLevel = 4;

/// Java splits IDAT at 32768 bytes. Qt uses 8192, which is one of the four
/// reasons QImage cannot be used here.
constexpr std::size_t kIdatChunk = 32768;

} // namespace

std::vector<unsigned char> writePngRgb(const std::vector<unsigned char>& rgb,
                                       int width, int height) {
    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);

    std::vector<unsigned char> out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

    std::vector<unsigned char> ih;
    be32(ih, static_cast<std::uint32_t>(width));
    be32(ih, static_cast<std::uint32_t>(height));
    ih.push_back(8);    // bit depth
    ih.push_back(2);    // colour type 2: truecolour RGB, no alpha
    ih.push_back(0);    // compression method
    ih.push_back(0);    // filter method
    ih.push_back(0);    // no interlace
    chunk(out, "IHDR", ih.data(), ih.size());

    // Filtered scanlines: one filter byte, then the row.
    std::vector<unsigned char> raw;
    raw.reserve(h * (w * 3 + 1));
    for (std::size_t y = 0; y < h; y++) {
        raw.push_back(kFilterNone);
        const std::size_t off = y * w * 3;
        raw.insert(raw.end(),
                   rgb.begin() + static_cast<std::ptrdiff_t>(off),
                   rgb.begin() + static_cast<std::ptrdiff_t>(off + w * 3));
    }

    uLongf cap = compressBound(static_cast<uLong>(raw.size()));
    std::vector<unsigned char> z(cap);
    compress2(z.data(), &cap, raw.data(), static_cast<uLong>(raw.size()), kZlibLevel);
    z.resize(cap);

    for (std::size_t i = 0; i < z.size(); i += kIdatChunk)
        chunk(out, "IDAT", z.data() + i, std::min(kIdatChunk, z.size() - i));

    chunk(out, "IEND", nullptr, 0);
    return out;
}

} // namespace pzformat

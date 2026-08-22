#include "le.hpp"
#include "lew.hpp"
#include "packfile.hpp"

#include "check.hpp"

#include <algorithm>
#include <vector>

using namespace pzformat;

namespace {

// A minimal but structurally valid PNG: 8-byte signature, IHDR chunk, IEND
// chunk. Real enough that the magic check passes and the legacy IEND-walk finds
// the true end. CRCs are not validated by the reader, so they are left zero.
std::vector<std::byte> minimalPng(std::uint32_t w, std::uint32_t h) {
    LEW s;
    // signature
    for (auto b : {0x89, int('P'), int('N'), int('G'), 0x0D, 0x0A, 0x1A, 0x0A}) s.u8(b);

    auto beU32 = [&](std::uint32_t v) {
        s.u8(static_cast<int>((v >> 24) & 0xFF));
        s.u8(static_cast<int>((v >> 16) & 0xFF));
        s.u8(static_cast<int>((v >> 8) & 0xFF));
        s.u8(static_cast<int>(v & 0xFF));
    };

    // IHDR: length 13, "IHDR", width, height, 5 more bytes, CRC(4)
    beU32(13);
    s.ascii("IHDR");
    beU32(w);
    beU32(h);
    s.u8(8).u8(6).u8(0).u8(0).u8(0); // bit depth, colour type, etc.
    beU32(0);                        // CRC placeholder

    // IEND: length 0, "IEND", CRC(4)
    beU32(0);
    s.ascii("IEND");
    beU32(0);

    return s.take();
}

// Build a synthetic pack via the writer, given layout and pages, then confirm
// read(write(x)) reproduces it and the bytes round-trip.
void testPzpkRoundTrip() {
    // Construct by reading a hand-written buffer so we exercise the reader, not
    // just the writer. Two pages, PZPK layout (length-prefixed PNGs).
    const auto png0 = minimalPng(64, 128);
    const auto png1 = minimalPng(32, 32);

    LEW w;
    w.ascii("PZPK").i32(1);   // magic, version
    w.i32(2);                 // numPages

    // page 0
    w.lenString("vegetation_trees_01");
    w.i32(2);                 // numEntries
    w.i32(1);                 // unknown
    w.lenString("vegetation_trees_01_0");
    w.i32(0).i32(0).i32(64).i32(96).i32(0).i32(0).i32(64).i32(96);
    w.lenString("vegetation_trees_01_1");
    w.i32(64).i32(0).i32(32).i32(64).i32(5).i32(6).i32(37).i32(70);
    w.i32(static_cast<std::int32_t>(png0.size()));
    w.bytes(png0);

    // page 1
    w.lenString("floors_01");
    w.i32(1);
    w.i32(0);                 // unknown = 0, as on some UI pages
    w.lenString("floors_01_0");
    w.i32(0).i32(0).i32(32).i32(32).i32(0).i32(0).i32(32).i32(32);
    w.i32(static_cast<std::int32_t>(png1.size()));
    w.bytes(png1);

    const auto bytes = w.take();

    const PackFile pf = PackFile::read(bytes);
    CHECK(pf.pzpk());
    CHECK_EQ(pf.version(), 1);
    CHECK_EQ(pf.pages().size(), std::size_t(2));

    const auto& p0 = pf.pages()[0];
    CHECK_EQ(p0.name, std::string("vegetation_trees_01"));
    CHECK_EQ(p0.entries.size(), std::size_t(2));
    CHECK_EQ(p0.entries[1].name, std::string("vegetation_trees_01_1"));
    CHECK_EQ(p0.entries[1].fy, 70);
    CHECK_EQ(p0.pngWidth(), 64);   // IHDR width, big-endian at offset 16
    CHECK_EQ(p0.pngHeight(), 128);

    CHECK_EQ(pf.pageUnknown()[0], 1);
    CHECK_EQ(pf.pageUnknown()[1], 0); // the 0 case is preserved, not normalised

    // write(read(bytes)) == bytes
    const auto again = pf.write();
    CHECK_EQ(again.size(), bytes.size());
    CHECK(std::equal(bytes.begin(), bytes.end(), again.begin()));
}

// Legacy layout: no PZPK header, no PNG length prefix (walk to IEND), pages
// separated by 0xDEADBEEF with none after the last.
void testLegacyRoundTrip() {
    const auto png0 = minimalPng(16, 16);
    const auto png1 = minimalPng(8, 8);

    LEW w;
    w.i32(2);                 // numPages, straight away -- no magic

    w.lenString("JumboTrees1x");
    w.i32(1).i32(1);
    w.lenString("jumbo_0");
    w.i32(0).i32(0).i32(16).i32(16).i32(0).i32(0).i32(16).i32(16);
    w.bytes(png0);            // NO length prefix
    w.i32(static_cast<std::int32_t>(0xDEADBEEF)); // separator after page 0

    w.lenString("JumboTrees2x");
    w.i32(1).i32(1);
    w.lenString("jumbo2_0");
    w.i32(0).i32(0).i32(8).i32(8).i32(0).i32(0).i32(8).i32(8);
    w.bytes(png1);            // no separator after the last page

    const auto bytes = w.take();

    const PackFile pf = PackFile::read(bytes);
    CHECK(!pf.pzpk());
    CHECK_EQ(pf.pages().size(), std::size_t(2));
    CHECK_EQ(pf.pages()[0].name, std::string("JumboTrees1x"));
    CHECK_EQ(pf.pages()[0].png.size(), png0.size()); // IEND-walk found the true end
    CHECK_EQ(pf.pages()[1].png.size(), png1.size());
    CHECK_EQ(pf.pageSeparator()[0], true);
    CHECK_EQ(pf.pageSeparator()[1], false);

    const auto again = pf.write();
    CHECK_EQ(again.size(), bytes.size());
    CHECK(std::equal(bytes.begin(), bytes.end(), again.begin()));
}

void testMisparsedEntryTableCaught() {
    // If the entry table is read wrong, the cursor lands somewhere that is not
    // a PNG header, and the magic check must fire. Simulate by claiming one
    // extra entry than the bytes provide -- the reader consumes PNG bytes as an
    // entry and then fails to find magic (or runs off the end).
    const auto png = minimalPng(16, 16);
    LEW w;
    w.ascii("PZPK").i32(1).i32(1);
    w.lenString("p");
    w.i32(1).i32(1);
    w.lenString("p_0");
    w.i32(0).i32(0).i32(16).i32(16).i32(0).i32(0).i32(16).i32(16);
    // Correct PNG length, but corrupt the first PNG byte so magic fails.
    auto badPng = png;
    badPng[0] = std::byte{0x00};
    w.i32(static_cast<std::int32_t>(badPng.size()));
    w.bytes(badPng);
    const auto bytes = w.take();

    CHECK_THROWS(PackFile::read(bytes));
}

void testLegacyNoIendCaught() {
    // A legacy PNG whose chunk walk never reaches IEND before the buffer ends.
    LEW w;
    w.i32(1);                 // one page, legacy
    w.lenString("p");
    w.i32(0).i32(1);          // zero entries
    // 8-byte signature then a truncated chunk header, no IEND.
    for (auto b : {0x89, int('P'), int('N'), int('G'), 0x0D, 0x0A, 0x1A, 0x0A}) w.u8(b);
    w.u8(0).u8(0).u8(0).u8(5).ascii("IDAT"); // says 5 data bytes but none follow
    const auto bytes = w.take();

    CHECK_THROWS(PackFile::read(bytes));
}

void testImplausiblePageCount() {
    LEW w;
    w.ascii("PZPK").i32(1).i32(999'999);
    const auto bytes = w.take();
    CHECK_THROWS(PackFile::read(bytes));
}

} // namespace

int main() {
    testPzpkRoundTrip();
    testLegacyRoundTrip();
    testMisparsedEntryTableCaught();
    testLegacyNoIendCaught();
    testImplausiblePageCount();
    return pztest::summary();
}

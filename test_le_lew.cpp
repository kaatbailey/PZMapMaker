#include "le.hpp"
#include "lew.hpp"

#include "check.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

using namespace pzformat;

namespace {

std::vector<std::byte> raw(std::initializer_list<int> vals) {
    std::vector<std::byte> v;
    v.reserve(vals.size());
    for (int b : vals) v.push_back(static_cast<std::byte>(b & 0xFF));
    return v;
}

// --- byte order is asserted against literal bytes, not against the writer -----
// A round-trip through LEW would pass even if both sides were big-endian.
// Prediction before running: 78 56 34 12 reads as 0x12345678.

void testByteOrderAgainstLiterals() {
    auto b = raw({0x78, 0x56, 0x34, 0x12});
    LE le(b);
    CHECK_EQ(le.i32(), std::int32_t(0x12345678));
    CHECK_EQ(le.pos(), std::size_t(4));

    auto b2 = raw({0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08});
    LE le2(b2);
    CHECK_EQ(le2.i64(), std::int64_t(0x0807060504030201LL));

    auto neg = raw({0xFF, 0xFF, 0xFF, 0xFF});
    LE le3(neg);
    CHECK_EQ(le3.i32(), std::int32_t(-1));

    auto min = raw({0x00, 0x00, 0x00, 0x80});
    LE le4(min);
    CHECK_EQ(le4.i32(), std::numeric_limits<std::int32_t>::min());

    auto hi = raw({0x80, 0xFF});
    LE le5(hi);
    CHECK_EQ(int(le5.u8()), 0x80);
    CHECK_EQ(int(le5.u8()), 0xFF);
}

// --- read(write(x)) == x -----------------------------------------------------

void testScalarRoundTrip() {
    const std::int32_t i32s[] = {
        0, 1, -1, 42, -42,
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max(),
        static_cast<std::int32_t>(0xDEADBEEF),
    };
    const std::int64_t i64s[] = {
        0, 1, -1,
        std::numeric_limits<std::int64_t>::min(),
        std::numeric_limits<std::int64_t>::max(),
        static_cast<std::int64_t>(0xFEEDFACECAFEBEEFULL),
    };

    LEW w;
    for (auto v : i32s) w.i32(v);
    for (auto v : i64s) w.i64(v);
    for (int v = 0; v < 256; ++v) w.u8(v);

    LE r(w.data());
    for (auto v : i32s) CHECK_EQ(r.i32(), v);
    for (auto v : i64s) CHECK_EQ(r.i64(), v);
    for (int v = 0; v < 256; ++v) CHECK_EQ(int(r.u8()), v);
    CHECK(r.eof());
}

void testStringRoundTrip() {
    // Includes bytes >= 0x80. If either side ever grows a UTF-8 conversion,
    // this is the check that fails.
    const std::string high = "tile\x80\xFF_01";
    const std::string plain = "walls_interior_house_01_11";

    LEW w;
    w.lenString(plain);
    w.lenString(high);
    w.lenString("");
    w.nlString(plain);
    w.nlString(high);

    LE r(w.data());
    CHECK_EQ(r.lenString(), plain);
    CHECK_EQ(r.lenString(), high);
    CHECK_EQ(r.lenString(), std::string());
    CHECK_EQ(r.cString(), plain);
    CHECK_EQ(r.cString(), high);
    CHECK(r.eof());
}

// write(read(bytes)) == bytes, which is the one that tests whether the read
// model retained everything.
void testWriteReadWriteIsIdentical() {
    LEW original;
    original.i32(3).nlString("floors_exterior_natural_01_0")
            .nlString("walls_interior_house_01_11")
            .nlString("blends_natural_01_64")
            .i64(-7)
            .lenString("Muldraugh, KY")
            .u8(0xC3);

    const auto bytes = original.take();

    LE r(bytes);
    LEW again;
    const std::int32_t n = r.i32();
    again.i32(n);
    for (std::int32_t i = 0; i < n; ++i) again.nlString(r.cString());
    again.i64(r.i64());
    again.lenString(r.lenString());
    again.u8(r.u8());

    CHECK(r.eof());
    CHECK_EQ(again.size(), bytes.size());
    CHECK(std::equal(bytes.begin(), bytes.end(), again.data().begin()));
}

// --- terminators -------------------------------------------------------------

void testCStringTerminators() {
    auto nul = raw({'a', 'b', 0, 'c'});
    LE r1(nul);
    CHECK_EQ(r1.cString(), std::string("ab"));
    CHECK_EQ(r1.pos(), std::size_t(3)); // terminator consumed

    auto nl = raw({'a', 'b', '\n', 'c'});
    LE r2(nl);
    CHECK_EQ(r2.cString(), std::string("ab"));

    auto cr = raw({'a', 'b', '\r', 'c'});
    LE r3(cr);
    CHECK_EQ(r3.cString(), std::string("ab"));

    // Unterminated at end of buffer returns what it has, as the Java did.
    auto bare = raw({'a', 'b'});
    LE r4(bare);
    CHECK_EQ(r4.cString(), std::string("ab"));
    CHECK(r4.eof());

    auto empty = raw({0});
    LE r5(empty);
    CHECK_EQ(r5.cString(), std::string());
}

// --- error paths -------------------------------------------------------------

void testBoundsAndGuards() {
    auto three = raw({1, 2, 3});
    LE r(three);
    CHECK_THROWS(r.i32());          // 4 bytes from a 3-byte buffer
    CHECK_EQ(r.pos(), std::size_t(0)); // and position is unmoved

    LE r2(three);
    r2.u8();
    CHECK_THROWS(r2.i64());
    CHECK_THROWS(r2.view(3));
    CHECK_THROWS(r2.bytes(3));

    LE r3(three);
    CHECK_THROWS(r3.seek(4));
    r3.seek(3);
    CHECK(r3.eof());
    CHECK_EQ(r3.remaining(), std::size_t(0));

    // Implausible length prefix: > 1<<20, and negative.
    auto big = raw({0x00, 0x00, 0x00, 0x01}); // 0x01000000
    LE r4(big);
    CHECK_THROWS(r4.lenString());

    auto negLen = raw({0xFF, 0xFF, 0xFF, 0xFF});
    LE r5(negLen);
    CHECK_THROWS(r5.lenString());

    // A plausible length with no bytes behind it must still fail, not truncate.
    auto lying = raw({0x10, 0x00, 0x00, 0x00, 'a'});
    LE r6(lying);
    CHECK_THROWS(r6.lenString());
}

void testPeekAndView() {
    auto b = raw({0x78, 0x56, 0x34, 0x12, 0xAA, 0xBB});
    LE r(b);
    CHECK_EQ(r.peekI32(), std::int32_t(0x12345678));
    CHECK_EQ(r.pos(), std::size_t(0));
    CHECK_EQ(r.i32(), std::int32_t(0x12345678));

    const auto v = r.view(2);
    CHECK_EQ(v.size(), std::size_t(2));
    CHECK(v.data() == b.data() + 4); // zero-copy: points into the source buffer
    CHECK(r.eof());
}

// --- hexDump layout ----------------------------------------------------------
// Exact-match against the Java's layout so probe output diffs cleanly.

void testHexDumpLayout() {
    std::vector<std::byte> b;
    for (int i = 0; i < 20; ++i) b.push_back(static_cast<std::byte>(i));
    LE r(b);

    const std::string dump = r.hexDump(0, 20);
    const std::string want1 =
        "00000000  00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F  |................|\n";

    CHECK(dump.rfind(want1, 0) == 0);

    const auto lines = std::count(dump.begin(), dump.end(), '\n');
    CHECK_EQ(lines, std::ptrdiff_t(2));
    CHECK(dump.size() > want1.size());
    CHECK(dump.compare(dump.size() - 8, 8, " |....|\n") == 0);

    // Printable bytes show as themselves.
    auto txt = raw({'M', 'u', 'l', 'd', 'r', 'a', 'u', 'g', 'h'});
    LE r2(txt);
    const std::string d2 = r2.hexDump(0, 9);
    CHECK(d2.find("|Muldraugh|") != std::string::npos);

    // offset + n past the end clamps instead of throwing.
    CHECK(r2.hexDump(0, 1000).find("|Muldraugh|") != std::string::npos);
    CHECK_EQ(r2.hexDump(9, 4), std::string());
}

} // namespace

int main() {
    testByteOrderAgainstLiterals();
    testScalarRoundTrip();
    testStringRoundTrip();
    testWriteReadWriteIsIdentical();
    testCStringTerminators();
    testBoundsAndGuards();
    testPeekAndView();
    testHexDumpLayout();
    return pztest::summary();
}

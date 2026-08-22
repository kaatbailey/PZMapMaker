#include "lew.hpp"
#include "lotheader.hpp"

#include "check.hpp"

#include <algorithm>
#include <vector>

using namespace pzformat;

namespace {

LotHeader synth() {
    LotHeader h;
    h.b42 = true;
    h.version = 1;
    h.tileNames = {
        "floors_exterior_natural_01_0",
        "walls_interior_house_01_11",
        "blends_natural_01_64",
        "tile\x80\xFF_high_bytes",
        "",                            // empty name: nlString writes just the \n
    };
    h.levelsAbove = 8;
    h.levelsBelow = 8;
    h.minLevel = -2;                   // the ~70 basement cells
    h.unknown12 = 7;

    Room r0;
    r0.name = "livingroom";
    r0.floor = 0;
    r0.rects = {{10, 12, 6, 5}, {16, 12, 3, 2}};
    r0.objects = {{1, 10, 12}, {-1, 0, 0}};
    h.rooms.push_back(r0);

    Room r1;
    r1.name = "bathroom";
    r1.floor = 1;
    r1.rects = {{20, 20, 2, 3}};
    h.rooms.push_back(r1);

    Room r2;                           // empty name, negative floor
    r2.floor = -2;
    h.rooms.push_back(r2);

    h.buildings = {{0, 1}, {2}, {}};   // including a zero-room building

    h.chunkGrid.resize(kGridBytes);
    for (int i = 0; i < kGridBytes; ++i) {
        h.chunkGrid[static_cast<std::size_t>(i)] = static_cast<std::byte>(i * 7 + 3);
    }
    return h;
}

void testB42RoundTrip() {
    const LotHeader original = synth();
    const auto bytes = original.write();

    const LotHeader back = LotHeader::read(bytes);
    CHECK(back.b42);
    CHECK(back.fullyConsumed);
    CHECK_EQ(back.version, 1);
    CHECK_EQ(back.tileNames.size(), std::size_t(5));
    CHECK_EQ(back.tileNames[0], std::string("floors_exterior_natural_01_0"));
    CHECK_EQ(back.tileNames[3], std::string("tile\x80\xFF_high_bytes"));
    CHECK(back.tileNames[4].empty());

    CHECK_EQ(back.levelsAbove, 8);
    CHECK_EQ(back.levelsBelow, 8);
    CHECK_EQ(back.minLevel, -2);
    CHECK_EQ(back.unknown12, 7);
    CHECK_EQ(back.maxLevel(), 7);
    CHECK_EQ(back.levelCount(), 10);
    CHECK_EQ(back.levelRange().minLevel, -2);
    CHECK_EQ(back.levelRange().maxLevel, 7);
    CHECK_EQ(back.levelRange().levelCount(), 10);

    CHECK_EQ(back.rooms.size(), std::size_t(3));
    CHECK_EQ(back.rooms[0].name, std::string("livingroom"));
    CHECK_EQ(back.rooms[0].rects.size(), std::size_t(2));
    CHECK_EQ(back.rooms[0].rects[1].w, 3);
    CHECK_EQ(back.rooms[0].objects.size(), std::size_t(2));
    CHECK_EQ(back.rooms[0].objects[1].a, -1);
    CHECK(back.rooms[2].name.empty());
    CHECK_EQ(back.rooms[2].floor, -2);

    CHECK_EQ(back.buildings.size(), std::size_t(3));
    CHECK_EQ(back.buildings[0].size(), std::size_t(2));
    CHECK(back.buildings[2].empty());
    CHECK_EQ(back.roomRefs(), std::size_t(3));

    CHECK_EQ(back.chunkGrid.size(), std::size_t(1024));
    CHECK_EQ(std::to_integer<unsigned>(back.chunkGrid[1]), 10u);

    // B42 stores no cell geometry. If these ever come back set, something
    // parsed the B41 path by mistake.
    CHECK_EQ(back.width, -1);
    CHECK_EQ(back.height, -1);
    CHECK_EQ(back.levels, -1);

    // write(read(bytes)) == bytes
    const auto again = back.write();
    CHECK_EQ(again.size(), bytes.size());
    CHECK(std::equal(bytes.begin(), bytes.end(), again.begin()));
}

// The trailer layout is only defensible because this check exists: after the
// buildings, exactly 1024 bytes must remain. One extra or missing int32
// anywhere upstream lands here.
void testTrailerGuardCatchesShift() {
    const auto bytes = synth().write();

    {   // one byte short of the grid
        auto truncated = bytes;
        truncated.pop_back();
        CHECK_THROWS(LotHeader::read(truncated));
    }
    {   // one byte too many
        auto extended = bytes;
        extended.push_back(std::byte{0});
        CHECK_THROWS(LotHeader::read(extended));
    }
}

void testBuildingIndexValidation() {
    LotHeader h = synth();
    h.buildings = {{0, 99}};           // room index past roomCount
    const auto bytes = h.write();
    CHECK_THROWS(LotHeader::read(bytes));
}

void testWriterGuards() {
    {   // B41 headers have no writer
        LotHeader h = synth();
        h.b42 = false;
        bool threw = false;
        try { (void)h.write(); } catch (const std::logic_error&) { threw = true; }
        CHECK(threw);
    }
    {   // grid must be exactly 1024 bytes
        LotHeader h = synth();
        h.chunkGrid.resize(1000);
        bool threw = false;
        try { (void)h.write(); } catch (const std::logic_error&) { threw = true; }
        CHECK(threw);
    }
}

// A B41 header has no magic, so the first four bytes are the version field and
// the reader must rewind. Build one and confirm it takes the legacy path.
void testB41Detection() {
    LEW w;
    w.i32(0);              // version -- not "LOTH"
    w.i32(2);              // tileCount
    w.ascii("floor_0").u8(0);
    w.ascii("wall_1").u8(0);
    w.i32(300).i32(300).i32(8);   // width, height, levels
    w.i32(0);              // roomCount
    w.i32(0);              // buildingCount
    for (int i = 0; i < 30 * 30; ++i) w.u8(i & 0xFF); // density grid
    const auto bytes = w.take();

    const LotHeader h = LotHeader::read(bytes);
    CHECK(!h.b42);
    CHECK_EQ(h.version, 0);
    CHECK_EQ(h.tileNames.size(), std::size_t(2));
    CHECK_EQ(h.tileNames[1], std::string("wall_1"));
    CHECK_EQ(h.width, 300);
    CHECK_EQ(h.height, 300);
    CHECK_EQ(h.levels, 8);
    CHECK_EQ(h.padBytesSkipped, 0);
    CHECK_EQ(h.zombieDensity.size(), std::size_t(900));
    CHECK(h.warnings.empty());

    // B41 is tolerant, not strict: a header whose geometry cannot be located
    // yields a raw trailer and a warning rather than an exception.
    LEW w2;
    w2.i32(0).i32(1);
    w2.ascii("floor_0").u8(0);
    for (int i = 0; i < 40; ++i) w2.u8(0xEE);
    const auto bytes2 = w2.take();
    const LotHeader h2 = LotHeader::read(bytes2);
    CHECK(!h2.b42);
    CHECK_EQ(h2.width, -1);
    CHECK_EQ(h2.warnings.size(), std::size_t(1));
    CHECK_EQ(h2.trailer.size(), std::size_t(40));
}

void testImplausibleTileCount() {
    LEW w;
    w.ascii("LOTH").i32(1).i32(999'999);
    const auto bytes = w.take();
    CHECK_THROWS(LotHeader::read(bytes));
}

} // namespace

int main() {
    testB42RoundTrip();
    testTrailerGuardCatchesShift();
    testBuildingIndexValidation();
    testWriterGuards();
    testB41Detection();
    testImplausibleTileCount();
    return pztest::summary();
}

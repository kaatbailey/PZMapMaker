#include "le.hpp"
#include "lew.hpp"
#include "tilebin.hpp"
#include "tiledefs.hpp"

#include "check.hpp"

#include <string>

using namespace pzformat;

namespace {

// A two-tileset text dump exercising: key/value props, valueless flags, the
// index formula index = y*width + x, and a name comment that matches.
const char* kText = R"(version = 1
tileset
{
    file = advertising_01
    size = 8,16
    id = 88
    // advertising_01_0
    tile
    {
        xy = 0,0
        Facing = S
        solid =
    }
    // advertising_01_9
    tile
    {
        xy = 1,1
        Facing = W
        container = crate
        ContainerCapacity = 10
    }
}
tileset
{
    file = walls_exterior_house_01
    size = 32,8
    id = 255
    // walls_exterior_house_01_33
    tile
    {
        xy = 1,1
        wall =
        doorN =
    }
}
)";

void testTextParse() {
    TileDefs td;
    td.parseText(kText);

    CHECK_EQ(td.tilesets().size(), std::size_t(2));
    CHECK_EQ(td.tileCount(), std::size_t(3));
    CHECK_EQ(td.nameMismatches(), 0); // all // comments matched computed names

    const Tile* t0 = td.find("advertising_01_0");
    CHECK(t0 != nullptr);
    CHECK_EQ(t0->index, 0);
    CHECK_EQ(t0->x, 0);
    CHECK_EQ(t0->y, 0);
    CHECK_EQ(t0->tileset, std::string("advertising_01"));
    CHECK(t0->facing().has_value());
    CHECK_EQ(*t0->facing(), std::string("S"));
    CHECK(t0->solid());                // valueless `solid` is a flag
    CHECK(t0->flag("solid"));
    CHECK(t0->get("solid").has_value());       // present...
    CHECK(t0->get("solid")->empty());          // ...with an empty value

    const Tile* t9 = td.find("advertising_01_9");
    CHECK(t9 != nullptr);
    CHECK_EQ(t9->index, 9);            // y=1,x=1,width=8 -> 1*8+1 = 9
    CHECK_EQ(*t9->facing(), std::string("W"));
    CHECK_EQ(*t9->get("container"), std::string("crate"));
    CHECK_EQ(*t9->get("ContainerCapacity"), std::string("10"));
    CHECK(!t9->solid());

    const Tile* tw = td.find("walls_exterior_house_01_33");
    CHECK(tw != nullptr);
    CHECK_EQ(tw->index, 33);          // y=1,x=1,width=32 -> 33
    CHECK(tw->flag("wall"));
    CHECK(tw->flag("doorN"));
    CHECK(!tw->flag("doorW"));

    const Tileset* ts = td.tilesetByFile("advertising_01");
    CHECK(ts != nullptr);
    CHECK_EQ(ts->width, 8);
    CHECK_EQ(ts->height, 16);
    CHECK_EQ(ts->id, 88);
}

void testNameMismatchDetected() {
    // Comment claims _5 but xy=0,0 in an 8-wide tileset computes _0.
    const char* bad = R"(tileset
{
    file = t
    size = 8,8
    id = 1
    // t_5
    tile
    {
        xy = 0,0
        solid =
    }
}
)";
    TileDefs td;
    td.parseText(bad);
    CHECK_EQ(td.nameMismatches(), 1);
    CHECK_EQ(td.mismatchSamples().size(), std::size_t(1));
}

// Build a synthetic .tiles (tdef) buffer in the confirmed retail layout:
// CountOnly prelude, extraInts=0. Same tiles as the text above, so the two
// parsers must produce identical property maps -- the check that actually
// matters for the binary reader.
std::vector<std::byte> synthTiles() {
    LEW w;
    w.ascii("tdef");
    w.i32(1);          // version
    w.i32(1);          // tilesetCount

    // tileset advertising_01, 8x16, id 88, 2 tiles
    w.nlString("advertising_01");
    w.nlString("advertising_01.png");
    w.i32(8).i32(16).i32(88);
    w.i32(2);          // tileCount

    // tile 0 (index 0): Facing=S, solid= (flag)
    w.i32(2);          // propCount
    w.nlString("Facing").nlString("S");
    w.nlString("solid").nlString("");

    // tile 1 (index 1): Facing=W, container=crate, ContainerCapacity=10
    w.i32(3);
    w.nlString("Facing").nlString("W");
    w.nlString("container").nlString("crate");
    w.nlString("ContainerCapacity").nlString("10");

    return w.take();
}

void testBinaryParse() {
    const auto bytes = synthTiles();
    const TileBin tb = TileBin::read(bytes, TileShape::CountOnly, 0);

    CHECK_EQ(tb.version(), 1);
    CHECK_EQ(tb.tilesetCount(), 1);
    CHECK_EQ(tb.tileCount(), std::size_t(2));

    const Tile* t0 = tb.find("advertising_01_0");
    CHECK(t0 != nullptr);
    CHECK_EQ(*t0->facing(), std::string("S"));
    CHECK(t0->solid());

    const Tile* t1 = tb.find("advertising_01_1");
    CHECK(t1 != nullptr);
    CHECK_EQ(*t1->get("container"), std::string("crate"));
    CHECK_EQ(*t1->get("ContainerCapacity"), std::string("10"));
}

// The independent cross-check inside one language: text and binary parsers,
// fed the same tiles, must agree property-for-property. This is the same
// comparison TileBin.solveAll runs against real vanilla files.
void testBinaryMatchesText() {
    const char* text = R"(tileset
{
    file = advertising_01
    size = 8,16
    id = 88
    // advertising_01_0
    tile
    {
        xy = 0,0
        Facing = S
        solid =
    }
    // advertising_01_1
    tile
    {
        xy = 1,0
        Facing = W
        container = crate
        ContainerCapacity = 10
    }
}
)";
    TileDefs td;
    td.parseText(text);

    const auto bytes = synthTiles();
    const TileBin tb = TileBin::read(bytes, TileShape::CountOnly, 0);

    // Every text tile must appear in the binary with an identical prop map.
    // (The binary may hold more -- propertyless tiles the text omits -- but the
    // synthetic pair is matched so counts are equal here.)
    for (const auto& [name, ttile] : td.ordered()) {
        const Tile* btile = tb.find(name);
        CHECK(btile != nullptr);
        if (btile) CHECK(ttile->props == btile->props);
    }
    CHECK_EQ(td.tileCount(), tb.tileCount());
}

void testWrongShapeRejected() {
    const auto bytes = synthTiles();
    // XY_COUNT reads two int32s where CountOnly has one, so it consumes a
    // property key as coordinates and derails -- must throw, not silently
    // mis-parse to the end.
    CHECK_THROWS(TileBin::read(bytes, TileShape::XYCount, 0));
    // A nonzero extraInts shifts every field and lands off the end.
    CHECK_THROWS(TileBin::read(bytes, TileShape::CountOnly, 1));
}

void testBadMagic() {
    auto bytes = synthTiles();
    bytes[0] = static_cast<std::byte>('x');
    CHECK_THROWS(TileBin::read(bytes, TileShape::CountOnly, 0));
}

} // namespace

int main() {
    testTextParse();
    testNameMismatchDetected();
    testBinaryParse();
    testBinaryMatchesText();
    testWrongShapeRejected();
    testBadMagic();
    return pztest::summary();
}

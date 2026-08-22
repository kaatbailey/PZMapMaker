#include "celldata.hpp"
#include "le.hpp"
#include "lew.hpp"
#include "lotheader.hpp"
#include "lotpack.hpp"

#include "check.hpp"

#include <algorithm>
#include <vector>

using namespace pzformat;

namespace {

// Build a small but non-trivial cell: 2x2 chunks (16x16), 8 levels, a negative
// minLevel so the actual-z / z-index mapping is exercised. Written with the
// confirmed SPAN_LEVELS_FULL policy so it should round-trip through CellData.
LotHeader synthHeader() {
    LotHeader h;
    h.b42 = true;
    h.version = 1;
    h.levelsAbove = 8;
    h.levelsBelow = 8;
    h.minLevel = -2;
    h.unknown12 = 5;              // levelCount = 5 - (-2) + 1 = 8
    h.tileNames = {
        "floors_exterior_natural_01_0",
        "walls_interior_house_01_11",
        "blends_natural_01_64",
    };
    h.chunkGrid.assign(static_cast<std::size_t>(kGridBytes), std::byte{0});
    h.fullyConsumed = true;
    return h;
}

// A .lotpack matching synthHeader, SPAN_LEVELS_FULL: every chunk encodes all 8
// levels. Chunk 0 gets a real square at (z=-2 index 0, x0, y0); the rest empty.
std::vector<std::byte> synthPack() {
    const int chunkCount = 4;
    const int levels = 8;
    const int spl = kSquaresPerLevel;

    auto body_with_square = [&](bool putSquare) {
        LEW w;
        std::int32_t run = 0;
        for (int sq = 0; sq < levels * spl; ++sq) {
            if (putSquare && sq == 0) {
                if (run > 0) { w.i32(-1).i32(run); run = 0; }
                w.i32(2).i32(7).i32(0); // count=2 (room+1 tile), room=7, tile idx 0
            } else {
                ++run;
            }
        }
        if (run > 0) w.i32(-1).i32(run);
        return w.take();
    };

    std::vector<std::vector<std::byte>> bodies;
    bodies.push_back(body_with_square(true));
    for (int i = 1; i < chunkCount; ++i) bodies.push_back(body_with_square(false));

    LEW w;
    w.ascii("LOTP").i32(1).i32(chunkCount);
    std::int64_t off = 12 + 8 * chunkCount;
    for (const auto& b : bodies) { w.i64(off); off += static_cast<std::int64_t>(b.size()); }
    for (const auto& b : bodies) w.bytes(b);
    return w.take();
}

void testLoadAndAccessors() {
    const auto pack = synthPack();
    CellData c = CellData::load(pack, synthHeader());

    CHECK_EQ(c.cellSize(), 16);
    CHECK_EQ(c.chunksPerSide(), 2);
    CHECK_EQ(c.levelCount(), 8);
    CHECK_EQ(c.minLevel(), -2);
    CHECK_EQ(c.maxLevel(), 5);
    CHECK_EQ(c.zIndex(-2), 0);   // basement maps to index 0
    CHECK_EQ(c.zIndex(0), 2);

    // The one real square sits at actual z = -2 (index 0), local (0,0).
    CHECK(c.hasSquare(0, 0, -2));
    CHECK_EQ(c.roomAt(0, 0, -2), 7);
    const auto t = c.tilesAt(0, 0, -2);
    CHECK_EQ(t.size(), std::size_t(1));
    CHECK_EQ(t[0], 0);
    const auto names = c.tileNamesAt(0, 0, -2);
    CHECK_EQ(names.size(), std::size_t(1));
    CHECK_EQ(names[0], std::string("floors_exterior_natural_01_0"));

    CHECK(!c.hasSquare(1, 1, -2));
    CHECK_EQ(c.roomAt(1, 1, -2), -1);
    CHECK_EQ(c.nonEmptySquares(), 1L);
}

// The core proof: load -> writeLotPack -> bytes identical to the input. This
// exercises the SPAN_LEVELS_FULL encoder through CellData's own storage.
void testWriteRoundTrip() {
    const auto pack = synthPack();
    CellData c = CellData::load(pack, synthHeader());
    const auto out = c.writeLotPack();
    CHECK_EQ(out.size(), pack.size());
    CHECK(std::equal(pack.begin(), pack.end(), out.begin()));
}

// The lotheader written back must match what the header would serialise.
void testHeaderRoundTrip() {
    const auto pack = synthPack();
    CellData c = CellData::load(pack, synthHeader());
    const auto hdrOut = c.writeLotHeader();
    const auto hdrExpected = synthHeader().write();
    CHECK_EQ(hdrOut.size(), hdrExpected.size());
    CHECK(std::equal(hdrExpected.begin(), hdrExpected.end(), hdrOut.begin()));
}

// fill() must touch exactly the rectangle, and report only genuinely changed
// squares. Then diff() must confirm the edit was surgical.
void testFillAndDiff() {
    const auto pack = synthPack();
    CellData before = CellData::load(pack, synthHeader());
    CellData after  = CellData::load(pack, synthHeader());

    // Fill a 3x3 at actual z=0 with a new floor. None existed there, so all 9
    // change. The tile name is new -> appended to the header table.
    const std::size_t namesBefore = after.header().tileNames.size();
    const int changed = after.fill("floors_new_grass_01_0", 2, 2, 3, 3, 0);
    CHECK_EQ(changed, 9);
    CHECK_EQ(after.header().tileNames.size(), namesBefore + 1);

    const auto d = CellData::diff(before, after);
    CHECK_EQ(d.squaresAdded, 9);   // were empty, now filled
    CHECK_EQ(d.squaresChanged, 0);
    CHECK_EQ(d.squaresRemoved, 0);
    CHECK(!d.isEmpty());

    // Re-filling the identical rectangle changes nothing.
    const int again = after.fill("floors_new_grass_01_0", 2, 2, 3, 3, 0);
    CHECK_EQ(again, 0);

    // A no-op edit leaves diff empty.
    CellData x = CellData::load(pack, synthHeader());
    CellData y = CellData::load(pack, synthHeader());
    CHECK(CellData::diff(x, y).isEmpty());
}

// setSquare / clearSquare and the tileIndex append contract.
void testEditPrimitives() {
    const auto pack = synthPack();
    CellData c = CellData::load(pack, synthHeader());

    // Appending an existing name returns its index; a new one grows the table.
    CHECK_EQ(c.tileIndex("walls_interior_house_01_11"), 1);
    const auto n0 = c.header().tileNames.size();
    const std::int32_t fresh = c.tileIndex("furniture_seating_01_5");
    CHECK_EQ(fresh, static_cast<std::int32_t>(n0));
    CHECK_EQ(c.header().tileNames.size(), n0 + 1);

    c.setSquare(5, 5, 1, {fresh, 2}, 42);
    CHECK(c.hasSquare(5, 5, 1));
    CHECK_EQ(c.roomAt(5, 5, 1), 42);
    CHECK_EQ(c.tilesAt(5, 5, 1).size(), std::size_t(2));

    c.clearSquare(5, 5, 1);
    CHECK(!c.hasSquare(5, 5, 1));
    CHECK_EQ(c.roomAt(5, 5, 1), -1);

    // Editing then writing must still round-trip through a reload.
    c.setSquare(3, 4, 2, {0}, 9);
    const auto bytes = c.writeLotPack();
    CellData reloaded = CellData::load(bytes, synthHeader());
    CHECK(reloaded.hasSquare(3, 4, 2));
    CHECK_EQ(reloaded.roomAt(3, 4, 2), 9);
    CHECK_EQ(reloaded.tilesAt(3, 4, 2)[0], 0);
}

void testZBounds() {
    const auto pack = synthPack();
    CellData c = CellData::load(pack, synthHeader());
    // actual z range is -2..5; anything outside throws out_of_range (a logic
    // error, matching the Java IllegalArgumentException — NOT a ParseError).
    auto throwsRange = [](auto&& fn) {
        try { fn(); } catch (const std::out_of_range&) { return true; } catch (...) {}
        return false;
    };
    CHECK(throwsRange([&] { (void)c.tilesAt(0, 0, -3); }));
    CHECK(throwsRange([&] { (void)c.tilesAt(0, 0, 6); }));
    // valid extremes do not throw
    (void)c.hasSquare(0, 0, -2);
    (void)c.hasSquare(0, 0, 5);
}

void testBlankCell() {
    LotHeader h = synthHeader();
    CellData c = CellData::blank(std::move(h), 2);
    CHECK_EQ(c.nonEmptySquares(), 0L);
    CHECK_EQ(c.cellSize(), 16);
    // A blank cell still writes a valid, full-level lotpack.
    const auto bytes = c.writeLotPack();
    CellData reloaded = CellData::load(bytes, synthHeader());
    CHECK_EQ(reloaded.nonEmptySquares(), 0L);
    CHECK(std::equal(bytes.begin(), bytes.end(), reloaded.writeLotPack().begin()));
}

} // namespace

int main() {
    testLoadAndAccessors();
    testWriteRoundTrip();
    testHeaderRoundTrip();
    testFillAndDiff();
    testEditPrimitives();
    testZBounds();
    testBlankCell();
    return pztest::summary();
}

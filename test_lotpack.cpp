#include "lew.hpp"
#include "lotpack.hpp"

#include "check.hpp"

#include <algorithm>
#include <vector>

using namespace pzformat;

namespace {

// A synthetic 2x2-chunk cell. Small enough to reason about by hand, and it
// exercises the four cases that matter:
//   chunk 0: square 0 with one tile, then a run to the level boundary
//   chunk 1: a run covering all of z=0, then a square at z=1 with three tiles
//   chunk 2: a square with count == 1 -- a room and NO tiles, which is not the
//            same as an empty square and must not re-encode as part of a run
//   chunk 3: a run, then the last square of the level, ending flush
//
// Every body fills to the end of its last level. That is not decoration: see
// the note in the response about what minimal-levels round-tripping implies.

std::vector<std::byte> body0() {
    LEW w;
    w.i32(2).i32(5).i32(7);   // count=2, room=5, tile 7
    w.i32(-1).i32(63);        // squares 1..63 empty
    return w.take();
}

std::vector<std::byte> body1() {
    LEW w;
    w.i32(-1).i32(64);                       // all of z=0 empty
    w.i32(4).i32(9).i32(11).i32(12).i32(13); // z=1 square 0: room 9, tiles 11,12,13
    w.i32(-1).i32(63);
    return w.take();
}

std::vector<std::byte> body2() {
    LEW w;
    w.i32(1).i32(3);   // count=1: room 3, zero tiles
    w.i32(-1).i32(63);
    return w.take();
}

std::vector<std::byte> body3() {
    LEW w;
    w.i32(-1).i32(63);        // squares 0..62 empty
    w.i32(2).i32(-1).i32(42); // square 63: no room, tile 42
    return w.take();
}

std::vector<std::byte> synthCell() {
    const std::vector<std::vector<std::byte>> bodies{body0(), body1(), body2(), body3()};

    const std::int64_t headerSize = 12 + 8 * 4;
    LEW w;
    w.ascii("LOTP").i32(1).i32(4);
    std::int64_t off = headerSize;
    for (const auto& b : bodies) { w.i64(off); off += static_cast<std::int64_t>(b.size()); }
    for (const auto& b : bodies) w.bytes(b);
    return w.take();
}

constexpr LevelRange kLevelRange{0, 7}; // 8 levels, as a shallow cell

void testHeader() {
    const auto cell = synthCell();
    const auto lp = LotPack::read(cell, kLevelRange);

    CHECK_EQ(lp.version(), 1);
    CHECK_EQ(lp.chunkCount(), 4);
    CHECK_EQ(lp.chunksPerSide(), 2);
    CHECK_EQ(lp.cellSize(), 16);
    CHECK_EQ(lp.levelCount(), 8);
    CHECK_EQ(lp.offsets()[0], std::int64_t(44)); // 12 + 8*4, must equal header size

    // Column-major: cx varies slowest. If this ever reads cy*perSide+cx the
    // whole cell transposes and nothing else in the suite notices.
    CHECK_EQ(lp.chunkIndex(0, 0), 0);
    CHECK_EQ(lp.chunkIndex(0, 1), 1);
    CHECK_EQ(lp.chunkIndex(1, 0), 2);
    CHECK_EQ(lp.chunkIndex(1, 1), 3);

    CHECK_EQ(lp.zIndex(0), 0);
    CHECK_EQ(lp.actualZ(3), 3);
}

void testDecode() {
    const auto cell = synthCell();
    const auto lp = LotPack::read(cell, kLevelRange);

    const Chunk c0 = lp.chunk(0, 0);
    CHECK(c0.has(0, 0, 0));
    CHECK_EQ(c0.room(0, 0, 0), 5);
    CHECK_EQ(c0.tiles(0, 0, 0).size(), std::size_t(1));
    CHECK_EQ(c0.tiles(0, 0, 0)[0], 7);
    CHECK(!c0.has(0, 0, 1));
    CHECK_EQ(c0.squaresCovered(), 64);
    CHECK_EQ(c0.maxZ(), 0);
    CHECK_EQ(c0.levelsWithData(), 1);

    const Chunk c1 = lp.chunk(0, 1);
    CHECK(!c1.has(0, 0, 0));
    CHECK(c1.has(1, 0, 0));
    CHECK_EQ(c1.room(1, 0, 0), 9);
    CHECK_EQ(c1.tiles(1, 0, 0).size(), std::size_t(3));
    CHECK_EQ(c1.tiles(1, 0, 0)[2], 13);
    CHECK_EQ(c1.maxZ(), 1);
    CHECK_EQ(c1.levelsWithData(), 2);
    CHECK_EQ(c1.squaresCovered(), 128);

    // count == 1: present, room set, zero tiles. The distinction the run
    // encoder must not collapse.
    const Chunk c2 = lp.chunk(1, 0);
    CHECK(c2.has(0, 0, 0));
    CHECK_EQ(c2.room(0, 0, 0), 3);
    CHECK_EQ(c2.tiles(0, 0, 0).size(), std::size_t(0));

    const Chunk c3 = lp.chunk(1, 1);
    CHECK(c3.has(0, 7, 7));
    CHECK_EQ(c3.room(0, 7, 7), -1);
    CHECK_EQ(c3.tiles(0, 7, 7)[0], 42);
    CHECK_EQ(c3.squaresCovered(), 64);

    // Absent squares report room -1 and no tiles rather than throwing.
    CHECK_EQ(c3.room(0, 0, 0), -1);
    CHECK(c3.tiles(0, 0, 0).empty());
    CHECK(!c3.has(40, 0, 0)); // past stored levels
}

// The real test of the port: re-encode each body and compare to the original
// bytes. Read and write share no formula here, unlike chunkIndex, so this
// catches an asymmetry between the decoder and the encoder.
void testReEncodeIsByteIdentical() {
    const auto cell = synthCell();
    const auto lp = LotPack::read(cell, kLevelRange);

    for (int i = 0; i < lp.chunkCount(); ++i) {
        const auto original = lp.rawChunk(i);
        const auto again = lp.encodeChunk(lp.chunkAt(i), Policy::SpanLevelsMinimal);
        CHECK_EQ(again.size(), original.size());
        CHECK(std::equal(original.begin(), original.end(), again.begin()));
    }

    const auto rebuilt = lp.write(Policy::SpanLevelsMinimal);
    CHECK_EQ(rebuilt.size(), cell.size());
    CHECK(std::equal(cell.begin(), cell.end(), rebuilt.begin()));
}

// A different policy must still round-trip through the decoder to the same
// squares, even though the bytes differ.
void testFullLevelPolicyRoundTrips() {
    const auto cell = synthCell();
    const auto lp = LotPack::read(cell, kLevelRange);

    const auto full = lp.write(Policy::SpanLevelsFull);
    CHECK(full.size() > cell.size()); // pads every chunk to 8 levels

    const auto lp2 = LotPack::read(full, kLevelRange);
    for (int cx = 0; cx < 2; ++cx) {
        for (int cy = 0; cy < 2; ++cy) {
            const Chunk a = lp.chunk(cx, cy);
            const Chunk b = lp2.chunk(cx, cy);
            CHECK_EQ(b.maxZ(), a.maxZ());
            for (int z = 0; z <= std::max(a.maxZ(), 0); ++z) {
                for (int x = 0; x < kChunkSize; ++x) {
                    for (int y = 0; y < kChunkSize; ++y) {
                        CHECK_EQ(b.has(z, x, y), a.has(z, x, y));
                        if (!a.has(z, x, y)) continue;
                        CHECK_EQ(b.room(z, x, y), a.room(z, x, y));
                        const auto ta = a.tiles(z, x, y);
                        const auto tb = b.tiles(z, x, y);
                        CHECK_EQ(tb.size(), ta.size());
                        CHECK(std::equal(ta.begin(), ta.end(), tb.begin()));
                    }
                }
            }
        }
    }

    // BreakAtLevel differs from SpanLevels only where a run crosses a level
    // boundary. Chunk 1's leading run covers exactly one level, so it does not.
    const auto brk = lp.write(Policy::BreakAtLevelMinimal);
    CHECK_EQ(brk.size(), cell.size());
}

void testTileNames() {
    const auto cell = synthCell();
    const auto lp = LotPack::read(cell, kLevelRange);

    const std::vector<std::string> names{
        "floors_exterior_natural_01_0", "a", "b", "c", "d", "e", "f",
        "walls_interior_house_01_11",
    };

    const auto at00 = lp.tileNamesAt(0, 0, 0, names);
    CHECK_EQ(at00.size(), std::size_t(1));
    CHECK_EQ(at00[0], std::string("walls_interior_house_01_11")); // id 7

    // Out-of-range id degrades to "?<id>" rather than throwing.
    const auto at1515 = lp.tileNamesAt(15, 15, 0, names);
    CHECK_EQ(at1515.size(), std::size_t(1));
    CHECK_EQ(at1515[0], std::string("?42"));

    const auto empty = lp.tileNamesAt(1, 1, 0, names);
    CHECK(empty.empty());
}

void testErrorPaths() {
    auto cell = synthCell();

    // Bad magic.
    {
        auto bad = cell;
        bad[0] = static_cast<std::byte>('X');
        CHECK_THROWS(LotPack::read(bad, kLevelRange));
    }

    // chunkCount not a perfect square.
    {
        LEW w;
        w.ascii("LOTP").i32(1).i32(3).i64(36).i64(40).i64(44).i32(0);
        const auto bytes = w.take();
        CHECK_THROWS(LotPack::read(bytes, kLevelRange));
    }

    // First offset != header size.
    {
        auto bad = synthCell();
        LEW w;
        w.i64(45);
        const auto patch = w.data();
        std::copy(patch.begin(), patch.end(), bad.begin() + 12);
        CHECK_THROWS(LotPack::read(bad, kLevelRange));
    }

    // A body that does not end where the next begins. Widen chunk 0's first
    // square by one tile without moving the offsets: the decode must notice.
    {
        auto bad = synthCell();
        bad[44] = static_cast<std::byte>(3); // count 2 -> 3
        const auto lp = LotPack::read(bad, kLevelRange);
        CHECK_THROWS(lp.chunk(0, 0));
    }

    // Truncated file: offsets pointing past the end.
    {
        auto truncated = synthCell();
        truncated.resize(60);
        CHECK_THROWS(LotPack::read(truncated, kLevelRange));
    }
}

} // namespace

int main() {
    testHeader();
    testDecode();
    testReEncodeIsByteIdentical();
    testFullLevelPolicyRoundTrips();
    testTileNames();
    testErrorPaths();
    return pztest::summary();
}

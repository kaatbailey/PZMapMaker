#include "celldata.hpp"
#include "lew.hpp"
#include "lotheader.hpp"
#include "mapproject.hpp"
#include "tileindex.hpp"

#include "check.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace pzformat;
namespace fs = std::filesystem;

namespace {

// A synthetic cell pair on disk: header with a few tile names, a lotpack whose
// chunk 0 has one square. Written with the confirmed full-level encoder so it
// round-trips.
LotHeader synthHeader() {
    LotHeader h;
    h.b42 = true; h.version = 1;
    h.levelsAbove = 8; h.levelsBelow = 8;
    h.minLevel = 0; h.unknown12 = 7;
    h.tileNames = {"floor_0", "wall_0"};
    h.chunkGrid.assign(static_cast<std::size_t>(kGridBytes), std::byte{0});
    h.fullyConsumed = true;
    return h;
}

std::vector<std::byte> synthPack() {
    const int chunkCount = 4, levels = 8, spl = kSquaresPerLevel;
    auto body = [&](bool put) {
        LEW w; std::int32_t run = 0;
        for (int sq = 0; sq < levels * spl; ++sq) {
            if (put && sq == 0) { if (run) { w.i32(-1).i32(run); run = 0; } w.i32(2).i32(1).i32(0); }
            else ++run;
        }
        if (run) w.i32(-1).i32(run);
        return w.take();
    };
    std::vector<std::vector<std::byte>> bodies;
    bodies.push_back(body(true));
    for (int i = 1; i < chunkCount; ++i) bodies.push_back(body(false));
    LEW w;
    w.ascii("LOTP").i32(1).i32(chunkCount);
    std::int64_t off = 12 + 8 * chunkCount;
    for (auto& b : bodies) { w.i64(off); off += static_cast<std::int64_t>(b.size()); }
    for (auto& b : bodies) w.bytes(b);
    return w.take();
}

void writeCell(const fs::path& dir, int x, int y) {
    const std::string base = std::to_string(x) + "_" + std::to_string(y);
    const auto hb = synthHeader().write();
    const auto pb = synthPack();
    std::ofstream(dir / (base + ".lotheader"), std::ios::binary)
        .write(reinterpret_cast<const char*>(hb.data()), static_cast<std::streamsize>(hb.size()));
    std::ofstream(dir / ("world_" + base + ".lotpack"), std::ios::binary)
        .write(reinterpret_cast<const char*>(pb.data()), static_cast<std::streamsize>(pb.size()));
}

fs::path makeMap(const std::string& tag, std::vector<std::pair<int,int>> coords) {
    const fs::path dir = fs::temp_directory_path() / ("pzmm_test_" + tag);
    fs::remove_all(dir);
    fs::create_directories(dir);
    for (auto [x, y] : coords) writeCell(dir, x, y);
    return dir;
}

void testEnumeration() {
    const auto dir = makeMap("enum", {{35,35},{35,36},{36,35}});
    // A stray lotheader with no matching lotpack must NOT count as a cell.
    std::ofstream(dir / "99_99.lotheader") << "junk";
    // A stray lotpack with no header must NOT count either.
    std::ofstream(dir / "world_88_88.lotpack") << "junk";

    TileIndex ti;
    MapProject p = MapProject::open(dir, ti);
    CHECK_EQ(p.cells().size(), std::size_t(3));
    CHECK(p.hasCell({35, 35}));
    CHECK(p.hasCell({36, 35}));
    CHECK(!p.hasCell({99, 99}));   // header only
    CHECK(!p.hasCell({88, 88}));   // pack only
    CHECK(!p.hasCell({0, 0}));

    // Sorted order.
    CHECK(p.cells()[0] == CellCoord{35, 35});
    CHECK(p.cells()[1] == CellCoord{35, 36});
    CHECK(p.cells()[2] == CellCoord{36, 35});

    fs::remove_all(dir);
}

void testOpenEmptyThrows() {
    const fs::path dir = fs::temp_directory_path() / "pzmm_test_empty";
    fs::remove_all(dir); fs::create_directories(dir);
    TileIndex ti;
    bool threw = false;
    try { MapProject::open(dir, ti); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    fs::remove_all(dir);
}

void testLoadAndResidency() {
    const auto dir = makeMap("load", {{10,10},{10,11}});
    TileIndex ti;
    MapProject p = MapProject::open(dir, ti);

    CHECK_EQ(p.residentCount(), std::size_t(0));
    LoadedCell& lc = p.load({10, 10});
    CHECK(lc.coord == CellCoord{10, 10});
    CHECK(lc.data != nullptr);
    CHECK(lc.editor != nullptr);
    CHECK(!lc.dirty);
    CHECK(p.isResident({10, 10}));
    CHECK_EQ(p.residentCount(), std::size_t(1));

    // Loading again returns the same resident object, not a reload.
    LoadedCell& again = p.load({10, 10});
    CHECK(&again == &lc);
    CHECK_EQ(p.residentCount(), std::size_t(1));

    // Loading a non-cell throws.
    bool threw = false;
    try { p.load({77, 77}); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    fs::remove_all(dir);
}

void testLruEvictsCleanButNotDirty() {
    // Cap of 2. Load three clean cells -> oldest evicted. Then make one dirty
    // and confirm it survives eviction pressure.
    const auto dir = makeMap("lru", {{0,0},{0,1},{0,2},{0,3}});
    TileIndex ti;
    MapProject p = MapProject::open(dir, ti);
    p.setCacheCap(2);

    p.load({0, 0});
    p.load({0, 1});
    CHECK_EQ(p.residentCount(), std::size_t(2));
    p.load({0, 2});                       // over cap -> evict LRU ({0,0})
    CHECK_EQ(p.residentCount(), std::size_t(2));
    CHECK(!p.isResident({0, 0}));         // evicted
    CHECK(p.isResident({0, 1}));
    CHECK(p.isResident({0, 2}));

    // Make {0,1} dirty, then push two more clean loads. {0,1} must NOT be
    // evicted despite being LRU-eligible, because evicting it loses edits.
    p.markDirty({0, 1});
    p.load({0, 3});
    p.load({0, 0});
    CHECK(p.isResident({0, 1}));          // dirty cell protected
    CHECK(p.anyDirty());

    fs::remove_all(dir);
}

// The core C2 guarantee: edit, save, reopen, and the edit is there. And the
// bytes on disk are exactly what the model produced.
void testEditSaveReopenNoLoss() {
    const auto dir = makeMap("roundtrip", {{5,5}});
    TileIndex ti;

    {
        MapProject p = MapProject::open(dir, ti);
        LoadedCell& lc = p.load({5, 5});
        CHECK(!lc.dirty);
        // Edit through the editor: set a new square at (1,1,0).
        lc.editor->addObject(1, 1, 0, "wall_0");
        p.markDirty({5, 5});
        CHECK(p.anyDirty());
        p.save({5, 5});
        CHECK(!p.anyDirty());              // flag cleared after save
    }

    // Reopen from scratch. The edit must be on disk.
    {
        MapProject p = MapProject::open(dir, ti);
        LoadedCell& lc = p.load({5, 5});
        CHECK(lc.data->hasSquare(1, 1, 0));
        const auto names = lc.data->tileNamesAt(1, 1, 0);
        bool found = false;
        for (auto& n : names) if (n == "wall_0") found = true;
        CHECK(found);
    }

    fs::remove_all(dir);
}

// saveAll flushes every dirty cell and leaves none dirty.
void testSaveAll() {
    const auto dir = makeMap("saveall", {{1,1},{1,2},{1,3}});
    TileIndex ti;
    MapProject p = MapProject::open(dir, ti);

    p.load({1, 1}); p.load({1, 2}); p.load({1, 3});
    p.load({1, 1}).editor->addObject(0, 0, 0, "wall_0"); p.markDirty({1, 1});
    p.load({1, 3}).editor->addObject(0, 0, 0, "wall_0"); p.markDirty({1, 3});

    CHECK_EQ(p.dirtyCells().size(), std::size_t(2));
    const int wrote = p.saveAll();
    CHECK_EQ(wrote, 2);
    CHECK(!p.anyDirty());

    fs::remove_all(dir);
}

// A crash mid-write must not corrupt the existing file: save writes to .tmp then
// renames, so the target is never partially written. We can at least confirm no
// .tmp is left behind after a successful save.
void testSaveLeavesNoTemp() {
    const auto dir = makeMap("atomic", {{2,2}});
    TileIndex ti;
    MapProject p = MapProject::open(dir, ti);
    p.load({2, 2}).editor->addObject(0, 0, 0, "wall_0");
    p.markDirty({2, 2});
    p.save({2, 2});

    bool anyTmp = false;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.path().extension() == ".tmp") anyTmp = true;
    }
    CHECK(!anyTmp);

    fs::remove_all(dir);
}

} // namespace

int main() {
    testEnumeration();
    testOpenEmptyThrows();
    testLoadAndResidency();
    testLruEvictsCleanButNotDirty();
    testEditSaveReopenNoLoss();
    testSaveAll();
    testSaveLeavesNoTemp();
    return pztest::summary();
}

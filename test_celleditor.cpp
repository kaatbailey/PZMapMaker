#include "celldata.hpp"
#include "celleditor.hpp"
#include "lotheader.hpp"
#include "tile.hpp"
#include "tileindex.hpp"

#include "check.hpp"

#include <string>
#include <vector>

using namespace pzformat;

namespace {

Tile mk(const std::string& name, std::vector<std::pair<std::string, std::string>> props) {
    Tile t;
    t.name = name;
    for (auto& [k, v] : props) t.props.put(k, v);
    return t;
}

TileIndex vocab() {
    TileIndex ti;
    ti.add(mk("floor_a", {{"attachedFloor", ""}}));
    ti.add(mk("floor_b", {{"attachedFloor", ""}}));
    ti.add(mk("wallN", {{"WallN", ""}, {"wall", ""}}));
    ti.add(mk("wallW", {{"WallW", ""}, {"wall", ""}}));
    ti.add(mk("doorN", {{"doorN", ""}}));                 // door leaf fixture
    ti.add(mk("sofa", {{"Facing", "S"}}));                // plain object
    ti.add(mk("grime", {{"FloorOverlay", ""}}));          // overlay, not a floor
    return ti;
}

CellData oneSquareCell() {
    LotHeader h = CellData::newHeader(
        {"floor_a", "floor_b", "wallN", "wallW", "doorN", "sofa", "grime"}, 0, 7);
    return CellData::blank(std::move(h), 4);
}

std::vector<std::string> namesAt(const CellData& c, int x, int y, int z) {
    return c.tileNamesAt(x, y, z);
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& e : v) if (e == s) return true;
    return false;
}

// THE core guarantee: replacing the floor leaves the wall and object in place.
// This is the "holes through houses" bug that motivated the whole file.
void testSetFloorIsSurgical() {
    CellData c = oneSquareCell();
    const TileIndex ti = vocab();
    // Square starts with a floor, a north wall, and a sofa.
    c.setSquare(5, 5, 0, {c.tileIndex("floor_a"), c.tileIndex("wallN"),
                          c.tileIndex("sofa")}, 3);

    CellEditor ed(c, ti);
    ed.setFloor(5, 5, 0, "floor_b");

    const auto names = namesAt(c, 5, 5, 0);
    CHECK(contains(names, "floor_b"));   // floor replaced
    CHECK(!contains(names, "floor_a"));  // old floor gone
    CHECK(contains(names, "wallN"));     // wall survived
    CHECK(contains(names, "sofa"));      // object survived
    CHECK_EQ(c.roomAt(5, 5, 0), 3);      // room id untouched
}

void testSetFloorOnEmptyPrepends() {
    CellData c = oneSquareCell();
    const TileIndex ti = vocab();
    // A square with only a wall, no floor.
    c.setSquare(5, 5, 0, {c.tileIndex("wallN")}, -1);

    CellEditor ed(c, ti);
    ed.setFloor(5, 5, 0, "floor_a");

    const auto t = c.tilesAt(5, 5, 0);
    CHECK_EQ(t.size(), std::size_t(2));
    // Floor drew beneath, so it goes first.
    CHECK_EQ(c.tileNamesAt(5, 5, 0)[0], std::string("floor_a"));
    CHECK_EQ(c.tileNamesAt(5, 5, 0)[1], std::string("wallN"));
}

// An overlay is NOT a floor: setFloor must not replace grime.
void testSetFloorIgnoresOverlay() {
    CellData c = oneSquareCell();
    const TileIndex ti = vocab();
    c.setSquare(5, 5, 0, {c.tileIndex("floor_a"), c.tileIndex("grime")}, -1);

    CellEditor ed(c, ti);
    ed.setFloor(5, 5, 0, "floor_b");

    const auto names = namesAt(c, 5, 5, 0);
    CHECK(contains(names, "floor_b"));
    CHECK(contains(names, "grime"));    // overlay preserved
    CHECK(!contains(names, "floor_a"));
}

void testSetWallReplacesOneEdge() {
    CellData c = oneSquareCell();
    const TileIndex ti = vocab();
    c.setSquare(5, 5, 0, {c.tileIndex("floor_a"), c.tileIndex("wallN"),
                          c.tileIndex("wallW")}, -1);

    CellEditor ed(c, ti);
    // Replacing the north wall with itself is a no-op on content but proves the
    // west edge and floor are never touched by a north-edge operation.
    ed.setWall(5, 5, 0, TileIndex::Edge::North, "wallN");

    const auto names = namesAt(c, 5, 5, 0);
    CHECK(contains(names, "wallN"));
    CHECK(contains(names, "wallW"));    // west edge untouched
    CHECK(contains(names, "floor_a"));  // floor untouched

    // Now remove the north wall and confirm only it goes.
    ed.removeWall(5, 5, 0, TileIndex::Edge::North);
    const auto after = namesAt(c, 5, 5, 0);
    CHECK(!contains(after, "wallN"));
    CHECK(contains(after, "wallW"));
    CHECK(contains(after, "floor_a"));
}

void testRemoveWallTakesFixtureWithIt() {
    CellData c = oneSquareCell();
    const TileIndex ti = vocab();
    // North wall plus a door leaf mounted in it, plus a west wall and floor.
    c.setSquare(5, 5, 0, {c.tileIndex("floor_a"), c.tileIndex("wallN"),
                          c.tileIndex("doorN"), c.tileIndex("wallW")}, -1);

    CellEditor ed(c, ti);
    ed.removeWall(5, 5, 0, TileIndex::Edge::North);

    const auto names = namesAt(c, 5, 5, 0);
    CHECK(!contains(names, "wallN"));   // wall removed
    CHECK(!contains(names, "doorN"));   // fixture in that edge removed too
    CHECK(contains(names, "wallW"));    // other edge survives
    CHECK(contains(names, "floor_a"));  // floor survives
}

void testAddAndClearObjects() {
    CellData c = oneSquareCell();
    const TileIndex ti = vocab();
    c.setSquare(5, 5, 0, {c.tileIndex("floor_a"), c.tileIndex("wallN")}, -1);

    CellEditor ed(c, ti);
    ed.addObject(5, 5, 0, "sofa");
    CHECK(contains(namesAt(c, 5, 5, 0), "sofa"));

    ed.clearObjects(5, 5, 0);
    const auto names = namesAt(c, 5, 5, 0);
    CHECK(!contains(names, "sofa"));    // object cleared
    CHECK(contains(names, "floor_a"));  // floor kept
    CHECK(contains(names, "wallN"));    // wall kept
}

// Undo must restore the square byte-for-byte, including room id, and redo must
// reapply. This is the uniform-journal guarantee.
void testUndoRedoFidelity() {
    CellData c = oneSquareCell();
    const TileIndex ti = vocab();
    c.setSquare(5, 5, 0, {c.tileIndex("floor_a"), c.tileIndex("wallN")}, 7);

    const auto before = c.tileNamesAt(5, 5, 0);
    const int beforeRoom = c.roomAt(5, 5, 0);

    CellEditor ed(c, ti);
    CHECK(!ed.canUndo());
    ed.setFloor(5, 5, 0, "floor_b");
    CHECK(ed.canUndo());
    CHECK(!ed.canRedo());

    ed.undo();
    CHECK_EQ(c.tileNamesAt(5, 5, 0), before);      // exact restore
    CHECK_EQ(c.roomAt(5, 5, 0), beforeRoom);
    CHECK(ed.canRedo());

    ed.redo();
    CHECK(contains(c.tileNamesAt(5, 5, 0), "floor_b"));
    CHECK(!ed.canRedo());
}

// clearSquare then undo must bring the whole square back (present again).
void testClearAndUndoRestoresPresence() {
    CellData c = oneSquareCell();
    const TileIndex ti = vocab();
    c.setSquare(5, 5, 0, {c.tileIndex("floor_a"), c.tileIndex("sofa")}, 4);

    CellEditor ed(c, ti);
    ed.clearSquare(5, 5, 0);
    CHECK(!c.hasSquare(5, 5, 0));

    ed.undo();
    CHECK(c.hasSquare(5, 5, 0));
    CHECK_EQ(c.roomAt(5, 5, 0), 4);
    CHECK(contains(c.tileNamesAt(5, 5, 0), "sofa"));
}

// A grouped fill undoes in ONE step, not w*h steps.
void testGroupedFillUndoesAsOne() {
    CellData c = oneSquareCell();
    const TileIndex ti = vocab();

    CellEditor ed(c, ti);
    const auto edit = ed.fillFloor(2, 2, 3, 3, 0, "floor_a");
    CHECK_EQ(edit.squaresTouched(), 9);
    CHECK_EQ(ed.undoDepth(), 1);   // one undo entry for the whole rectangle

    for (int x = 2; x < 5; ++x)
        for (int y = 2; y < 5; ++y)
            CHECK(contains(c.tileNamesAt(x, y, 0), "floor_a"));

    ed.undo();
    CHECK_EQ(ed.undoDepth(), 0);
    for (int x = 2; x < 5; ++x)
        for (int y = 2; y < 5; ++y)
            CHECK(!c.hasSquare(x, y, 0));   // all nine gone in one undo
}

// A new edit after undo clears the redo stack.
void testNewEditClearsRedo() {
    CellData c = oneSquareCell();
    const TileIndex ti = vocab();
    c.setSquare(5, 5, 0, {c.tileIndex("floor_a")}, -1);

    CellEditor ed(c, ti);
    ed.setFloor(5, 5, 0, "floor_b");
    ed.undo();
    CHECK(ed.canRedo());
    ed.addObject(5, 5, 0, "sofa"); // a genuine fresh edit, not a no-op
    CHECK(!ed.canRedo());            // redo history discarded
}

// A no-op edit records nothing.
void testNoOpRecordsNothing() {
    CellData c = oneSquareCell();
    const TileIndex ti = vocab();
    c.setSquare(5, 5, 0, {c.tileIndex("floor_a")}, 2);

    CellEditor ed(c, ti);
    ed.setFloor(5, 5, 0, "floor_a"); // already floor_a -> no change
    CHECK(!ed.canUndo());
    ed.setRoom(5, 5, 0, 2);          // already room 2 -> no change
    CHECK(!ed.canUndo());
}

} // namespace

int main() {
    testSetFloorIsSurgical();
    testSetFloorOnEmptyPrepends();
    testSetFloorIgnoresOverlay();
    testSetWallReplacesOneEdge();
    testRemoveWallTakesFixtureWithIt();
    testAddAndClearObjects();
    testUndoRedoFidelity();
    testClearAndUndoRestoresPresence();
    testGroupedFillUndoesAsOne();
    testNewEditClearsRedo();
    testNoOpRecordsNothing();
    return pztest::summary();
}

#include "celldata.hpp"
#include "lotheader.hpp"
#include "mapvalidator.hpp"
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

// Vocabulary the validator keys off.
TileIndex vocab() {
    TileIndex ti;
    ti.add(mk("floor", {{"attachedFloor", ""}}));           // FLOOR
    ti.add(mk("wallN", {{"WallN", ""}, {"wall", ""}}));
    ti.add(mk("wallW", {{"WallW", ""}, {"wall", ""}}));
    ti.add(mk("wallNW", {{"WallNW", ""}, {"wall", ""}}));    // corner: both edges
    ti.add(mk("doorN", {{"DoorWallN", ""}, {"wall", ""}})); // wall with a door frame
    return ti;
}

// A cell whose header carries one room rect, with tiles placed by the test.
// Room is an axis-aligned rectangle [rx,ry,rw,rh] at z=0.
CellData cellWithRoom(const std::string& roomName, int rx, int ry, int rw, int rh) {
    LotHeader h = CellData::newHeader({"floor", "wallN", "wallW", "doorN"}, 0, 7);
    Room room;
    room.name = roomName;
    room.floor = 0;
    room.rects.push_back({rx, ry, rw, rh});
    h.rooms.push_back(room);
    return CellData::blank(std::move(h), 4); // 32x32 cell
}

// Helpers to stamp tiles by name onto a square (room id 1 unless told otherwise).
void put(CellData& c, int x, int y, std::vector<std::string> names, int roomId = 1) {
    std::vector<std::int32_t> idx;
    for (auto& n : names) idx.push_back(c.tileIndex(n));
    c.setSquare(x, y, 0, std::move(idx), roomId);
}

bool hasFinding(const MapValidator::Report& r, MapValidator::Severity sev,
                std::string_view needle) {
    for (const auto& f : r.findings) {
        if (f.severity == sev && f.message.find(needle) != std::string::npos) return true;
    }
    return false;
}

// Fill the interior of a 1-square-border room with floor + room membership,
// and lay walls on the full perimeter. leaveDoorAt optionally converts one
// north-edge square to a doorN wall.
void buildClosedRoom(CellData& c, int rx, int ry, int rw, int rh,
                     bool withDoor, bool withFloor = true, int roomId = 1) {
    // Floors + membership across the interior.
    for (int y = ry; y < ry + rh; ++y)
        for (int x = rx; x < rx + rw; ++x)
            put(c, x, y, withFloor ? std::vector<std::string>{"floor"}
                                   : std::vector<std::string>{}, roomId);

    // Perimeter walls: north edge at ry, west edge at rx, plus the far edges at
    // ry+rh and rx+rw (walls belong to the square on the high side).
    for (int x = rx; x < rx + rw; ++x) {
        put(c, x, ry, {"wallN", "floor"}, roomId);          // north edge
        put(c, x, ry + rh, {"wallN"}, -1);                  // south edge (outside)
    }
    for (int y = ry; y < ry + rh; ++y) {
        put(c, rx, y, {"wallW", "floor"}, roomId);          // west edge
        put(c, rx + rw, y, {"wallW"}, -1);                  // east edge (outside)
    }
    // Re-lay floor+membership on the two interior edges we just overwrote.
    for (int x = rx; x < rx + rw; ++x) put(c, x, ry, {"wallN", "floor"}, roomId);
    for (int y = ry; y < ry + rh; ++y) put(c, rx, y, {"wallW", "floor"}, roomId);
    // The NW corner needs BOTH edges — the west loop above dropped its WallN.
    // Real corner tiles carry WallNW; use it here.
    put(c, rx, ry, {"wallNW", "floor"}, roomId);

    if (withDoor) {
        // Convert one north-edge square to a door-bearing wall, with floor on
        // both the inside and the outside so rule 2 is satisfied.
        const int dx = rx + rw / 2;
        put(c, dx, ry, {"doorN", "floor"}, roomId);
        put(c, dx, ry - 1, {"floor"}, -1); // floor on the far (north) side
    }
}

void testSealedRoomIsError() {
    const TileIndex ti = vocab();
    CellData c = cellWithRoom("bunker", 5, 5, 4, 4);
    buildClosedRoom(c, 5, 5, 4, 4, /*withDoor=*/false);

    const auto rep = MapValidator::validate(ti, c);
    CHECK_EQ(rep.roomsChecked, 1);
    CHECK(hasFinding(rep, MapValidator::Severity::Error, "sealed"));
    CHECK(rep.errors() >= 1);
}

void testRoomWithDoorPasses() {
    const TileIndex ti = vocab();
    CellData c = cellWithRoom("kitchen", 5, 5, 4, 4);
    buildClosedRoom(c, 5, 5, 4, 4, /*withDoor=*/true);

    const auto rep = MapValidator::validate(ti, c);
    CHECK_EQ(rep.roomsChecked, 1);
    // A door is present, so NOT sealed. No "sealed" error.
    CHECK(!hasFinding(rep, MapValidator::Severity::Error, "sealed"));
    // Floors are complete and members stamped, so no floor/membership warnings.
    CHECK(!hasFinding(rep, MapValidator::Severity::Warning, "no floor"));
    CHECK(!hasFinding(rep, MapValidator::Severity::Warning, "not stamped"));
}

void testDoorwayOntoVoid() {
    const TileIndex ti = vocab();
    CellData c = cellWithRoom("porch", 5, 5, 4, 4);
    buildClosedRoom(c, 5, 5, 4, 4, /*withDoor=*/true);
    // Remove the floor on the far side of the door so rule 2 fires.
    const int dx = 5 + 4 / 2;
    c.clearSquare(dx, 5 - 1, 0);

    const auto rep = MapValidator::validate(ti, c);
    CHECK(hasFinding(rep, MapValidator::Severity::Warning, "missing floor"));
}

void testMissingFloor() {
    const TileIndex ti = vocab();
    CellData c = cellWithRoom("empty", 5, 5, 4, 4);
    buildClosedRoom(c, 5, 5, 4, 4, /*withDoor=*/true, /*withFloor=*/false);

    const auto rep = MapValidator::validate(ti, c);
    CHECK(hasFinding(rep, MapValidator::Severity::Warning, "no floor"));
}

void testRoomMembershipMismatch() {
    const TileIndex ti = vocab();
    CellData c = cellWithRoom("mislabeled", 5, 5, 4, 4);
    buildClosedRoom(c, 5, 5, 4, 4, /*withDoor=*/true, /*withFloor=*/true, /*roomId=*/-1);

    const auto rep = MapValidator::validate(ti, c);
    CHECK(hasFinding(rep, MapValidator::Severity::Warning, "not stamped"));
}

void testEmptyOutsideSkipped() {
    const TileIndex ti = vocab();
    CellData c = cellWithRoom("emptyoutside", 5, 5, 4, 4);
    const auto rep = MapValidator::validate(ti, c);
    CHECK_EQ(rep.roomsChecked, 0); // emptyoutside is not an interior room
    CHECK(rep.clean());
}

void testUpperFloorNoDoorIsWarning() {
    const TileIndex ti = vocab();
    // Same sealed room but at z=1: a missing door is a warning (stairs), not an
    // error. Build the room at floor 1.
    LotHeader h = CellData::newHeader({"floor", "wallN", "wallW", "doorN"}, 0, 7);
    Room room; room.name = "attic"; room.floor = 1;
    room.rects.push_back({5, 5, 4, 4});
    h.rooms.push_back(room);
    CellData c = CellData::blank(std::move(h), 4);

    // Walls all around at z=1, no door, floors present.
    for (int y = 5; y < 9; ++y) for (int x = 5; x < 9; ++x) {
        std::vector<std::int32_t> f{c.tileIndex("floor")};
        c.setSquare(x, y, 1, f, 1);
    }
    for (int x = 5; x < 9; ++x) {
        c.setSquare(x, 5, 1, {c.tileIndex("wallN"), c.tileIndex("floor")}, 1);
        c.setSquare(x, 9, 1, {c.tileIndex("wallN")}, -1);
    }
    for (int y = 5; y < 9; ++y) {
        c.setSquare(5, y, 1, {c.tileIndex("wallW"), c.tileIndex("floor")}, 1);
        c.setSquare(9, y, 1, {c.tileIndex("wallW")}, -1);
    }
    // NW corner needs both edges.
    c.setSquare(5, 5, 1, {c.tileIndex("wallNW"), c.tileIndex("floor")}, 1);

    const auto rep = MapValidator::validate(ti, c);
    CHECK(!hasFinding(rep, MapValidator::Severity::Error, "sealed"));
    CHECK(hasFinding(rep, MapValidator::Severity::Warning, "may connect by stairs"));
}

} // namespace

int main() {
    testSealedRoomIsError();
    testRoomWithDoorPasses();
    testDoorwayOntoVoid();
    testMissingFloor();
    testRoomMembershipMismatch();
    testEmptyOutsideSkipped();
    testUpperFloorNoDoorIsWarning();
    return pztest::summary();
}

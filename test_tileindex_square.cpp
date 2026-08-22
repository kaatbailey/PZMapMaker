#include "celldata.hpp"
#include "lew.hpp"
#include "lotheader.hpp"
#include "square.hpp"
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

// A hand-built TileIndex with the real property vocabulary, one tile per case.
TileIndex buildIndex() {
    TileIndex ti;
    ti.add(mk("floors_exterior_natural_01_0", {{"attachedFloor", ""}}));
    ti.add(mk("blends_natural_01_64", {}));                       // floor by name prefix
    ti.add(mk("walls_interior_house_01_0", {{"WallN", ""}, {"wall", ""}}));
    ti.add(mk("walls_interior_house_01_1", {{"WallW", ""}, {"wall", ""}}));
    ti.add(mk("walls_interior_house_01_2", {{"WallNW", ""}, {"wall", ""}}));
    ti.add(mk("walls_doorframe_01_0", {{"DoorWallN", ""}, {"wall", ""}}));   // doorway wall
    ti.add(mk("walls_window_01_0", {{"WindowN", ""}, {"wall", ""}, {"WindowShape", "1"}}));
    ti.add(mk("fixtures_doors_01_0", {{"doorN", ""}}));          // door leaf (fixture)
    ti.add(mk("fixtures_windows_01_0", {{"windowW", ""}}));      // glass pane (fixture)
    ti.add(mk("overlay_grime_01_0", {{"WallOverlay", ""}, {"attachedN", ""}})); // decoration
    ti.add(mk("f_grime_floor_01_0", {{"FloorOverlay", ""}}));    // floor overlay
    ti.add(mk("vegetation_foliage_01_3", {{"tree", ""}, {"MoveWithWind", ""}}));
    ti.add(mk("location_shop_generic_01_8", {{"container", "shelves"}}));
    ti.add(mk("fencing_01_4", {{"solidtrans", ""}}));            // see-through blocker
    ti.add(mk("furniture_seating_01_2", {{"Facing", "S"}, {"solid", ""}}));
    return ti;
}

void testKindOf() {
    const TileIndex ti = buildIndex();
    using K = TileIndex::Kind;
    CHECK(ti.kindOf("floors_exterior_natural_01_0") == K::Floor);
    CHECK(ti.kindOf("blends_natural_01_64") == K::Floor);        // by name prefix
    CHECK(ti.kindOf("walls_interior_house_01_0") == K::Wall);
    CHECK(ti.kindOf("walls_doorframe_01_0") == K::Door);         // DoorWallN wins
    CHECK(ti.kindOf("walls_window_01_0") == K::Window);          // WindowShape wins
    CHECK(ti.kindOf("fixtures_doors_01_0") == K::Door);          // doorN
    CHECK(ti.kindOf("vegetation_foliage_01_3") == K::Vegetation);
    CHECK(ti.kindOf("location_shop_generic_01_8") == K::Object);
    CHECK(ti.kindOf("does_not_exist_99") == K::Unknown);
}

void testEdgeOf() {
    const TileIndex ti = buildIndex();
    using E = TileIndex::Edge;
    CHECK(ti.edgeOf("walls_interior_house_01_0") == E::North);
    CHECK(ti.edgeOf("walls_interior_house_01_1") == E::West);
    CHECK(ti.edgeOf("walls_interior_house_01_2") == E::Both);   // WallNW
    CHECK(ti.edgeOf("walls_doorframe_01_0") == E::North);       // DoorWallN
    CHECK(ti.edgeOf("fixtures_doors_01_0") == E::North);        // doorN fixture
    CHECK(ti.edgeOf("fixtures_windows_01_0") == E::West);       // windowW fixture

    // THE lesson: a grime overlay carries attachedN like a wall, but edgeOf
    // must NOT report an edge for it, or wall-joining derails. This is the
    // 99.5%-correlated-proxy bug the split prevents.
    CHECK(ti.edgeOf("overlay_grime_01_0") == E::None);
    CHECK(ti.decorationEdge("overlay_grime_01_0") == E::North); // the attached side lives here
}

void testWallDiscrimination() {
    const TileIndex ti = buildIndex();
    CHECK(ti.isStructuralWall("walls_interior_house_01_0"));
    CHECK(ti.isStructuralWall("walls_doorframe_01_0"));
    CHECK(!ti.isStructuralWall("fixtures_doors_01_0")); // a fixture, not the wall
    CHECK(!ti.isStructuralWall("overlay_grime_01_0"));  // an overlay, not the wall

    CHECK(ti.isWallFixture("fixtures_doors_01_0"));
    CHECK(ti.isWallFixture("fixtures_windows_01_0"));
    CHECK(!ti.isWallFixture("walls_interior_house_01_0"));

    CHECK(ti.isDoorway("walls_doorframe_01_0"));
    CHECK(!ti.isDoorway("walls_interior_house_01_0"));
    CHECK(ti.isWindowWall("walls_window_01_0"));

    CHECK(ti.isOverlay("overlay_grime_01_0"));
    CHECK(ti.isOverlay("f_grime_floor_01_0"));           // FloorOverlay
    CHECK(ti.isOverlay("overlay_anything_by_prefix_0")); // name prefix, even if unknown
    CHECK(!ti.isOverlay("walls_interior_house_01_0"));

    CHECK(ti.blocksMovement("furniture_seating_01_2")); // solid
    CHECK(ti.blocksMovement("fencing_01_4"));           // solidtrans
    CHECK(!ti.blocksMovement("floors_exterior_natural_01_0"));

    CHECK(ti.containerType("location_shop_generic_01_8") == "shelves");
    CHECK(!ti.containerType("floors_exterior_natural_01_0").has_value());
    CHECK(ti.facing("furniture_seating_01_2") == "S");
}

// Assemble a real square from tiles and confirm the semantic split: floor,
// north wall, a door fixture, and a grime overlay all coexist without one
// stealing another's slot.
void testSquareAssembly() {
    const TileIndex ti = buildIndex();

    // Build a one-square cell carrying: floor + north wall + door leaf + grime.
    LotHeader h = CellData::newHeader(
        {"floors_exterior_natural_01_0", "walls_interior_house_01_0",
         "fixtures_doors_01_0", "overlay_grime_01_0"},
        0, 7);
    CellData cell = CellData::blank(std::move(h), 4); // 32x32
    cell.setSquare(5, 6, 0, {0, 1, 2, 3}, 42);

    const Square s = Square::at(cell, ti, 5, 6, 0);
    CHECK(!s.isEmpty());
    CHECK_EQ(s.roomId, 42);
    CHECK(s.indoors());
    CHECK(s.floor == "floors_exterior_natural_01_0");
    CHECK(s.hasWall());
    CHECK(s.northWall == "walls_interior_house_01_0");
    CHECK(!s.westWall.has_value());

    // The door leaf is a fixture, not the wall; it sets hasDoor but does not
    // replace northWall.
    CHECK_EQ(s.fixtures.size(), std::size_t(1));
    CHECK(s.hasDoor);
    CHECK(s.northWall == "walls_interior_house_01_0"); // still the wall, not the leaf

    // Grime is an overlay, parsed in pass 2, and never touches floor/wall slots.
    CHECK_EQ(s.overlays.size(), std::size_t(1));
    CHECK(s.overlays[0] == "overlay_grime_01_0");
    CHECK(s.floor == "floors_exterior_natural_01_0"); // overlay did not win the floor slot
}

void testEmptySquare() {
    const TileIndex ti = buildIndex();
    LotHeader h = CellData::newHeader({"floors_exterior_natural_01_0"}, 0, 7);
    CellData cell = CellData::blank(std::move(h), 4);
    const Square s = Square::at(cell, ti, 0, 0, 0);
    CHECK(s.isEmpty());
    CHECK(!s.hasWall());
    CHECK(!s.indoors());
    CHECK_EQ(s.roomId, -1);
}

} // namespace

int main() {
    testKindOf();
    testEdgeOf();
    testWallDiscrimination();
    testSquareAssembly();
    testEmptySquare();
    return pztest::summary();
}

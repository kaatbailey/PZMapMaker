#include "square.hpp"

#include "celldata.hpp"

namespace pzformat {

Square Square::at(const CellData& cell, const TileIndex& ti, int x, int y, int actualZ) {
    Square s;
    s.x = x; s.y = y; s.z = actualZ;
    s.roomId = cell.roomAt(x, y, actualZ);

    const auto names = cell.tileNamesAt(x, y, actualZ);
    if (names.empty()) return s;
    s.tiles = names;

    using Kind = TileIndex::Kind;
    using Edge = TileIndex::Edge;

    // Two passes: structural tiles first, so decoration painted onto a wall or
    // floor never wins the slot from the thing it is painted on.
    for (int pass = 0; pass < 2; ++pass) {
        const bool wantOverlay = pass == 1;
        for (const auto& n : names) {
            if (ti.isOverlay(n) != wantOverlay) continue;
            const Kind k = ti.kindOf(n);
            const Edge e = ti.edgeOf(n);
            if (ti.blocksMovement(n)) s.blocksMovement = true;
            const auto ct = ti.containerType(n);
            if (ct && !s.containerType) s.containerType = ct;

            if (wantOverlay) { s.overlays.push_back(n); continue; }

            // Fixtures (door leaf, glass pane) mount in a wall but are not the
            // wall; decoration merely hangs on it. Only structure gets the edge.
            if (ti.isWallFixture(n)) {
                s.fixtures.push_back(n);
                if (k == Kind::Door)   s.hasDoor = true;
                if (k == Kind::Window) s.hasWindow = true;
                continue;
            }
            if ((k == Kind::Wall || k == Kind::Door || k == Kind::Window)
                && !ti.isStructuralWall(n)) {
                s.objects.push_back(n);
                continue;
            }

            switch (k) {
                case Kind::Floor:
                    if (!s.floor) s.floor = n;
                    break;
                case Kind::Wall:
                case Kind::Door:
                case Kind::Window: {
                    const bool doorway = ti.isDoorway(n);
                    const bool windowWall = ti.isWindowWall(n);
                    if (doorway) s.hasDoor = true;
                    if (windowWall) s.hasWindow = true;
                    switch (e) {
                        case Edge::North:
                            if (!s.northWall) s.northWall = n;
                            s.northIsDoorway |= doorway;
                            s.northIsWindow  |= windowWall;
                            break;
                        case Edge::West:
                            if (!s.westWall) s.westWall = n;
                            s.westIsDoorway |= doorway;
                            s.westIsWindow  |= windowWall;
                            break;
                        case Edge::Both:
                            if (!s.northWall) s.northWall = n;
                            if (!s.westWall)  s.westWall = n;
                            s.northIsDoorway |= doorway; s.westIsDoorway |= doorway;
                            s.northIsWindow  |= windowWall; s.westIsWindow |= windowWall;
                            break;
                        case Edge::None:
                            s.objects.push_back(n);
                            break;
                    }
                    break;
                }
                case Kind::Vegetation:
                    s.vegetation.push_back(n);
                    break;
                default:
                    s.objects.push_back(n);
                    break;
            }
        }
    }
    return s;
}

std::string Square::toString() const {
    std::string sb = "(" + std::to_string(x) + "," + std::to_string(y)
                   + ",z" + std::to_string(z) + ")";
    if (roomId >= 0) sb += " room=" + std::to_string(roomId);
    if (floor) sb += " floor=" + *floor;
    if (northWall) {
        sb += " N=" + *northWall
            + (northIsDoorway ? "[door]" : northIsWindow ? "[window]" : "");
    }
    if (westWall) {
        sb += " W=" + *westWall
            + (westIsDoorway ? "[door]" : westIsWindow ? "[window]" : "");
    }
    if (!fixtures.empty())  sb += " fixtures=" + std::to_string(fixtures.size());
    if (containerType)      sb += " container=" + *containerType;
    if (hasDoor)   sb += " DOOR";
    if (hasWindow) sb += " WINDOW";
    if (!overlays.empty())  sb += " overlays=" + std::to_string(overlays.size());
    if (!objects.empty())   sb += " objects=" + std::to_string(objects.size());
    return sb;
}

} // namespace pzformat

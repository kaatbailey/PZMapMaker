#include "mapvalidator.hpp"

#include <array>
#include <set>

namespace pzformat {

namespace {
const std::set<std::string> kNotInterior = {"emptyoutside"};
} // namespace

bool MapValidator::hasTileProp(const TileIndex& ti, const CellData& c,
                               int x, int y, int z, std::string_view prop) {
    if (x < 0 || y < 0 || x >= c.cellSize() || y >= c.cellSize()) return false;
    // z out of range is not an error here — a room floor can name a level the
    // cell does not encode; treat as "no such tile".
    if (c.zIndex(z) < 0 || c.zIndex(z) >= c.levelCount()) return false;
    const auto names = c.tileNamesAt(x, y, z);
    for (const auto& name : names) {
        const Tile* t = ti.get(name);
        if (t && t->props.contains(prop)) return true;
    }
    return false;
}

bool MapValidator::hasDoorOnEdge(const TileIndex& ti, const CellData& c,
                                 int x, int y, int z, bool north) {
    return north
        ? (hasTileProp(ti, c, x, y, z, "DoorWallN") || hasTileProp(ti, c, x, y, z, "doorN"))
        : (hasTileProp(ti, c, x, y, z, "DoorWallW") || hasTileProp(ti, c, x, y, z, "doorW"));
}

bool MapValidator::hasEdge(const TileIndex& ti, const CellData& c,
                           int x, int y, int z, bool north) {
    return north
        ? (hasTileProp(ti, c, x, y, z, "WallN") || hasTileProp(ti, c, x, y, z, "DoorWallN")
           || hasTileProp(ti, c, x, y, z, "WallNW"))
        : (hasTileProp(ti, c, x, y, z, "WallW") || hasTileProp(ti, c, x, y, z, "DoorWallW")
           || hasTileProp(ti, c, x, y, z, "WallNW"));
}

bool MapValidator::hasFloor(const TileIndex& ti, const CellData& c, int x, int y, int z) {
    if (x < 0 || y < 0 || x >= c.cellSize() || y >= c.cellSize()) return false;
    if (c.zIndex(z) < 0 || c.zIndex(z) >= c.levelCount()) return false;
    const auto names = c.tileNamesAt(x, y, z);
    for (const auto& name : names) {
        if (ti.kindOf(name) == TileIndex::Kind::Floor) return true;
    }
    return false;
}

bool MapValidator::insideRoom(const Room& room, int x, int y) {
    for (const auto& r : room.rects) {
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return true;
    }
    return false;
}

MapValidator::Report MapValidator::validate(const TileIndex& ti, const CellData& c) {
    Report rep;
    const LotHeader& h = c.header();

    auto error = [&](const std::string& label, int ri, const std::string& msg) {
        rep.findings.push_back({Severity::Error, label, msg, ri});
    };
    auto warn = [&](const std::string& label, int ri, const std::string& msg) {
        rep.findings.push_back({Severity::Warning, label, msg, ri});
    };

    for (int ri = 0; ri < static_cast<int>(h.rooms.size()); ++ri) {
        const Room& room = h.rooms[static_cast<std::size_t>(ri)];
        if (kNotInterior.count(room.name)) continue;
        if (room.rects.empty()) continue;
        ++rep.roomsChecked;

        const int z = room.floor;
        const std::string label = (room.name.empty() ? "room" : room.name)
                                + " #" + std::to_string(ri) + " z=" + std::to_string(z);

        // Rule 3: wall gaps on perimeter.
        int gaps = 0;
        for (const auto& r : room.rects) {
            const int rx = r.x, ry = r.y, rw = r.w, rh = r.h;
            for (int x = rx; x < rx + rw; ++x)
                if (!hasEdge(ti, c, x, ry, z, true) && !insideRoom(room, x, ry - 1)) ++gaps;
            for (int x = rx; x < rx + rw; ++x)
                if (!hasEdge(ti, c, x, ry + rh, z, true) && !insideRoom(room, x, ry + rh)) ++gaps;
            for (int y = ry; y < ry + rh; ++y)
                if (!hasEdge(ti, c, rx, y, z, false) && !insideRoom(room, rx - 1, y)) ++gaps;
            for (int y = ry; y < ry + rh; ++y)
                if (!hasEdge(ti, c, rx + rw, y, z, false) && !insideRoom(room, rx + rw, y)) ++gaps;
        }
        if (gaps > 0) warn(label, ri, std::to_string(gaps) + " wall gap(s) on perimeter");

        // Rule 1: at least one door on the perimeter.
        struct Door { int x, y; bool north; };
        std::vector<Door> doors;
        for (const auto& r : room.rects) {
            const int rx = r.x, ry = r.y, rw = r.w, rh = r.h;
            for (int x = rx; x < rx + rw; ++x)
                if (hasDoorOnEdge(ti, c, x, ry, z, true)) doors.push_back({x, ry, true});
            for (int x = rx; x < rx + rw; ++x)
                if (hasDoorOnEdge(ti, c, x, ry + rh, z, true)) doors.push_back({x, ry + rh, true});
            for (int y = ry; y < ry + rh; ++y)
                if (hasDoorOnEdge(ti, c, rx, y, z, false)) doors.push_back({rx, y, false});
            for (int y = ry; y < ry + rh; ++y)
                if (hasDoorOnEdge(ti, c, rx + rw, y, z, false)) doors.push_back({rx + rw, y, false});
        }
        if (doors.empty() && gaps == 0) {
            // Truly sealed: no door AND no wall gap. A wall gap is a valid exit
            // (open-plan boundary), even without a door tile.
            if (z == 0) {
                error(label, ri, "sealed — no door and no wall gap on any perimeter edge");
            } else {
                warn(label, ri, "no door on perimeter (z=" + std::to_string(z)
                     + " — may connect by stairs)");
            }
        }
        // doors.empty() && gaps > 0: accessible but no closeable entrance —
        // normal for open-plan rooms (livingroom/kitchen). No finding, matching
        // the Java's empty branch.

        // Rule 2: every door must have floor on both sides.
        for (const auto& d : doors) {
            const int ox = d.north ? d.x : d.x - 1;
            const int oy = d.north ? d.y - 1 : d.y;
            const bool f1 = hasFloor(ti, c, d.x, d.y, z);
            const bool f2 = hasFloor(ti, c, ox, oy, z);
            if (!f1 || !f2) {
                warn(label, ri, "doorway at (" + std::to_string(d.x) + ","
                     + std::to_string(d.y) + ") " + (d.north ? "north" : "west")
                     + " edge — missing floor on " + ((!f1 && !f2) ? "both sides" : "one side"));
            }
        }

        // Rule 4: floor coverage.
        int noFloor = 0;
        for (const auto& r : room.rects) {
            for (int y = r.y; y < r.y + r.h; ++y)
                for (int x = r.x; x < r.x + r.w; ++x)
                    if (!hasFloor(ti, c, x, y, z)) ++noFloor;
        }
        if (noFloor > 0) {
            warn(label, ri, std::to_string(noFloor) + " interior square(s) with no floor");
        }

        // Rule 5: room membership.
        int noMember = 0;
        for (const auto& r : room.rects) {
            for (int y = r.y; y < r.y + r.h; ++y)
                for (int x = r.x; x < r.x + r.w; ++x) {
                    if (x < 0 || y < 0 || x >= c.cellSize() || y >= c.cellSize()) continue;
                    if (c.zIndex(z) < 0 || c.zIndex(z) >= c.levelCount()) continue;
                    if (c.roomAt(x, y, z) < 0) ++noMember;
                }
        }
        if (noMember > 0) {
            warn(label, ri, std::to_string(noMember)
                 + " interior square(s) not stamped with room id");
        }
    }
    return rep;
}

} // namespace pzformat

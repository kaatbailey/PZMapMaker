#include "lotheader.hpp"

#include "lew.hpp"

#include <stdexcept>

namespace pzformat {

std::size_t LotHeader::roomRefs() const noexcept {
    std::size_t n = 0;
    for (const auto& b : buildings) n += b.size();
    return n;
}

void LotHeader::readNameTable(LE& r, LotHeader& h) {
    const std::int32_t tileCount = r.i32();
    if (tileCount < 0 || tileCount > 500'000) {
        throw ParseError("implausible tile count " + std::to_string(tileCount)
                         + " at offset " + std::to_string(r.pos() - 4));
    }
    h.tileNames.reserve(static_cast<std::size_t>(tileCount));
    for (std::int32_t i = 0; i < tileCount; ++i) h.tileNames.push_back(r.cString());
}

LotHeader LotHeader::read(std::span<const std::byte> data) {
    LotHeader h;
    LE r(data);

    const auto magic = r.view(4);
    h.b42 = magic[0] == static_cast<std::byte>('L')
         && magic[1] == static_cast<std::byte>('O')
         && magic[2] == static_cast<std::byte>('T')
         && magic[3] == static_cast<std::byte>('H');

    if (h.b42) {
        h.version = r.i32();
        readNameTable(r, h);
        h.trailerOffset = r.pos();
        h.hasTrailerOffset = true;
        readB42Meta(r, h);
        return h;
    }

    // B41: the 4 bytes we consumed were the version field.
    r.seek(0);
    h.version = r.i32();
    readNameTable(r, h);
    h.trailerOffset = r.pos();
    h.hasTrailerOffset = true;
    readB41Meta(r, h);
    return h;
}

// ---------------------------------------------------------------- B42 -------

void LotHeader::readB42Meta(LE& r, LotHeader& h) {
    h.levelsAbove = r.i32();
    h.levelsBelow = r.i32();
    h.minLevel    = r.i32();
    h.unknown12   = r.i32();

    const std::int32_t roomCount = r.i32();
    if (roomCount < 0 || roomCount > 200'000) {
        throw ParseError("roomCount " + std::to_string(roomCount)
                         + " at " + std::to_string(r.pos() - 4));
    }
    h.rooms.reserve(static_cast<std::size_t>(roomCount));
    for (std::int32_t i = 0; i < roomCount; ++i) {
        Room room;
        room.name  = r.cString();
        room.floor = r.i32();

        const std::int32_t rectCount = r.i32();
        if (rectCount < 0 || rectCount > 5000) {
            throw ParseError("room " + std::to_string(i) + " rectCount "
                             + std::to_string(rectCount));
        }
        room.rects.reserve(static_cast<std::size_t>(rectCount));
        for (std::int32_t j = 0; j < rectCount; ++j) {
            Rect rc;
            rc.x = r.i32(); rc.y = r.i32(); rc.w = r.i32(); rc.h = r.i32();
            room.rects.push_back(rc);
        }

        const std::int32_t objCount = r.i32();
        if (objCount < 0 || objCount > 50'000) {
            throw ParseError("room " + std::to_string(i) + " objCount "
                             + std::to_string(objCount));
        }
        room.objects.reserve(static_cast<std::size_t>(objCount));
        for (std::int32_t j = 0; j < objCount; ++j) {
            RoomObject ob;
            ob.a = r.i32(); ob.b = r.i32(); ob.c = r.i32();
            room.objects.push_back(ob);
        }
        h.rooms.push_back(std::move(room));
    }

    const std::int32_t buildingCount = r.i32();
    if (buildingCount < 0 || buildingCount > 200'000) {
        throw ParseError("buildingCount " + std::to_string(buildingCount)
                         + " at " + std::to_string(r.pos() - 4));
    }
    h.buildings.reserve(static_cast<std::size_t>(buildingCount));
    for (std::int32_t i = 0; i < buildingCount; ++i) {
        const std::int32_t n = r.i32();
        if (n < 0 || n > 50'000) {
            throw ParseError("building " + std::to_string(i) + " roomCount "
                             + std::to_string(n));
        }
        std::vector<std::int32_t> idx;
        idx.reserve(static_cast<std::size_t>(n));
        for (std::int32_t j = 0; j < n; ++j) {
            const std::int32_t v = r.i32();
            if (v < 0 || v >= roomCount) {
                throw ParseError("building " + std::to_string(i) + " room index "
                                 + std::to_string(v) + " out of range (roomCount="
                                 + std::to_string(roomCount) + ")");
            }
            idx.push_back(v);
        }
        h.buildings.push_back(std::move(idx));
    }

    // The check that makes the trailer layout falsifiable: after buildings,
    // exactly the grid must remain. Any other number and a field is missing,
    // extra, or the wrong width.
    if (r.remaining() != static_cast<std::size_t>(kGridBytes)) {
        throw ParseError("expected exactly " + std::to_string(kGridBytes)
                         + " grid bytes after buildings, found "
                         + std::to_string(r.remaining()));
    }
    h.chunkGrid = r.bytes(static_cast<std::size_t>(kGridBytes));
    h.fullyConsumed = r.eof();
}

// ---------------------------------------------------------------- B41 -------

void LotHeader::readB41Meta(LE& r, LotHeader& h) {
    std::int32_t found = -1;
    for (std::int32_t pad = 0; pad <= 8; ++pad) {
        const std::size_t probe = h.trailerOffset + static_cast<std::size_t>(pad);
        if (probe + 12 > r.length()) break;
        r.seek(probe);
        const std::int32_t w = r.i32(), ht = r.i32(), lv = r.i32();
        if ((w == 300 || w == 256) && w == ht && lv > 0 && lv <= 64) {
            found = pad;
            h.width = w; h.height = ht; h.levels = lv;
            break;
        }
    }
    if (found < 0) {
        r.seek(h.trailerOffset);
        h.trailer = r.bytes(r.remaining());
        h.warnings.emplace_back("no width/height/levels found after tile table; "
                                "trailer left raw");
        return;
    }
    h.padBytesSkipped = found;

    try {
        const std::int32_t roomCount = r.i32();
        if (roomCount < 0 || roomCount > 200'000) {
            h.warnings.push_back("implausible roomCount " + std::to_string(roomCount));
            return;
        }
        for (std::int32_t i = 0; i < roomCount; ++i) {
            Room room;
            room.name  = r.cString();
            room.floor = r.i32();

            const std::int32_t rectCount = r.i32();
            if (rectCount < 0 || rectCount > 10'000) {
                h.warnings.push_back("room '" + room.name + "': bad rectCount "
                                     + std::to_string(rectCount));
                return;
            }
            for (std::int32_t j = 0; j < rectCount; ++j) {
                Rect rc;
                rc.x = r.i32(); rc.y = r.i32(); rc.w = r.i32(); rc.h = r.i32();
                room.rects.push_back(rc);
            }

            const std::int32_t objCount = r.i32();
            if (objCount < 0 || objCount > 100'000) {
                h.warnings.push_back("room '" + room.name + "': bad objectCount "
                                     + std::to_string(objCount));
                return;
            }
            for (std::int32_t j = 0; j < objCount; ++j) {
                RoomObject ob;
                ob.a = r.i32(); ob.b = r.i32(); ob.c = r.i32();
                room.objects.push_back(ob);
            }
            h.rooms.push_back(std::move(room));
        }

        const std::int32_t buildingCount = r.i32();
        if (buildingCount < 0 || buildingCount > 200'000) {
            h.warnings.push_back("implausible buildingCount "
                                 + std::to_string(buildingCount));
            return;
        }
        for (std::int32_t i = 0; i < buildingCount; ++i) {
            const std::int32_t n = r.i32();
            if (n < 0 || n > 10'000) {
                h.warnings.push_back("bad building room count " + std::to_string(n));
                return;
            }
            std::vector<std::int32_t> idx;
            idx.reserve(static_cast<std::size_t>(n));
            for (std::int32_t j = 0; j < n; ++j) idx.push_back(r.i32());
            h.buildings.push_back(std::move(idx));
        }

        const auto expected = static_cast<std::size_t>((h.width / 10) * (h.height / 10));
        if (r.remaining() >= expected) {
            h.zombieDensity = r.bytes(expected);
            if (r.remaining() != 0) {
                h.warnings.push_back(std::to_string(r.remaining())
                                     + " trailing bytes after density grid");
            }
        } else {
            h.warnings.push_back("expected " + std::to_string(expected)
                                 + " density bytes, " + std::to_string(r.remaining())
                                 + " remain");
        }
    } catch (const ParseError& e) {
        h.warnings.push_back(std::string("metadata section failed: ") + e.what());
    }
}

// ------------------------------------------------------------- writing ------

std::vector<std::byte> LotHeader::write() const {
    if (!b42) throw std::logic_error("writer supports B42 only");
    if (chunkGrid.size() != static_cast<std::size_t>(kGridBytes)) {
        throw std::logic_error("chunkGrid must be exactly "
                               + std::to_string(kGridBytes) + " bytes, is "
                               + std::to_string(chunkGrid.size()));
    }

    LEW w;
    w.ascii(std::string_view(kMagic, 4));
    w.i32(version);
    w.i32(static_cast<std::int32_t>(tileNames.size()));
    for (const auto& t : tileNames) w.nlString(t);

    w.i32(levelsAbove);
    w.i32(levelsBelow);
    w.i32(minLevel);
    w.i32(unknown12);

    w.i32(static_cast<std::int32_t>(rooms.size()));
    for (const auto& room : rooms) {
        w.nlString(room.name);
        w.i32(room.floor);
        w.i32(static_cast<std::int32_t>(room.rects.size()));
        for (const auto& rc : room.rects) { w.i32(rc.x).i32(rc.y).i32(rc.w).i32(rc.h); }
        w.i32(static_cast<std::int32_t>(room.objects.size()));
        for (const auto& ob : room.objects) { w.i32(ob.a).i32(ob.b).i32(ob.c); }
    }

    w.i32(static_cast<std::int32_t>(buildings.size()));
    for (const auto& b : buildings) {
        w.i32(static_cast<std::int32_t>(b.size()));
        for (auto idx : b) w.i32(idx);
    }

    w.bytes(chunkGrid);
    return w.take();
}

} // namespace pzformat

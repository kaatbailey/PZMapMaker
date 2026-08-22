// Port of LotHeader.java.
//
// X_Y.lotheader -- per-cell tile-name table plus trailing metadata.
//
// TWO KNOWN VARIANTS:
//
// B42 (version 1) -- CONFIRMED against 42.20.x retail files:
//   char[4]  "LOTH" magic
//   int32    version                  (1)
//   int32    tileCount
//   names    tileCount entries, '\n'-separated ASCII
//   int32    levelsAbove              (8 in every cell observed)
//   int32    levelsBelow              (8 in every cell observed)
//   int32    minLevel                 (0 mostly; negative in ~70 cells)
//   int32    unknown12                (meaning not yet identified)
//   int32    roomCount
//   room[]   name '\n', floor, rectCount, rect[]{x,y,w,h}, objCount, obj[]{a,b,c}
//   int32    buildingCount
//   building[] int32 roomCount, int32[] roomIndices
//   byte[1024] per-chunk grid
//
// The trailer layout was confirmed by fitting leftover = 1 + buildings +
// roomRefs across 4064 retail cells.
//
// B42 does NOT store width/height/levels. A full-file scan of a retail 42.20
// lotheader finds no 300/256 cell-size triple anywhere; cell geometry is
// global or lives in map.info.
//
// B41 (no magic) -- legacy, per PZwiki "File formats":
//   int32    version
//   int32    tileCount
//   names    NUL-terminated
//   int32    width, height, levels
//   ...      rooms, buildings, zombie density
//
// The B41 path stays tolerant and records warnings; the B42 path is strict and
// throws, because its layout is confirmed and a mismatch means the file is not
// what we think it is.
#pragma once

#include "le.hpp"
#include "levels.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pzformat {

/// Per-chunk grid: 32x32 for a 256-tile cell, i.e. 8-tile chunks.
inline constexpr int kGridSide  = 32;
inline constexpr int kGridBytes = kGridSide * kGridSide;

struct Rect {
    std::int32_t x = 0, y = 0, w = 0, h = 0;
};

/// Three int32s per room object. Field meanings not identified, so they keep
/// the Java's positional names rather than being given ones we cannot defend.
struct RoomObject {
    std::int32_t a = 0, b = 0, c = 0;
};

struct Room {
    std::string name;
    std::int32_t floor = 0;
    std::vector<Rect> rects;
    std::vector<RoomObject> objects;
};

class LotHeader {
public:
    static constexpr char kMagic[4] = {'L', 'O', 'T', 'H'};

    /// Does not retain the span.
    static LotHeader read(std::span<const std::byte> data);

    /// Serialise a B42 lotheader. Field order and terminators mirror the
    /// reader exactly; round-tripping a retail cell reproduces it byte for byte.
    /// Throws if this is a B41 header.
    std::vector<std::byte> write() const;

    // --- common ---
    bool b42 = false;
    std::int32_t version = 0;
    std::vector<std::string> tileNames;
    std::vector<Room> rooms;
    std::vector<std::vector<std::int32_t>> buildings;

    /// Offset where the tile-name table ended and unidentified data begins.
    std::size_t trailerOffset = 0;
    bool hasTrailerOffset = false;
    std::vector<std::string> warnings;

    // --- B41 only. -1 when absent, which is every B42 file. ---
    std::int32_t width = -1, height = -1, levels = -1;
    std::vector<std::byte> zombieDensity;
    /// Raw unparsed bytes after the tile table, when the B41 layout was not found.
    std::vector<std::byte> trailer;
    std::int32_t padBytesSkipped = -1;

    // --- B42 only ---
    std::int32_t levelsAbove = -1, levelsBelow = -1;
    std::int32_t minLevel = 0;
    /// Trailer field at +12. Behaves as the highest actual z containing data,
    /// but the name records that this is inferred, not identified.
    std::int32_t unknown12 = 0;
    std::vector<std::byte> chunkGrid;
    bool fullyConsumed = false;

    /// Highest actual z containing data. Stored at trailer+12.
    std::int32_t maxLevel() const noexcept { return unknown12; }
    /// Number of z-levels encoded: maxLevel - minLevel + 1.
    std::int32_t levelCount() const noexcept { return unknown12 - minLevel + 1; }
    /// Exactly what LotPack::read needs.
    LevelRange levelRange() const noexcept { return {minLevel, unknown12}; }
    /// Total room references across all buildings.
    std::size_t roomRefs() const noexcept;

private:
    static void readNameTable(LE& r, LotHeader& h);
    static void readB42Meta(LE& r, LotHeader& h);
    static void readB41Meta(LE& r, LotHeader& h);
};

} // namespace pzformat

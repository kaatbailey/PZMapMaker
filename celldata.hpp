// Port of CellData.java.
//
// A whole cell held in memory and editable, decoupled from file layout.
//
// The parsers hand back fresh objects per call, which is right for reading and
// useless for editing. This owns the data: load once, mutate, write back.
//
// Coordinates are cell-local (0..255 on each axis). z is an ACTUAL level, so
// basements are negative; the internal array index is z - minLevel.
//
// Difference from the Java storage model: tiles are stored as a flat
// std::optional<std::vector<int32>> per (z,x,y) rather than int[][][][], and the
// Java relied on a fixed MAX_LEVELS array making every [z] valid even for a
// chunk that encoded fewer levels. Here load() reads only the levels a chunk
// actually stored (Chunk::has) and leaves the rest empty, which is the same
// result the Java got from its null slots.
#pragma once

#include "lotheader.hpp"
#include "lotpack.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pzformat {

class CellData {
public:
    /// An empty cell built from nothing, for generated maps. The header must
    /// already carry its tile names and level range.
    static CellData blank(LotHeader h, int chunksPerSide);

    /// A header for a generated cell: B42 layout, no rooms or buildings yet.
    static LotHeader newHeader(std::vector<std::string> tileNames,
                               std::int32_t minLevel, std::int32_t maxLevel);

    /// Load from file paths.
    static CellData load(const std::filesystem::path& lotpack,
                         const std::filesystem::path& lotheader);
    /// Load from already-read bytes (e.g. mmap). Neither span is retained.
    static CellData load(std::span<const std::byte> lotpackBytes, LotHeader h);

    // --- geometry ---
    const LotHeader& header() const noexcept { return header_; }
    LotHeader&       header()       noexcept { return header_; }
    int cellSize()      const noexcept { return cellSize_; }
    int chunksPerSide() const noexcept { return chunksPerSide_; }
    int levelCount()    const noexcept { return levelCount_; }
    std::int32_t minLevel() const noexcept { return minLevel_; }
    std::int32_t maxLevel() const noexcept { return maxLevel_; }
    std::int32_t version = 1;

    int zIndex(int actualZ) const noexcept { return actualZ - minLevel_; }
    /// Column-major, matching LotPack. See the note there.
    int chunkIndex(int cx, int cy) const noexcept { return cx * chunksPerSide_ + cy; }

    // --- accessors ---
    /// Tile indices at a square, empty span if the square is empty.
    std::span<const std::int32_t> tilesAt(int x, int y, int actualZ) const;
    bool hasSquare(int x, int y, int actualZ) const;
    std::vector<std::string> tileNamesAt(int x, int y, int actualZ) const;
    std::int32_t roomAt(int x, int y, int actualZ) const;

    void setSquare(int x, int y, int actualZ, std::vector<std::int32_t> tileIndices,
                   std::int32_t roomId);
    void clearSquare(int x, int y, int actualZ);

    /// Index of a tile name, appending to the header's table if absent.
    /// Appending is safe: existing indices are unchanged.
    std::int32_t tileIndex(const std::string& name);

    /// Replace the tiles on a rectangle with a single tile. Returns squares changed.
    int fill(const std::string& tileName, int x0, int y0, int w, int h, int actualZ);

    long nonEmptySquares() const;

    // --- writing ---
    /// Serialise to lotpack bytes using the confirmed encoder policy:
    /// every chunk covers the cell's full levelCount, runs of empty squares
    /// span level boundaries (SPAN_LEVELS_FULL — confirmed across 4M+ chunks).
    std::vector<std::byte> writeLotPack() const;
    std::vector<std::byte> writeLotHeader() const { return header_.write(); }

    // --- diffing ---
    struct Diff {
        int squaresChanged = 0, squaresAdded = 0, squaresRemoved = 0;
        std::vector<std::string> samples;
        bool isEmpty() const noexcept {
            return squaresChanged + squaresAdded + squaresRemoved == 0;
        }
        std::string toString() const;
    };

    /// Square-by-square comparison, for proving an edit was surgical.
    static Diff diff(const CellData& a, const CellData& b);

private:
    CellData(LotHeader h, int chunksPerSide, int levelCount);

    void checkZ(int zi) const;
    std::vector<std::byte> encodeChunk(int cx, int cy) const;

    std::size_t idx(int zi, int x, int y) const noexcept {
        return (static_cast<std::size_t>(zi) * static_cast<std::size_t>(cellSize_)
                + static_cast<std::size_t>(x)) * static_cast<std::size_t>(cellSize_)
             + static_cast<std::size_t>(y);
    }

    LotHeader header_;
    int chunksPerSide_ = 0;
    int cellSize_ = 0;
    int levelCount_ = 0;
    std::int32_t minLevel_ = 0;
    std::int32_t maxLevel_ = 0;

    // [zIndex][x][y] flattened. nullopt == empty square (Java's null slot).
    std::vector<std::optional<std::vector<std::int32_t>>> tiles_;
    std::vector<std::int32_t> rooms_; // -1 == none
};

} // namespace pzformat

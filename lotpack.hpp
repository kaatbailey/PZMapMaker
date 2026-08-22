// Port of LotPack.java.
//
// world_X_Y.lotpack -- Build 42 tile data for one 256x256 cell.
//
// FORMAT CONFIRMED against retail 42.20:
//
//   char[4]  "LOTP"
//   int32    version        (1)
//   int32    chunkCount     (1024 = 32x32 chunks of 8x8 tiles)
//   int64    offset[chunkCount]        64-bit, unlike B41's 32-bit table
//   chunk bodies at those offsets
//
// Header size is 12 + 8*chunkCount, which equals offset[0] exactly.
//
// Chunk body, iterating z, then x, then y over the 8x8 chunk:
//   int32 count
//     count == -1 -> int32 run : that many consecutive empty squares
//     else        -> int32 roomId (-1 = none), then count-1 tile indices
//                    into the lotheader tile-name table
//
// Verified by requiring every chunk body to end exactly where the next begins.
//
// Differences from the Java, all storage rather than format:
//  - Reads over a span, so a cell can be mmap'ed and chunks decoded on demand.
//    LotPack does NOT own the bytes; keep the MappedFile alive alongside it.
//  - A Chunk stores only the levels it actually has, in flat vectors with the
//    tile lists in one contiguous pool. The Java allocated
//    MAX_LEVELS*8*8 slots per chunk regardless -- 4096 pointers for a chunk
//    that typically uses 64 of them, times 1024 chunks per cell.
#pragma once

#include "le.hpp"
#include "levels.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pzformat {

inline constexpr int kChunkSize       = 8;
inline constexpr int kSquaresPerLevel = kChunkSize * kChunkSize;

/// Nominal level count (levelsAbove + levelsBelow from the lotheader).
/// NOT a hard cap: 4 of 4065 retail cells encode more, up to 30 levels, and
/// those are the deep-basement cells.
inline constexpr int kLevels    = 16;
/// Generous allocation ceiling; actual depth is reported per chunk.
inline constexpr int kMaxLevels = 64;

/// Chunk-body encoding policy. Reading tells us runs of empty squares are
/// written as (-1, count) and that trailing empty levels are omitted, but NOT
/// the encoder's exact choices. These are the plausible variants; the
/// round-trip harness decides which reproduces retail bytes.
enum class Policy {
    SpanLevelsMinimal,   ///< runs may span levels; chunk encodes only the levels it needs
    BreakAtLevelMinimal, ///< runs break at level boundaries; minimal levels
    SpanLevelsFull,      ///< runs may span levels; every chunk encodes the cell's full level count
    BreakAtLevelFull,    ///< runs break at level boundaries; full level count
};

constexpr bool runsSpanLevels(Policy p) noexcept {
    return p == Policy::SpanLevelsMinimal || p == Policy::SpanLevelsFull;
}
constexpr bool minimalLevels(Policy p) noexcept {
    return p == Policy::SpanLevelsMinimal || p == Policy::BreakAtLevelMinimal;
}

/// One decoded 8x8xN chunk.
///
/// A square is either absent (covered by a run) or present. A present square
/// always has a room id and may have zero tiles -- count == 1 in the file is a
/// square with a room and no tiles, which is NOT the same as an empty square
/// and must not re-encode as part of a run.
class Chunk {
public:
    int  squaresCovered() const noexcept { return squaresCovered_; }
    int  maxZ()           const noexcept { return maxZ_; }
    /// Levels the file's square count spans, including trailing runs. Diagnostic.
    int  levelsEncoded()  const noexcept {
        return (squaresCovered_ + kSquaresPerLevel - 1) / kSquaresPerLevel;
    }
    /// Levels up to and including the last square that actually holds data.
    /// This is what the minimal-levels encode policy writes.
    int  levelsWithData() const noexcept { return maxZ_ < 0 ? 1 : maxZ_ + 1; }
    int  levelsStored()   const noexcept {
        return static_cast<int>(room_.size() / kSquaresPerLevel);
    }

    bool has(int z, int x, int y) const noexcept {
        const std::size_t i = index(z, x, y);
        return i < start_.size() && start_[i] != kAbsent;
    }
    std::int32_t room(int z, int x, int y) const noexcept {
        const std::size_t i = index(z, x, y);
        return i < room_.size() ? room_[i] : -1;
    }
    std::span<const std::int32_t> tiles(int z, int x, int y) const noexcept {
        const std::size_t i = index(z, x, y);
        if (i >= start_.size() || start_[i] == kAbsent) return {};
        return std::span<const std::int32_t>(pool_).subspan(start_[i], count_[i]);
    }

    static std::size_t index(int z, int x, int y) noexcept {
        return static_cast<std::size_t>(z) * kSquaresPerLevel
             + static_cast<std::size_t>(x) * kChunkSize
             + static_cast<std::size_t>(y);
    }

private:
    friend class LotPack;

    static constexpr std::uint32_t kAbsent = 0xFFFFFFFFu;

    void put(int square, std::int32_t roomId, std::span<const std::int32_t> tiles);

    std::vector<std::int32_t>  room_;
    std::vector<std::uint32_t> start_;
    std::vector<std::uint32_t> count_;
    std::vector<std::int32_t>  pool_;
    int squaresCovered_ = 0;
    int maxZ_ = -1;
};

class LotPack {
public:
    static constexpr char kMagic[4] = {'L', 'O', 'T', 'P'};

    /// Does not copy and does not take ownership. `data` must outlive the LotPack.
    static LotPack read(std::span<const std::byte> data, LevelRange levels);

    std::int32_t version()       const noexcept { return version_; }
    std::int32_t chunkCount()    const noexcept { return chunkCount_; }
    int          chunksPerSide() const noexcept { return chunksPerSide_; }
    int          cellSize()      const noexcept { return cellSize_; }
    std::int32_t minLevel()      const noexcept { return levels_.minLevel; }
    std::int32_t maxLevel()      const noexcept { return levels_.maxLevel; }
    std::int32_t levelCount()    const noexcept { return levels_.levelCount(); }

    std::span<const std::int64_t> offsets() const noexcept { return offsets_; }
    std::span<const std::byte>    rawFile() const noexcept { return data_; }

    /// Chunk offset table index. COLUMN-MAJOR: cx varies slowest.
    ///
    /// This was originally row-major, which transposed every coordinate in the
    /// cell. Byte round-tripping did not catch it -- read and write shared the
    /// same wrong formula, so files matched perfectly while the map was
    /// mirrored about its diagonal. Found by checking lotheader room
    /// rectangles against the tiles beneath them: rooms are indoors, and under
    /// the wrong orientation only 5.4% of room squares sat on an interior floor
    /// versus 30.6% under the right one.
    int chunkIndex(int cx, int cy) const noexcept { return cx * chunksPerSide_ + cy; }

    std::size_t chunkStart(int chunkIndexValue) const;
    std::size_t chunkEnd(int chunkIndexValue) const;

    /// Original bytes of a chunk body, for byte-comparison against a re-encode.
    std::span<const std::byte> rawChunk(int chunkIndexValue) const;

    Chunk chunk(int cx, int cy) const { return chunkAt(chunkIndex(cx, cy)); }
    Chunk chunkAt(int chunkIndexValue) const;

    /// Re-encode a chunk body under the given policy.
    std::vector<std::byte> encodeChunk(const Chunk& c, Policy p) const;

    /// Rebuild the whole file: magic, version, recomputed int64 offset table,
    /// chunk bodies.
    std::vector<std::byte> write(Policy p) const;

    /// Convert an actual z-level to a chunk array index, and back.
    int zIndex(int actualZ)  const noexcept { return actualZ - levels_.minLevel; }
    int actualZ(int zIdx)    const noexcept { return zIdx + levels_.minLevel; }

    /// Tile names for one square, z given as a chunk array index.
    /// Empty if the square is absent. Out-of-range ids come back as "?<id>",
    /// as the Java did, rather than throwing.
    std::vector<std::string> tileNamesAt(int cellX, int cellY, int z,
                                         std::span<const std::string> tileNames) const;

private:
    std::span<const std::byte> data_{};
    std::vector<std::int64_t>  offsets_;
    LevelRange   levels_{};
    std::int32_t version_ = 0;
    std::int32_t chunkCount_ = 0;
    int chunksPerSide_ = 0;
    int cellSize_ = 0;
};

} // namespace pzformat

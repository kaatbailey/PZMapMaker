#include "lotpack.hpp"

#include "lew.hpp"

#include <algorithm>
#include <cmath>

namespace pzformat {

// ---------------------------------------------------------------- Chunk -----

void Chunk::put(int square, std::int32_t roomId, std::span<const std::int32_t> tiles) {
    const int z = square / kSquaresPerLevel;
    const std::size_t need = static_cast<std::size_t>(z + 1) * kSquaresPerLevel;
    if (room_.size() < need) {
        room_.resize(need, -1);
        start_.resize(need, kAbsent);
        count_.resize(need, 0);
    }

    const auto i = static_cast<std::size_t>(square);
    room_[i]  = roomId;
    start_[i] = static_cast<std::uint32_t>(pool_.size());
    count_[i] = static_cast<std::uint32_t>(tiles.size());
    pool_.insert(pool_.end(), tiles.begin(), tiles.end());

    if (z > maxZ_) maxZ_ = z;
}

// -------------------------------------------------------------- LotPack -----

LotPack LotPack::read(std::span<const std::byte> data, LevelRange levels) {
    LotPack lp;
    lp.data_   = data;
    lp.levels_ = levels;

    LE r(data);
    const auto magic = r.view(4);
    if (!std::equal(magic.begin(), magic.end(), reinterpret_cast<const std::byte*>(kMagic))) {
        std::string found;
        for (auto b : magic) {
            const auto v = std::to_integer<std::uint8_t>(b);
            found.push_back(v >= 32 && v < 127 ? static_cast<char>(v) : '.');
        }
        throw ParseError("expected \"LOTP\" magic, found \"" + found + "\"");
    }

    lp.version_    = r.i32();
    lp.chunkCount_ = r.i32();
    if (lp.chunkCount_ <= 0 || lp.chunkCount_ > (1 << 20)) {
        throw ParseError("chunkCount " + std::to_string(lp.chunkCount_));
    }

    lp.chunksPerSide_ = static_cast<int>(std::lround(std::sqrt(
        static_cast<double>(lp.chunkCount_))));
    if (lp.chunksPerSide_ * lp.chunksPerSide_ != lp.chunkCount_) {
        throw ParseError("chunkCount " + std::to_string(lp.chunkCount_) + " is not square");
    }
    lp.cellSize_ = lp.chunksPerSide_ * kChunkSize;

    const auto headerSize = static_cast<std::int64_t>(12) + std::int64_t{8} * lp.chunkCount_;
    lp.offsets_.resize(static_cast<std::size_t>(lp.chunkCount_));
    for (auto& o : lp.offsets_) o = r.i64();

    if (lp.offsets_[0] != headerSize) {
        throw ParseError("first offset " + std::to_string(lp.offsets_[0])
                         + " != header size " + std::to_string(headerSize));
    }
    const auto fileLen = static_cast<std::int64_t>(data.size());
    for (std::size_t i = 1; i < lp.offsets_.size(); ++i) {
        if (lp.offsets_[i] <= lp.offsets_[i - 1] || lp.offsets_[i] >= fileLen) {
            throw ParseError("offset " + std::to_string(i) + " = "
                             + std::to_string(lp.offsets_[i]) + " invalid");
        }
    }
    return lp;
}

std::size_t LotPack::chunkStart(int chunkIndexValue) const {
    if (chunkIndexValue < 0 || chunkIndexValue >= chunkCount_) {
        throw ParseError("chunk index " + std::to_string(chunkIndexValue) + " out of range");
    }
    return static_cast<std::size_t>(offsets_[static_cast<std::size_t>(chunkIndexValue)]);
}

std::size_t LotPack::chunkEnd(int chunkIndexValue) const {
    if (chunkIndexValue < 0 || chunkIndexValue >= chunkCount_) {
        throw ParseError("chunk index " + std::to_string(chunkIndexValue) + " out of range");
    }
    return chunkIndexValue + 1 < chunkCount_
        ? static_cast<std::size_t>(offsets_[static_cast<std::size_t>(chunkIndexValue) + 1])
        : data_.size();
}

std::span<const std::byte> LotPack::rawChunk(int chunkIndexValue) const {
    const std::size_t s = chunkStart(chunkIndexValue);
    const std::size_t e = chunkEnd(chunkIndexValue);
    return data_.subspan(s, e - s);
}

Chunk LotPack::chunkAt(int chunkIndexValue) const {
    const std::size_t start = chunkStart(chunkIndexValue);
    const std::size_t end   = chunkEnd(chunkIndexValue);

    LE r(data_);
    r.seek(start);

    Chunk c;
    int square = 0; // linear index over z * 8 * 8
    constexpr int total = kMaxLevels * kSquaresPerLevel;

    // The body is driven by file position, not by a fixed level count: trailing
    // empty levels are omitted from the file entirely, so chunk bodies vary in
    // length and a fixed z-loop overruns into the next chunk.
    std::vector<std::int32_t> tiles;
    while (r.pos() < end) {
        const std::int32_t count = r.i32();

        if (count == -1) {
            const std::int32_t run = r.i32();
            if (run < 1) {
                throw ParseError("chunk " + std::to_string(chunkIndexValue) + " skip run "
                                 + std::to_string(run) + " at " + std::to_string(r.pos() - 4));
            }
            square += run;
            if (square > total) {
                throw ParseError("chunk " + std::to_string(chunkIndexValue)
                                 + " skip run overshoots: " + std::to_string(square)
                                 + " > " + std::to_string(total));
            }
            continue;
        }

        if (count < 1 || count > 256) {
            throw ParseError("chunk " + std::to_string(chunkIndexValue) + " count "
                             + std::to_string(count) + " at offset "
                             + std::to_string(r.pos() - 4));
        }
        if (square >= total) {
            throw ParseError("chunk " + std::to_string(chunkIndexValue)
                             + " more squares than " + std::to_string(total));
        }

        const std::int32_t roomId = r.i32();
        tiles.clear();
        tiles.reserve(static_cast<std::size_t>(count - 1));
        for (std::int32_t i = 0; i < count - 1; ++i) tiles.push_back(r.i32());
        c.put(square, roomId, tiles);
        ++square;
    }

    if (r.pos() != end) {
        throw ParseError("chunk " + std::to_string(chunkIndexValue) + " ended at "
                         + std::to_string(r.pos()) + ", next chunk begins at "
                         + std::to_string(end));
    }

    c.squaresCovered_ = square;
    return c;
}

std::vector<std::byte> LotPack::encodeChunk(const Chunk& c, Policy p) const {
    const int levels = minimalLevels(p)
        ? c.levelsWithData()
        : std::max(1, static_cast<int>(levels_.levelCount()));

    LEW w;
    std::int32_t run = 0;

    for (int sq = 0; sq < levels * kSquaresPerLevel; ++sq) {
        const int z   = sq / kSquaresPerLevel;
        const int rem = sq % kSquaresPerLevel;
        const int x   = rem / kChunkSize;
        const int y   = rem % kChunkSize;

        if (!runsSpanLevels(p) && rem == 0 && run > 0) {
            w.i32(-1).i32(run);
            run = 0;
        }

        if (!c.has(z, x, y)) { ++run; continue; }

        if (run > 0) { w.i32(-1).i32(run); run = 0; }

        const auto tiles = c.tiles(z, x, y);
        w.i32(static_cast<std::int32_t>(tiles.size()) + 1);
        w.i32(c.room(z, x, y));
        for (auto t : tiles) w.i32(t);
    }
    if (run > 0) w.i32(-1).i32(run);

    return w.take();
}

std::vector<std::byte> LotPack::write(Policy p) const {
    std::vector<std::vector<std::byte>> bodies(static_cast<std::size_t>(chunkCount_));
    for (int i = 0; i < chunkCount_; ++i) {
        bodies[static_cast<std::size_t>(i)] = encodeChunk(chunkAt(i), p);
    }

    const auto headerSize = static_cast<std::int64_t>(12) + std::int64_t{8} * chunkCount_;

    std::size_t totalBody = 0;
    for (const auto& b : bodies) totalBody += b.size();

    LEW w(static_cast<std::size_t>(headerSize) + totalBody);
    w.ascii(std::string_view(kMagic, 4));
    w.i32(version_);
    w.i32(chunkCount_);

    std::int64_t off = headerSize;
    for (const auto& b : bodies) {
        w.i64(off);
        off += static_cast<std::int64_t>(b.size());
    }
    for (const auto& b : bodies) w.bytes(b);

    return w.take();
}

std::vector<std::string> LotPack::tileNamesAt(int cellX, int cellY, int z,
                                              std::span<const std::string> tileNames) const {
    const Chunk c = chunk(cellX / kChunkSize, cellY / kChunkSize);
    const auto ids = c.tiles(z, cellX % kChunkSize, cellY % kChunkSize);

    std::vector<std::string> out;
    out.reserve(ids.size());
    for (auto id : ids) {
        if (id >= 0 && static_cast<std::size_t>(id) < tileNames.size()) {
            out.push_back(tileNames[static_cast<std::size_t>(id)]);
        } else {
            out.push_back("?" + std::to_string(id));
        }
    }
    return out;
}

} // namespace pzformat

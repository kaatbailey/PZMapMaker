#include "celldata.hpp"

#include "le.hpp"
#include "lew.hpp"
#include "mapped_file.hpp"

#include <algorithm>
#include <stdexcept>

namespace pzformat {

CellData::CellData(LotHeader h, int chunksPerSide, int levelCount)
    : header_(std::move(h)),
      chunksPerSide_(chunksPerSide),
      cellSize_(chunksPerSide * kChunkSize),
      levelCount_(levelCount),
      minLevel_(header_.minLevel),
      maxLevel_(header_.maxLevel()) {
    const std::size_t n = static_cast<std::size_t>(levelCount_)
                        * static_cast<std::size_t>(cellSize_)
                        * static_cast<std::size_t>(cellSize_);
    tiles_.resize(n);
    rooms_.assign(n, -1);
}

CellData CellData::blank(LotHeader h, int chunksPerSide) {
    const int levels = h.maxLevel() - h.minLevel + 1;
    return CellData(std::move(h), chunksPerSide, levels);
}

LotHeader CellData::newHeader(std::vector<std::string> tileNames,
                              std::int32_t minLevel, std::int32_t maxLevel) {
    LotHeader h;
    h.b42 = true;
    h.version = 1;
    h.levelsAbove = 8;
    h.levelsBelow = 8;
    h.minLevel = minLevel;
    h.unknown12 = maxLevel;
    h.tileNames = std::move(tileNames);
    h.chunkGrid.assign(static_cast<std::size_t>(kGridBytes), std::byte{0});
    h.fullyConsumed = true;
    return h;
}

CellData CellData::load(const std::filesystem::path& lotpack,
                        const std::filesystem::path& lotheader) {
    const auto hdrBytes = readAllBytes(lotheader);
    LotHeader h = LotHeader::read(hdrBytes);
    MappedFile packMap(lotpack);
    return load(packMap.span(), std::move(h));
}

CellData CellData::load(std::span<const std::byte> lotpackBytes, LotHeader h) {
    const LotPack lp = LotPack::read(lotpackBytes, h.levelRange());
    CellData c(std::move(h), lp.chunksPerSide(), lp.levelCount());
    c.version = lp.version();

    for (int cy = 0; cy < lp.chunksPerSide(); ++cy) {
        for (int cx = 0; cx < lp.chunksPerSide(); ++cx) {
            const Chunk ch = lp.chunk(cx, cy);
            for (int z = 0; z < c.levelCount_; ++z) {
                for (int x = 0; x < kChunkSize; ++x) {
                    for (int y = 0; y < kChunkSize; ++y) {
                        const int gx = cx * kChunkSize + x;
                        const int gy = cy * kChunkSize + y;
                        const std::size_t at = c.idx(z, gx, gy);
                        // Java read ch.tiles[z][x][y] (null when absent). Here a
                        // chunk stores only the levels it has, so a square past
                        // the chunk's stored depth is simply empty.
                        if (ch.has(z, x, y)) {
                            const auto t = ch.tiles(z, x, y);
                            c.tiles_[at].emplace(t.begin(), t.end());
                            c.rooms_[at] = ch.room(z, x, y);
                        }
                    }
                }
            }
        }
    }
    return c;
}

void CellData::checkZ(int zi) const {
    if (zi < 0 || zi >= levelCount_) {
        throw std::out_of_range("z index " + std::to_string(zi) + " outside 0.."
            + std::to_string(levelCount_ - 1) + " (cell covers actual z "
            + std::to_string(minLevel_) + ".." + std::to_string(maxLevel_) + ")");
    }
}

std::span<const std::int32_t> CellData::tilesAt(int x, int y, int actualZ) const {
    const int zi = zIndex(actualZ);
    checkZ(zi);
    const auto& slot = tiles_[idx(zi, x, y)];
    if (!slot) return {};
    return *slot;
}

bool CellData::hasSquare(int x, int y, int actualZ) const {
    const int zi = zIndex(actualZ);
    checkZ(zi);
    return tiles_[idx(zi, x, y)].has_value();
}

std::vector<std::string> CellData::tileNamesAt(int x, int y, int actualZ) const {
    const auto t = tilesAt(x, y, actualZ);
    std::vector<std::string> out;
    out.reserve(t.size());
    const auto& names = header_.tileNames;
    for (auto i : t) {
        if (i >= 0 && static_cast<std::size_t>(i) < names.size()) {
            out.push_back(names[static_cast<std::size_t>(i)]);
        } else {
            out.push_back("?" + std::to_string(i));
        }
    }
    return out;
}

std::int32_t CellData::roomAt(int x, int y, int actualZ) const {
    const int zi = zIndex(actualZ);
    checkZ(zi);
    return rooms_[idx(zi, x, y)];
}

void CellData::setSquare(int x, int y, int actualZ, std::vector<std::int32_t> tileIndices,
                         std::int32_t roomId) {
    const int zi = zIndex(actualZ);
    checkZ(zi);
    const std::size_t at = idx(zi, x, y);
    tiles_[at] = std::move(tileIndices);
    rooms_[at] = roomId;
}

void CellData::clearSquare(int x, int y, int actualZ) {
    const int zi = zIndex(actualZ);
    checkZ(zi);
    const std::size_t at = idx(zi, x, y);
    tiles_[at].reset();
    rooms_[at] = -1;
}

std::int32_t CellData::tileIndex(const std::string& name) {
    auto& names = header_.tileNames;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) return static_cast<std::int32_t>(i);
    }
    names.push_back(name);
    return static_cast<std::int32_t>(names.size() - 1);
}

int CellData::fill(const std::string& tileName, int x0, int y0, int w, int h, int actualZ) {
    const std::int32_t idxVal = tileIndex(tileName);
    const int zi = zIndex(actualZ);
    checkZ(zi);
    int changed = 0;
    const std::vector<std::int32_t> want{idxVal};
    for (int x = x0; x < x0 + w; ++x) {
        for (int y = y0; y < y0 + h; ++y) {
            if (x < 0 || y < 0 || x >= cellSize_ || y >= cellSize_) continue;
            auto& slot = tiles_[idx(zi, x, y)];
            if (!slot || *slot != want) ++changed;
            slot = want;
        }
    }
    return changed;
}

long CellData::nonEmptySquares() const {
    long n = 0;
    for (const auto& s : tiles_) if (s) ++n;
    return n;
}

std::vector<std::byte> CellData::encodeChunk(int cx, int cy) const {
    LEW w;
    std::int32_t run = 0;
    for (int z = 0; z < levelCount_; ++z) {
        for (int x = 0; x < kChunkSize; ++x) {
            for (int y = 0; y < kChunkSize; ++y) {
                const int gx = cx * kChunkSize + x;
                const int gy = cy * kChunkSize + y;
                const auto& slot = tiles_[idx(z, gx, gy)];
                if (!slot) { ++run; continue; }
                if (run > 0) { w.i32(-1).i32(run); run = 0; }
                w.i32(static_cast<std::int32_t>(slot->size()) + 1);
                w.i32(rooms_[idx(z, gx, gy)]);
                for (auto v : *slot) w.i32(v);
            }
        }
    }
    if (run > 0) w.i32(-1).i32(run);
    return w.take();
}

std::vector<std::byte> CellData::writeLotPack() const {
    const int chunkCount = chunksPerSide_ * chunksPerSide_;
    std::vector<std::vector<std::byte>> bodies(static_cast<std::size_t>(chunkCount));
    for (int cy = 0; cy < chunksPerSide_; ++cy) {
        for (int cx = 0; cx < chunksPerSide_; ++cx) {
            bodies[static_cast<std::size_t>(chunkIndex(cx, cy))] = encodeChunk(cx, cy);
        }
    }

    const auto headerSize = static_cast<std::int64_t>(12) + std::int64_t{8} * chunkCount;
    std::size_t total = 0;
    for (const auto& b : bodies) total += b.size();

    LEW w(static_cast<std::size_t>(headerSize) + total);
    w.ascii(std::string_view(LotPack::kMagic, 4));
    w.i32(version);
    w.i32(chunkCount);
    std::int64_t off = headerSize;
    for (const auto& b : bodies) { w.i64(off); off += static_cast<std::int64_t>(b.size()); }
    for (const auto& b : bodies) w.bytes(b);
    return w.take();
}

// -------------------------------------------------------------- diffing -----

std::string CellData::Diff::toString() const {
    return std::to_string(squaresChanged) + " changed, "
         + std::to_string(squaresAdded) + " added, "
         + std::to_string(squaresRemoved) + " removed";
}

CellData::Diff CellData::diff(const CellData& a, const CellData& b) {
    Diff d;
    const int levels = std::min(a.levelCount_, b.levelCount_);
    const int side = std::min(a.cellSize_, b.cellSize_);

    auto note = [&](int x, int y, int zi,
                    const std::optional<std::vector<std::int32_t>>& ta,
                    const std::optional<std::vector<std::int32_t>>& tb) {
        if (d.samples.size() >= 6) return;
        auto show = [](const std::optional<std::vector<std::int32_t>>& t) {
            if (!t) return std::string("[]");
            std::string s = "[";
            for (std::size_t i = 0; i < t->size(); ++i) {
                if (i) s += ", ";
                s += std::to_string((*t)[i]);
            }
            return s + "]";
        };
        d.samples.push_back("(" + std::to_string(x) + "," + std::to_string(y) + ",z"
            + std::to_string(zi + a.minLevel_) + ") " + show(ta) + " -> " + show(tb));
    };

    for (int z = 0; z < levels; ++z) {
        for (int x = 0; x < side; ++x) {
            for (int y = 0; y < side; ++y) {
                const auto& ta = a.tiles_[a.idx(z, x, y)];
                const auto& tb = b.tiles_[b.idx(z, x, y)];
                if (!ta && !tb) continue;
                if (!ta) { ++d.squaresAdded;   note(x, y, z, ta, tb); continue; }
                if (!tb) { ++d.squaresRemoved; note(x, y, z, ta, tb); continue; }
                if (*ta != *tb || a.rooms_[a.idx(z, x, y)] != b.rooms_[b.idx(z, x, y)]) {
                    ++d.squaresChanged;
                    note(x, y, z, ta, tb);
                }
            }
        }
    }
    return d;
}

} // namespace pzformat

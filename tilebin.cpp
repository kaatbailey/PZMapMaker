#include "tilebin.hpp"

#include "le.hpp"

#include <algorithm>

namespace pzformat {

const char* toString(TileShape s) noexcept {
    switch (s) {
        case TileShape::IndexCount:    return "INDEX_COUNT";
        case TileShape::XYCount:       return "XY_COUNT";
        case TileShape::IndexUnkCount: return "INDEX_UNK_COUNT";
        case TileShape::CountOnly:     return "COUNT_ONLY";
    }
    return "?";
}

namespace {

bool printable(std::string_view s) {
    for (char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 32 || u > 126) return false;
    }
    return true;
}

} // namespace

TileBin TileBin::read(std::span<const std::byte> data, TileShape shape, int extraInts) {
    TileBin tb;
    LE r(data);

    const auto magic = r.view(4);
    if (!std::equal(magic.begin(), magic.end(), reinterpret_cast<const std::byte*>(kMagic))) {
        std::string found;
        for (auto b : magic) {
            const auto v = std::to_integer<std::uint8_t>(b);
            found.push_back(v >= 32 && v < 127 ? static_cast<char>(v) : '.');
        }
        throw ParseError("expected \"tdef\", found \"" + found + "\"");
    }

    tb.version_ = r.i32();
    tb.tilesetCount_ = r.i32();
    if (tb.tilesetCount_ < 0 || tb.tilesetCount_ > 100'000) {
        throw ParseError("tilesetCount " + std::to_string(tb.tilesetCount_));
    }

    tb.tilesets_.reserve(static_cast<std::size_t>(tb.tilesetCount_));
    for (std::int32_t i = 0; i < tb.tilesetCount_; ++i) {
        Tileset ts;
        ts.file = r.cString();
        const std::string image = r.cString();
        if (ts.file.empty() || !printable(ts.file) || !printable(image)) {
            throw ParseError("tileset " + std::to_string(i)
                             + " has a non-printable name at " + std::to_string(r.pos()));
        }
        ts.width  = r.i32();
        ts.height = r.i32();
        ts.id     = r.i32();
        for (int k = 0; k < extraInts; ++k) (void)r.i32();
        const std::int32_t tileCount = r.i32();

        if (ts.width <= 0 || ts.width > 4096 || ts.height <= 0 || ts.height > 4096) {
            throw ParseError("tileset '" + ts.file + "' size "
                             + std::to_string(ts.width) + "x" + std::to_string(ts.height));
        }
        if (tileCount < 0 || tileCount > 1'000'000) {
            throw ParseError("tileset '" + ts.file + "' tileCount " + std::to_string(tileCount));
        }

        ts.tiles.reserve(static_cast<std::size_t>(tileCount));
        for (std::int32_t t = 0; t < tileCount; ++t) {
            Tile tile;
            std::int32_t propCount = 0;

            switch (shape) {
                case TileShape::IndexCount: {
                    const std::int32_t idx = r.i32();
                    tile.index = idx;
                    tile.x = ts.width == 0 ? 0 : idx % ts.width;
                    tile.y = ts.width == 0 ? 0 : idx / ts.width;
                    propCount = r.i32();
                    break;
                }
                case TileShape::XYCount: {
                    tile.x = r.i32();
                    tile.y = r.i32();
                    tile.index = tile.y * ts.width + tile.x;
                    propCount = r.i32();
                    break;
                }
                case TileShape::IndexUnkCount: {
                    const std::int32_t idx = r.i32();
                    (void)r.i32();
                    tile.index = idx;
                    tile.x = ts.width == 0 ? 0 : idx % ts.width;
                    tile.y = ts.width == 0 ? 0 : idx / ts.width;
                    propCount = r.i32();
                    break;
                }
                case TileShape::CountOnly: {
                    tile.index = t;
                    tile.x = ts.width == 0 ? 0 : t % ts.width;
                    tile.y = ts.width == 0 ? 0 : t / ts.width;
                    propCount = r.i32();
                    break;
                }
            }

            if (propCount < 0 || propCount > 500) {
                throw ParseError("tileset '" + ts.file + "' tile " + std::to_string(t)
                                 + " propCount " + std::to_string(propCount)
                                 + " at offset " + std::to_string(r.pos() - 4));
            }
            for (std::int32_t p = 0; p < propCount; ++p) {
                std::string k = r.cString();
                std::string v = r.cString();
                if (k.empty() || !printable(k)) {
                    throw ParseError("tileset '" + ts.file + "' tile " + std::to_string(t)
                                     + " property " + std::to_string(p) + " has a bad key at "
                                     + std::to_string(r.pos()));
                }
                tile.props.put(std::move(k), std::move(v));
            }
            tile.tileset = ts.file;
            tile.name = ts.file + "_" + std::to_string(tile.index);
            ts.tiles.push_back(std::move(tile));
        }
        tb.tilesets_.push_back(std::move(ts));
    }

    if (!r.eof()) {
        throw ParseError("trailing data: " + std::to_string(r.remaining()) + " bytes unread");
    }

    tb.rebuildIndex();
    return tb;
}

TileBin TileBin::read(const std::filesystem::path& file, TileShape shape, int extraInts) {
    return read(readAllBytes(file), shape, extraInts);
}

void TileBin::rebuildIndex() {
    byName_.clear();
    std::size_t total = 0;
    for (const auto& ts : tilesets_) total += ts.tiles.size();
    byName_.reserve(total);
    for (const auto& ts : tilesets_) {
        for (const auto& t : ts.tiles) byName_.insert_or_assign(t.name, &t);
    }
}

} // namespace pzformat

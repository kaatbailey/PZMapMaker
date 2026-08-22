#include "mapproject.hpp"

#include "le.hpp" // ParseError

#include <algorithm>
#include <fstream>
#include <regex>
#include <stdexcept>

namespace pzformat {

MapProject MapProject::open(const std::filesystem::path& mapDir, const TileIndex& tileIndex) {
    if (!std::filesystem::exists(mapDir) || !std::filesystem::is_directory(mapDir)) {
        throw std::runtime_error("not a directory: " + mapDir.string());
    }

    MapProject p(mapDir, tileIndex);

    // A cell exists iff BOTH X_Y.lotheader and world_X_Y.lotpack are present.
    // Enumerate by lotheader, then require the matching lotpack.
    static const std::regex headerRe(R"(^(-?\d+)_(-?\d+)\.lotheader$)");
    for (const auto& e : std::filesystem::directory_iterator(mapDir)) {
        if (!e.is_regular_file()) continue;
        const std::string fn = e.path().filename().string();
        std::smatch m;
        if (!std::regex_match(fn, m, headerRe)) continue;
        CellCoord c{std::stoi(m[1].str()), std::stoi(m[2].str())};
        if (std::filesystem::exists(p.lotpackPath(c))) {
            p.cells_.push_back(c);
        }
    }

    if (p.cells_.empty()) {
        throw std::runtime_error("no cells (X_Y.lotheader + world_X_Y.lotpack) in "
                                 + mapDir.string());
    }
    std::sort(p.cells_.begin(), p.cells_.end());
    return p;
}

bool MapProject::hasCell(const CellCoord& c) const {
    return std::binary_search(cells_.begin(), cells_.end(), c);
}

bool MapProject::isResident(const CellCoord& c) const {
    return cache_.find(c) != cache_.end();
}

LoadedCell& MapProject::load(const CellCoord& c) {
    const auto it = cache_.find(c);
    if (it != cache_.end()) {
        touch(c);
        return it->second;
    }
    if (!hasCell(c)) {
        throw std::runtime_error("cell " + c.name() + " is not part of this map");
    }

    auto data = std::make_unique<CellData>(
        CellData::load(lotpackPath(c), lotheaderPath(c)));
    auto editor = std::make_unique<CellEditor>(*data, *tiles_);

    LoadedCell lc;
    lc.coord = c;
    lc.data = std::move(data);
    lc.editor = std::move(editor);
    lc.dirty = false;

    auto [pos, inserted] = cache_.emplace(c, std::move(lc));
    (void)inserted;
    lru_.push_front(c);
    enforceCap();
    return pos->second;
}

void MapProject::touch(const CellCoord& c) {
    lru_.remove(c);
    lru_.push_front(c);
}

void MapProject::enforceCap() {
    // Evict clean cells from the LRU tail until at or under the cap. Dirty cells
    // are skipped — evicting one would silently drop unsaved edits.
    auto it = lru_.end();
    while (cache_.size() > cacheCap_ && it != lru_.begin()) {
        --it;
        const CellCoord victim = *it;
        const auto cit = cache_.find(victim);
        if (cit != cache_.end() && !cit->second.dirty) {
            cache_.erase(cit);
            it = lru_.erase(it);
        }
    }
}

void MapProject::markDirty(const CellCoord& c) {
    const auto it = cache_.find(c);
    if (it != cache_.end()) {
        it->second.dirty = true;
        touch(c);
    }
}

std::vector<CellCoord> MapProject::dirtyCells() const {
    std::vector<CellCoord> out;
    for (const auto& [coord, lc] : cache_) {
        if (lc.dirty) out.push_back(coord);
    }
    return out;
}

bool MapProject::anyDirty() const {
    for (const auto& [coord, lc] : cache_) if (lc.dirty) return true;
    return false;
}

void MapProject::save(const CellCoord& c) {
    const auto it = cache_.find(c);
    if (it == cache_.end() || !it->second.dirty) return;

    const CellData& cell = *it->second.data;

    // Write to a temp file then rename, so a crash mid-write cannot corrupt the
    // existing map file. C2's "crash safety" requirement.
    const auto writeAtomic = [&](const std::filesystem::path& target,
                                 const std::vector<std::byte>& bytes) {
        const auto tmp = target.string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) throw std::runtime_error("cannot write " + tmp);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            if (!out) throw std::runtime_error("write failed on " + tmp);
        }
        std::filesystem::rename(tmp, target);
    };

    writeAtomic(lotpackPath(c), cell.writeLotPack());
    writeAtomic(lotheaderPath(c), cell.writeLotHeader());
    it->second.dirty = false;
}

int MapProject::saveAll() {
    int n = 0;
    for (const auto& c : dirtyCells()) { save(c); ++n; }
    return n;
}

} // namespace pzformat

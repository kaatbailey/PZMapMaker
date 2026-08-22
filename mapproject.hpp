// The working store, as decided in C1 §1.3: the game's own map directory IS the
// live format. No database, no intermediate project file. A MapProject opens a
// directory, enumerates its cells, loads them on demand into CellData, tracks
// which are dirty, evicts under an LRU cap, and flushes back to .lotpack /
// .lotheader byte-identically.
//
// Cell naming (confirmed against the Java tree): cell "X_Y" is
//   X_Y.lotheader   +   world_X_Y.lotpack
// in the map directory.
//
// This layer is Qt-free and dependency-free so it can be unit-tested against
// the same oracle as the rest of the port. The Qt shell (MainWindow) drives it;
// it does not drive Qt.
#pragma once

#include "celldata.hpp"
#include "celleditor.hpp"
#include "tileindex.hpp"

#include <filesystem>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pzformat {

/// Grid coordinate of a cell within a map.
struct CellCoord {
    int x = 0, y = 0;
    std::string name() const { return std::to_string(x) + "_" + std::to_string(y); }
    bool operator==(const CellCoord& o) const noexcept { return x == o.x && y == o.y; }
    bool operator<(const CellCoord& o) const noexcept {
        return x != o.x ? x < o.x : y < o.y;
    }
};

/// One loaded, editable cell: its data, an editor over it, and a dirty flag.
/// Held by the project's cache; handed out by reference, never copied.
struct LoadedCell {
    CellCoord coord;
    std::unique_ptr<CellData> data;
    std::unique_ptr<CellEditor> editor;
    bool dirty = false;
};

class MapProject {
public:
    /// Open a map directory and enumerate its cells (does not load tile data).
    /// tileIndex is borrowed for the lifetime of the project; editors need it.
    /// Throws if the directory does not exist or holds no cells.
    static MapProject open(const std::filesystem::path& mapDir, const TileIndex& tileIndex);

    const std::filesystem::path& dir() const noexcept { return dir_; }
    const std::vector<CellCoord>& cells() const noexcept { return cells_; }
    bool hasCell(const CellCoord& c) const;

    /// Load (or return already-loaded) cell. Marks it most-recently-used and may
    /// evict the least-recently-used clean cell if over the cache cap. Throws if
    /// the cell is not part of this map.
    LoadedCell& load(const CellCoord& c);

    /// Whether a cell is currently resident in the cache.
    bool isResident(const CellCoord& c) const;
    /// Currently resident cell count.
    std::size_t residentCount() const noexcept { return cache_.size(); }
    /// Cells with unsaved edits (resident and dirty).
    std::vector<CellCoord> dirtyCells() const;
    bool anyDirty() const;

    /// Mark a resident cell dirty. The shell calls this after an edit; the
    /// editor does not know about the project.
    void markDirty(const CellCoord& c);

    /// Flush one dirty cell to disk (byte-identical writeLotPack/writeLotHeader),
    /// clearing its dirty flag. No-op if not resident or not dirty.
    void save(const CellCoord& c);
    /// Flush every dirty cell. Returns the number written.
    int saveAll();

    /// Cache cap: dirty cells are never evicted (they would lose edits), so the
    /// resident set can exceed this if enough cells are dirty. Clean cells are
    /// evicted LRU-first once over the cap.
    void setCacheCap(std::size_t n) { cacheCap_ = n; enforceCap(); }
    std::size_t cacheCap() const noexcept { return cacheCap_; }

private:
    MapProject(std::filesystem::path dir, const TileIndex& ti)
        : dir_(std::move(dir)), tiles_(&ti) {}

    std::filesystem::path lotheaderPath(const CellCoord& c) const {
        return dir_ / (c.name() + ".lotheader");
    }
    std::filesystem::path lotpackPath(const CellCoord& c) const {
        return dir_ / ("world_" + c.name() + ".lotpack");
    }

    void touch(const CellCoord& c);   // move to MRU
    void enforceCap();                // evict clean LRU cells over the cap

    std::filesystem::path dir_;
    const TileIndex* tiles_;
    std::vector<CellCoord> cells_;

    // LRU: front = most recent. cache_ owns the cells; lru_ orders them.
    std::map<CellCoord, LoadedCell> cache_;
    std::list<CellCoord> lru_;
    std::size_t cacheCap_ = 64;
};

} // namespace pzformat

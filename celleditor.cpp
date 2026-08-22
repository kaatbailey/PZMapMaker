#include "celleditor.hpp"

#include <algorithm>
#include <stdexcept>

namespace pzformat {

// ---------------------------------------------------------- journal ---------

void CellEditor::begin(const std::string& label) {
    if (group_) {
        throw std::logic_error("group '" + group_->label + "' already open");
    }
    group_ = Edit{};
    group_->label = label;
}

CellEditor::Edit CellEditor::end() {
    if (!group_) throw std::logic_error("no open group");
    Edit e = std::move(*group_);
    group_.reset();
    if (!e.changes.empty()) {
        undo_.push_back(e);
        redo_.clear();
    }
    return e;
}

void CellEditor::record(Change c) {
    if (group_) { group_->changes.push_back(std::move(c)); return; }
    Edit e;
    e.label = "edit";
    e.changes.push_back(std::move(c));
    undo_.push_back(std::move(e));
    redo_.clear();
}

void CellEditor::apply(int x, int y, int z, std::vector<std::int32_t> newTiles,
                       bool newPresent, std::int32_t newRoom, const std::string& label) {
    const bool oldPresent = cell_.hasSquare(x, y, z);
    std::vector<std::int32_t> oldTiles;
    if (oldPresent) {
        const auto s = cell_.tilesAt(x, y, z);
        oldTiles.assign(s.begin(), s.end());
    }
    const std::int32_t oldRoom = cell_.roomAt(x, y, z);

    // No-op guard, matching the Java Arrays.equals check (and treating
    // presence as part of the identity).
    if (oldPresent == newPresent && oldTiles == newTiles && oldRoom == newRoom) return;

    if (newPresent) {
        cell_.setSquare(x, y, z, newTiles, newRoom);
    } else {
        cell_.clearSquare(x, y, z);
    }

    Change c;
    c.x = x; c.y = y; c.z = z;
    c.oldTiles = std::move(oldTiles); c.oldRoom = oldRoom; c.oldPresent = oldPresent;
    c.newTiles = std::move(newTiles); c.newRoom = newRoom; c.newPresent = newPresent;
    record(std::move(c));

    if (group_ && group_->label.empty()) group_->label = label;
}

std::optional<CellEditor::Edit> CellEditor::undo() {
    if (undo_.empty()) return std::nullopt;
    Edit e = std::move(undo_.back());
    undo_.pop_back();
    for (auto it = e.changes.rbegin(); it != e.changes.rend(); ++it) {
        if (it->oldPresent) cell_.setSquare(it->x, it->y, it->z, it->oldTiles, it->oldRoom);
        else                cell_.clearSquare(it->x, it->y, it->z);
    }
    redo_.push_back(e);
    return e;
}

std::optional<CellEditor::Edit> CellEditor::redo() {
    if (redo_.empty()) return std::nullopt;
    Edit e = std::move(redo_.back());
    redo_.pop_back();
    for (const auto& c : e.changes) {
        if (c.newPresent) cell_.setSquare(c.x, c.y, c.z, c.newTiles, c.newRoom);
        else              cell_.clearSquare(c.x, c.y, c.z);
    }
    undo_.push_back(e);
    return e;
}

// ---------------------------------------------------- layer operations ------

std::vector<std::int32_t> CellEditor::stack(int x, int y, int z) const {
    const auto s = cell_.tilesAt(x, y, z);
    return {s.begin(), s.end()};
}

std::optional<std::string> CellEditor::nameOf(std::int32_t idx) const {
    const auto& names = cell_.header().tileNames;
    if (idx >= 0 && static_cast<std::size_t>(idx) < names.size()) {
        return names[static_cast<std::size_t>(idx)];
    }
    return std::nullopt;
}

bool CellEditor::edgeMatches(const std::string& tileName, TileIndex::Edge edge) const {
    const TileIndex::Edge e = tiles_.edgeOf(tileName);
    return e == edge || e == TileIndex::Edge::Both;
}

void CellEditor::setFloor(int x, int y, int z, const std::string& tileName) {
    const std::int32_t idx = cell_.tileIndex(tileName);
    const auto cur = stack(x, y, z);
    std::vector<std::int32_t> out;
    out.reserve(cur.size() + 1);
    bool replaced = false;
    for (auto t : cur) {
        const auto n = nameOf(t);
        if (!replaced && n && tiles_.kindOf(*n) == TileIndex::Kind::Floor
            && !tiles_.isOverlay(*n)) {
            out.push_back(idx);
            replaced = true;
        } else {
            out.push_back(t);
        }
    }
    if (!replaced) out.insert(out.begin(), idx);
    apply(x, y, z, std::move(out), true, cell_.roomAt(x, y, z), "set floor");
}

void CellEditor::setWall(int x, int y, int z, TileIndex::Edge edge, const std::string& tileName) {
    if (edge != TileIndex::Edge::North && edge != TileIndex::Edge::West) {
        throw std::invalid_argument("edge must be North or West");
    }
    const std::int32_t idx = cell_.tileIndex(tileName);
    const auto cur = stack(x, y, z);
    std::vector<std::int32_t> out;
    out.reserve(cur.size() + 1);
    bool replaced = false;
    for (auto t : cur) {
        const auto n = nameOf(t);
        if (!replaced && n && tiles_.isStructuralWall(*n) && edgeMatches(*n, edge)) {
            out.push_back(idx);
            replaced = true;
        } else {
            out.push_back(t);
        }
    }
    if (!replaced) out.push_back(idx);
    apply(x, y, z, std::move(out), true, cell_.roomAt(x, y, z), "set wall");
}

void CellEditor::removeWall(int x, int y, int z, TileIndex::Edge edge) {
    const auto cur = stack(x, y, z);
    std::vector<std::int32_t> out;
    for (auto t : cur) {
        const auto n = nameOf(t);
        if (n && edgeMatches(*n, edge)
            && (tiles_.isStructuralWall(*n) || tiles_.isWallFixture(*n))) {
            continue;
        }
        out.push_back(t);
    }
    const bool present = !out.empty();
    apply(x, y, z, std::move(out), present, cell_.roomAt(x, y, z), "remove wall");
}

void CellEditor::addObject(int x, int y, int z, const std::string& tileName) {
    const std::int32_t idx = cell_.tileIndex(tileName);
    auto out = stack(x, y, z);
    out.push_back(idx);
    apply(x, y, z, std::move(out), true, cell_.roomAt(x, y, z), "add object");
}

void CellEditor::clearObjects(int x, int y, int z) {
    const auto cur = stack(x, y, z);
    std::vector<std::int32_t> out;
    for (auto t : cur) {
        const auto n = nameOf(t);
        if (!n) { out.push_back(t); continue; }
        const TileIndex::Kind k = tiles_.kindOf(*n);
        const bool structural = tiles_.isStructuralWall(*n) || tiles_.isWallFixture(*n)
            || (k == TileIndex::Kind::Floor && !tiles_.isOverlay(*n));
        if (structural) out.push_back(t);
    }
    const bool present = !out.empty();
    apply(x, y, z, std::move(out), present, cell_.roomAt(x, y, z), "clear objects");
}

void CellEditor::clearSquare(int x, int y, int z) {
    apply(x, y, z, {}, false, -1, "clear square");
}

void CellEditor::setRoom(int x, int y, int z, std::int32_t roomId) {
    apply(x, y, z, stack(x, y, z), cell_.hasSquare(x, y, z), roomId, "set room");
}

CellEditor::Edit CellEditor::fillFloor(int x0, int y0, int w, int h, int z,
                                       const std::string& tileName) {
    begin("fill floor " + std::to_string(w) + "x" + std::to_string(h));
    for (int x = x0; x < x0 + w; ++x) {
        for (int y = y0; y < y0 + h; ++y) {
            if (x < 0 || y < 0 || x >= cell_.cellSize() || y >= cell_.cellSize()) continue;
            setFloor(x, y, z, tileName);
        }
    }
    return end();
}

CellEditor::Edit CellEditor::outlineRoom(int x0, int y0, int w, int h, int z,
                                         const std::string& northTile,
                                         const std::string& westTile) {
    begin("outline room " + std::to_string(w) + "x" + std::to_string(h));
    for (int x = x0; x < x0 + w; ++x) {
        setWall(x, y0, z, TileIndex::Edge::North, northTile);
        setWall(x, y0 + h, z, TileIndex::Edge::North, northTile);
    }
    for (int y = y0; y < y0 + h; ++y) {
        setWall(x0, y, z, TileIndex::Edge::West, westTile);
        setWall(x0 + w, y, z, TileIndex::Edge::West, westTile);
    }
    return end();
}

} // namespace pzformat

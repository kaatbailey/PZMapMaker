// Port of CellEditor.java.
//
// Layer-aware editing over a CellData, with undo/redo.
//
// CellData::fill replaces a square's ENTIRE tile stack, which is why the
// in-game test punched holes through houses — walls and furniture sharing those
// squares went with the floor. That is the right behaviour for "clear", and the
// wrong behaviour for everything else. These operations target one layer and
// leave the rest alone, using TileIndex to tell the layers apart.
//
// Every mutation goes through apply(), so undo is uniform: an edit is just the
// before and after state of the squares it touched. Related changes can be
// grouped so a rectangle fill undoes in one step.
//
// Port notes:
//  - The Java used int[]/null for the tile stack; here a square's stack is a
//    std::vector<int32>, empty == the Java's null (a cleared square). The undo
//    journal owns copies of before/after, which is exactly what undo needs.
//  - begin()/end() grouping is preserved. A group with no changes is discarded,
//    matching the Java.
#pragma once

#include "celldata.hpp"
#include "square.hpp"
#include "tileindex.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace pzformat {

class CellEditor {
public:
    CellEditor(CellData& cell, const TileIndex& tiles) : cell_(cell), tiles_(tiles) {}

    CellData&        cell()  noexcept { return cell_; }
    const CellData&  cell()  const noexcept { return cell_; }
    const TileIndex& tiles() const noexcept { return tiles_; }

    // --- undo journal ---
    struct Change {
        int x, y, z;
        std::vector<std::int32_t> oldTiles, newTiles;
        std::int32_t oldRoom, newRoom;
        bool oldPresent, newPresent; // distinguishes empty-stack from cleared
    };

    struct Edit {
        std::string label;
        std::vector<Change> changes;
        int squaresTouched() const noexcept { return static_cast<int>(changes.size()); }
        std::string toString() const {
            return label + " (" + std::to_string(changes.size()) + " squares)";
        }
    };

    /// Group subsequent operations into a single undo step.
    void begin(const std::string& label);
    Edit end();

    bool canUndo() const noexcept { return !undo_.empty(); }
    bool canRedo() const noexcept { return !redo_.empty(); }
    int  undoDepth() const noexcept { return static_cast<int>(undo_.size()); }

    /// Returns the edit undone/redone, or nullopt if the stack was empty.
    std::optional<Edit> undo();
    std::optional<Edit> redo();

    // --- layer-aware operations ---
    /// Replace the floor, preserving walls, objects and overlays. If the square
    /// has no floor the new tile goes first, since floors draw beneath.
    void setFloor(int x, int y, int z, const std::string& tileName);

    /// Place or replace the wall on one edge, leaving the other edge and
    /// everything else. edge must be North or West.
    void setWall(int x, int y, int z, TileIndex::Edge edge, const std::string& tileName);

    /// Remove the wall on one edge, and any door leaf or pane mounted in it.
    void removeWall(int x, int y, int z, TileIndex::Edge edge);

    /// Add an object on top, leaving structure intact.
    void addObject(int x, int y, int z, const std::string& tileName);

    /// Remove every non-structural object, keeping floor, walls and fixtures.
    void clearObjects(int x, int y, int z);

    /// Remove everything. The destructive operation; the others are not.
    void clearSquare(int x, int y, int z);

    void setRoom(int x, int y, int z, std::int32_t roomId);

    /// Floor fill over a rectangle, as one undo step.
    Edit fillFloor(int x0, int y0, int w, int h, int z, const std::string& tileName);

    /// Wall around a rectangle: north walls on the top row, west on the left column.
    Edit outlineRoom(int x0, int y0, int w, int h, int z,
                     const std::string& northTile, const std::string& westTile);

    Square square(int x, int y, int z) const { return Square::at(cell_, tiles_, x, y, z); }

private:
    std::vector<std::int32_t> stack(int x, int y, int z) const;
    std::optional<std::string> nameOf(std::int32_t idx) const;
    bool edgeMatches(const std::string& tileName, TileIndex::Edge edge) const;

    // present=false means the square becomes empty (Java null). apply is the
    // single choke point every mutation passes through.
    void apply(int x, int y, int z, std::vector<std::int32_t> newTiles, bool newPresent,
               std::int32_t newRoom, const std::string& label);
    void record(Change c);

    CellData& cell_;
    const TileIndex& tiles_;

    std::deque<Edit> undo_;
    std::deque<Edit> redo_;
    std::optional<Edit> group_;
};

} // namespace pzformat

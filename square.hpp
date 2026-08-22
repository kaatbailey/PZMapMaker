// Port of Square.java.
//
// A semantic view of one square: what is on it, not which sprite indices.
//
// This is the layer an editor works against. "Replace the floor" should leave
// the walls alone; "place a door" needs to know which edge a wall sits on.
// CellData holds raw tile indices; this interprets them via TileIndex.
//
// Walls in Project Zomboid are EDGE-based: a wall belongs to the north or west
// edge of a square, so one square can carry both. Confirmed against room
// geometry at 99.5% / 100%.
#pragma once

#include "tileindex.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pzformat {

class CellData; // decode source; only tileNamesAt/roomAt are used

struct Square {
    int x = 0, y = 0, z = 0;
    std::vector<std::string> tiles;

    std::optional<std::string> floor;
    std::optional<std::string> northWall, westWall;
    /// Door leaves and window panes mounted in this square's walls.
    std::vector<std::string> fixtures;
    bool northIsDoorway = false, westIsDoorway = false;
    bool northIsWindow = false, westIsWindow = false;
    /// Decoration painted on the structure: grime, blood, rust. Kept separate.
    std::vector<std::string> overlays;
    std::vector<std::string> objects;
    std::vector<std::string> vegetation;
    bool hasDoor = false, hasWindow = false, blocksMovement = false;
    std::optional<std::string> containerType;
    int roomId = -1;

    /// Build the semantic view of (x,y,z) in a cell.
    static Square at(const CellData& cell, const TileIndex& ti, int x, int y, int actualZ);

    bool isEmpty() const noexcept { return tiles.empty(); }
    bool hasWall() const noexcept { return northWall.has_value() || westWall.has_value(); }
    bool indoors() const noexcept { return roomId >= 0; }

    std::string toString() const;
};

} // namespace pzformat

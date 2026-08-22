// Port of MapValidator.java.
//
// A4: Validate a map against the rules that make a building playable.
// Charter §1: "TileZed lets you paint an invalid map. This must not."
//
//   1. ROOM WITH NO EXIT       — no doorway on any perimeter edge (and no gap).
//   2. DOORWAY WITH NO ADJACENT FLOOR — door onto void.
//   3. WALL GAP THAT ISN'T A DOOR     — hole in perimeter (warning, not error;
//      the livingroom/kitchen open boundary is a valid gap).
//   4. ROOM WITH NO FLOOR      — square inside a room rect has no floor tile.
//   5. ROOM MEMBERSHIP MISMATCH — square inside a room rect has room id -1.
//
// Difference from the Java: that version was a main() that printed. Here the
// validator is a library that RETURNS findings; the editor and the tests
// consume the structured result, and a thin CLI formats it. This keeps the
// library UI-free (Charter §3) and lets the rules be tested without parsing
// stdout. The 256-tile hardcode is replaced with CellData::cellSize(), which is
// strictly more correct and identical for vanilla B42.
#pragma once

#include "celldata.hpp"
#include "tileindex.hpp"

#include <string>
#include <vector>

namespace pzformat {

class MapValidator {
public:
    enum class Severity { Error, Warning };

    struct Finding {
        Severity severity;
        std::string room;    ///< label: "kitchen #3 z=0"
        std::string message;
        int roomIndex = -1;
    };

    struct Report {
        std::vector<Finding> findings;
        int roomsChecked = 0;

        int errors() const noexcept {
            int n = 0;
            for (const auto& f : findings) if (f.severity == Severity::Error) ++n;
            return n;
        }
        int warnings() const noexcept {
            int n = 0;
            for (const auto& f : findings) if (f.severity == Severity::Warning) ++n;
            return n;
        }
        bool clean() const noexcept { return findings.empty(); }
    };

    /// Validate every room in one cell.
    static Report validate(const TileIndex& ti, const CellData& cell);

private:
    // All coordinate-level predicates. z is an ACTUAL level (matches Room.floor).
    static bool hasTileProp(const TileIndex& ti, const CellData& c,
                            int x, int y, int z, std::string_view prop);
    static bool hasDoorOnEdge(const TileIndex& ti, const CellData& c,
                              int x, int y, int z, bool north);
    static bool hasEdge(const TileIndex& ti, const CellData& c,
                        int x, int y, int z, bool north);
    static bool hasFloor(const TileIndex& ti, const CellData& c, int x, int y, int z);
    static bool insideRoom(const Room& room, int x, int y);
};

} // namespace pzformat

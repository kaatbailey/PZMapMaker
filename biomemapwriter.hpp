// Port of BiomeMapWriter.java (Track F, port step 7). 132 lines Java.
//
// Writes `maps/biomemap_X_Y.png`, the per-tile biome and zone map.
//
// CONFIRMED by decompiling zombie.iso.worldgen.maps.BiomeRaster:
//
//     private static final int NUM_BANDS = 2;
//     for (int i = 0; i < 2; i++)
//         this.pixels[x * 2 + i + y * span] = (byte) pixel[i];
//
// So per pixel: RED = biome index, GREEN = zone index, BLUE ignored. Both
// indices are looked up in the same `biome_map_config` table
// (media/lua/server/metazones/BiomeMapConfig.lua), via BiomeMap.getBiomeName()
// and getZoneName(). BiomeMap.Type is BIOME(0), ZONE(1) — a band selector,
// not a flag.
//
// One 256x256 image per cell, one pixel per tile. BiomeMap.getRaster searches
// every map named in IsoWorld.getMap() and takes the first file that exists; a
// missing file logs a debug line and returns null, so shipping these is safe
// and incremental.
//
// SCOPE, carried over from the Java: WorldGen only generates chunks where
// IsoChunk.hasEmptySquaresOnLevelZero() is true. GisCells fills every square of
// every chunk, so none of ours are generated and the BIOME band may currently
// do nothing for us. The ZONE band still matters — it drives foraging zones.
// Whether biome also feeds the authored path (genMapChunk) is UNVERIFIED.
//
// ------------------------------------------------------------------------
// PORT NOTES
// ------------------------------------------------------------------------
//
//   1. THE PNG ENCODER IS OURS, NOT Qt's. Java writes through ImageIO;
//      QImage does not reproduce its bytes and cannot be configured to
//      (pHYs chunk, per-row filter choice, zlib level, IDAT chunking — all
//      four differ). `pzpng` reproduces ImageIO exactly, measured on 200 of
//      200 buffers. `pzgen` also must build with no Qt at all, which forbids
//      QImage here independently. See pzpng.hpp.
//
//   2. THE BAND CONSTANTS ARE A DESIGN CHOICE, NOT A MEASUREMENT. Nothing in
//      the game says a GIS building footprint should be a TownZone. Unlike the
//      tile data elsewhere in this project these are free to change — but
//      changing them during a PORT would break the only oracle, so they are
//      transcribed exactly. §40.
//
//   3. THE `>= g.width` TEST USES THE RASTER'S BOUNDS, NOT THE CELL GRID'S.
//      A cell can extend past the imported area, and those pixels take
//      DEEP_FOREST so the mod's edge matches open country. Note the test is
//      one-sided: gx and gy are always non-negative here because the loop
//      starts at cell 0, so there is no lower bound check to mirror.
//
//   4. `write` returns the image count and its log lines reach the generator's
//      stdout, which the step 7 oracle compares. Java prints directly; the log
//      is captured into a vector here so the unit stays free of I/O policy —
//      same convention as TreeScatter::place.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "gisimport.hpp"

namespace pzformat {

class BiomeMapWriter {
public:
    // biome_map_config indices. PORT NOTE 2: design choices, transcribed.
    static constexpr int TOWN = 115;          ///< townhouse      / TownZone
    static constexpr int FARM = 128;          ///< farmmix_forest / Farm
    static constexpr int FARMLAND = 141;      ///< farmmix_forest / FarmLand
    static constexpr int FARM_FOREST = 204;   ///< farm_forest    / FarmForest
    static constexpr int PH_FOREST = 153;     ///< ph_forest      / PHForest
    static constexpr int BIRCH_FOREST = 217;  ///< birch_forest   / BirchForest
    static constexpr int DEEP_FOREST = 255;   ///< primary_forest / DeepForest
    static constexpr int DIRT = 254;          ///< dirt           / ForagingNav

    /// Distance bands out from any building or road, in tiles.
    static constexpr int TOWN_RADIUS = 10;
    static constexpr int EDGE_RADIUS = 28;
    static constexpr int FOREST_RADIUS = 70;

    /// @return number of images written
    static int write(const GisImport& g, const std::filesystem::path& mapDir,
                     int cellsX, int cellsY, int originCellX, int originCellY,
                     std::vector<std::string>& log);

    /// The pixel buffer for one cell, w*h*3 bytes. Split out so the oracle can
    /// digest and encode it without touching the filesystem — writing four PNGs
    /// to disk to compare them would test the filesystem as well as the port.
    static std::vector<unsigned char> cellPixels(const GisImport& g,
                                                 const std::vector<std::vector<int>>& dist,
                                                 int cx, int cy,
                                                 std::int64_t& town, std::int64_t& edge,
                                                 std::int64_t& forest, std::int64_t& deep,
                                                 std::int64_t& outside);

private:
    BiomeMapWriter() = delete;
};

} // namespace pzformat

// biomemapwriter.cpp — port of BiomeMapWriter.java. See the header for the
// four port notes; the one that matters is NOTE 1, the encoder.

#include "biomemapwriter.hpp"

#include <cstdio>
#include <fstream>

#include "pzpng.hpp"
#include "treescatter.hpp"

namespace pzformat {

namespace {
inline std::size_t uz(int v) { return static_cast<std::size_t>(v); }
} // namespace

std::vector<unsigned char> BiomeMapWriter::cellPixels(
        const GisImport& g, const std::vector<std::vector<int>>& dist,
        int cx, int cy,
        std::int64_t& town, std::int64_t& edge, std::int64_t& forest,
        std::int64_t& deep, std::int64_t& outside) {

    std::vector<unsigned char> rgb(256u * 256u * 3u, 0);
    const int ox = cx * 256, oy = cy * 256;

    // Java walks x then y and calls setRGB(x, y); the buffer here is row-major,
    // so the write index is (y * 256 + x) * 3. Transposing this would produce a
    // valid PNG of the wrong map, which no size or format check would catch.
    for (int x = 0; x < 256; x++)
        for (int y = 0; y < 256; y++) {
            const int gx = ox + x, gy = oy + y;
            int value;

            if (gx >= g.width || gy >= g.height) {
                // Beyond the imported area. Treat as the outermost band so the
                // edge of the mod matches open country rather than cutting to
                // something else. PORT NOTE 3.
                value = DEEP_FOREST;
                outside++;
            } else {
                const int d = dist[uz(gx)][uz(gy)];
                if (d <= TOWN_RADIUS) { value = TOWN; town++; }
                else if (d <= EDGE_RADIUS) { value = FARM_FOREST; edge++; }
                else if (d <= FOREST_RADIUS) { value = PH_FOREST; forest++; }
                else { value = DEEP_FOREST; deep++; }
            }

            // RED = biome band, GREEN = zone band, BLUE unread. Written to all
            // three because Java builds an INT_RGB pixel as (v<<16)|(v<<8)|v,
            // and the blue byte is part of the file even though nothing reads
            // it — dropping it would change the bytes.
            const std::size_t i = (uz(y) * 256u + uz(x)) * 3u;
            const unsigned char b = static_cast<unsigned char>(value);
            rgb[i] = b;
            rgb[i + 1] = b;
            rgb[i + 2] = b;
        }
    return rgb;
}

int BiomeMapWriter::write(const GisImport& g, const std::filesystem::path& mapDir,
                          int cellsX, int cellsY, int originCellX, int originCellY,
                          std::vector<std::string>& log) {

    const std::filesystem::path outDir = mapDir / "maps";
    std::filesystem::create_directories(outDir);

    const std::vector<std::vector<int>> dist = TreeScatter::distanceToStructure(g);
    std::int64_t town = 0, edge = 0, forest = 0, deep = 0, outside = 0;
    int written = 0;

    for (int cy = 0; cy < cellsY; cy++)
        for (int cx = 0; cx < cellsX; cx++) {
            const std::vector<unsigned char> rgb =
                cellPixels(g, dist, cx, cy, town, edge, forest, deep, outside);
            const std::vector<unsigned char> png = writePngRgb(rgb, 256, 256);

            const std::string name = "biomemap_" + std::to_string(originCellX + cx)
                                     + "_" + std::to_string(originCellY + cy) + ".png";
            std::ofstream f(outDir / name, std::ios::binary);
            f.write(reinterpret_cast<const char*>(png.data()),
                    static_cast<std::streamsize>(png.size()));
            written++;
        }

    // Text reaches the generator's stdout, which the step 7 oracle compares, so
    // the wording and Java's "%d, %d, ..." spacing are part of the contract.
    log.push_back("biome maps: " + std::to_string(written) + " written to "
                  + outDir.filename().string() + "/");
    char line[192];
    std::snprintf(line, sizeof(line),
                  "   town %lld, edge %lld, forest %lld, deep %lld, beyond-raster %lld",
                  static_cast<long long>(town), static_cast<long long>(edge),
                  static_cast<long long>(forest), static_cast<long long>(deep),
                  static_cast<long long>(outside));
    log.push_back(line);
    log.push_back("   R=biome G=zone, both indexed into biome_map_config");
    return written;
}

} // namespace pzformat

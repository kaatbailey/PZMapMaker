// Classification digest for every tile, sorted, for cross-language diff with
// ClassifyOracle.java. Same tab-separated line format so `diff` is exact.
//   classify <mediaDir> <out>
#include "tileindex.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace pzformat;

namespace {

const char* kindName(TileIndex::Kind k) {
    switch (k) {
        case TileIndex::Kind::Floor:      return "FLOOR";
        case TileIndex::Kind::Wall:       return "WALL";
        case TileIndex::Kind::Door:       return "DOOR";
        case TileIndex::Kind::Window:     return "WINDOW";
        case TileIndex::Kind::Object:     return "OBJECT";
        case TileIndex::Kind::Vegetation: return "VEGETATION";
        case TileIndex::Kind::Unknown:    return "UNKNOWN";
    }
    return "?";
}

const char* edgeName(TileIndex::Edge e) {
    switch (e) {
        case TileIndex::Edge::North: return "NORTH";
        case TileIndex::Edge::West:  return "WEST";
        case TileIndex::Edge::Both:  return "BOTH";
        case TileIndex::Edge::None:  return "NONE";
    }
    return "?";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3 || std::string(argv[1]) != "classify") {
        std::cerr << "usage: classify_oracle classify <mediaDir> <out>\n";
        return 2;
    }

    const TileIndex ti = TileIndex::load(argv[2]);
    std::vector<std::string> lines;
    lines.reserve(ti.size());
    for (const auto& name : ti.names()) {
        const int flags =
              (ti.isStructuralWall(name) ? 1 : 0)
            | (ti.isWallFixture(name)    ? 2 : 0)
            | (ti.isDoorway(name)        ? 4 : 0)
            | (ti.isWindowWall(name)     ? 8 : 0)
            | (ti.isOverlay(name)        ? 16 : 0)
            | (ti.blocksMovement(name)   ? 32 : 0);
        const auto ct = ti.containerType(name);
        lines.push_back(name + "\t" + kindName(ti.kindOf(name)) + "\t"
            + edgeName(ti.edgeOf(name)) + "\t" + edgeName(ti.decorationEdge(name)) + "\t"
            + std::to_string(flags) + "\t" + (ct ? *ct : "-"));
    }
    std::sort(lines.begin(), lines.end());

    std::ofstream out(argv[3]);
    for (const auto& l : lines) out << l << '\n';
    std::cout << "cpp  classify: " << lines.size() << " tiles, files=" << ti.fileCount
              << " tilesets=" << ti.tilesetCount << " -> " << argv[3] << '\n';
    return 0;
}

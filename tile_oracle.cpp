// Cross-language oracle for TileBin. See TileOracle.java.
//   emit  <out>   write a synthetic tdef with the confirmed retail layout
//   check <in>    read one the Java side wrote, verify content
#include "le.hpp"
#include "lew.hpp"
#include "tilebin.hpp"
#include "tiledefs.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace pzformat;
namespace fs = std::filesystem;

namespace {

std::vector<std::byte> synthTiles() {
    LEW w;
    w.ascii("tdef");
    w.i32(1).i32(1);
    w.nlString("advertising_01");
    w.nlString("advertising_01.png");
    w.i32(8).i32(16).i32(88);
    w.i32(2);
    w.i32(2);
    w.nlString("Facing").nlString("S");
    w.nlString("solid").nlString("");
    w.i32(3);
    w.nlString("Facing").nlString("W");
    w.nlString("container").nlString("crate");
    w.nlString("ContainerCapacity").nlString("10");
    return w.take();
}

} // namespace

int main(int argc, char** argv) {
    const std::string cmd = argc > 1 ? argv[1] : "";

    if (cmd == "emit") {
        const auto bytes = synthTiles();
        std::ofstream(argv[2], std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        const auto tb = TileBin::read(bytes, TileShape::CountOnly, 0);
        std::cout << "cpp  emit: " << bytes.size() << " bytes, " << tb.tilesetCount()
                  << " tilesets, " << tb.tileCount() << " tiles"
                  << ", adv_0 solid=" << (tb.find("advertising_01_0")->solid() ? "true" : "false")
                  << ", adv_1 container=" << *tb.find("advertising_01_1")->get("container")
                  << '\n';
        return 0;
    }

    if (cmd == "check") {
        const auto bytes = readAllBytes(argv[2]);
        const auto tb = TileBin::read(bytes, TileShape::CountOnly, 0);
        const Tile* t0 = tb.find("advertising_01_0");
        const Tile* t1 = tb.find("advertising_01_1");
        const bool ok = tb.tileCount() == 2 && t0 && t1
            && t0->solid()
            && t0->facing() == "S"
            && t1->get("container") == "crate"
            && t1->get("ContainerCapacity") == "10";
        std::cout << "cpp  check: read java tdef, " << bytes.size() << " bytes, "
                  << tb.tileCount() << " tiles, content " << (ok ? "MATCHES" : "DIFFERS") << '\n';
        return ok ? 0 : 1;
    }

    if (cmd == "solveall") {
        // Mirror of TileBin.solveAll: parse every binary .tiles with the
        // confirmed retail layout, and for each with a *.tiles.txt sibling,
        // confirm every text tile appears in the binary with identical props.
        // The text dump omits propertyless tiles, so the binary legitimately
        // holds more; absent-from-binary or differing props are the failures.
        const fs::path dir = argv[2];
        std::vector<fs::path> bins;
        for (const auto& e : fs::directory_iterator(dir)) {
            if (e.is_regular_file() && e.path().extension() == ".tiles") {
                bins.push_back(e.path());
            }
        }
        std::sort(bins.begin(), bins.end());
        std::cout << "binary .tiles files: " << bins.size() << "\n\n";

        int ok = 0, failed = 0, verified = 0, noTruth = 0;
        long long tiles = 0, textTiles = 0, matchedTiles = 0;

        for (const auto& b : bins) {
            const fs::path txt = b.string() + ".txt";
            try {
                const auto tb = TileBin::read(b, TileShape::CountOnly, 0);
                ++ok;
                tiles += static_cast<long long>(tb.tileCount());

                if (fs::exists(txt)) {
                    TileDefs truth;
                    truth.parse(txt);
                    int matched = 0, mismatched = 0, absent = 0;
                    std::string firstBad;
                    for (const auto& [name, ttile] : truth.ordered()) {
                        const Tile* g = tb.find(name);
                        if (!g) {
                            ++absent;
                            if (firstBad.empty()) firstBad = "absent: " + name;
                        } else if (!(ttile->props == g->props)) {
                            ++mismatched;
                            if (firstBad.empty()) firstBad = name + " props differ";
                        } else {
                            ++matched;
                        }
                    }
                    const bool same = absent == 0 && mismatched == 0;
                    if (same) ++verified;
                    textTiles += static_cast<long long>(truth.tileCount());
                    matchedTiles += matched;
                    std::printf("   %-42s %6zu tiles  %5zu in text  %s\n",
                        b.filename().string().c_str(), tb.tileCount(), truth.tileCount(),
                        same ? "ALL MATCH"
                             : (std::to_string(mismatched) + " differ, "
                                + std::to_string(absent) + " absent  " + firstBad).c_str());
                } else {
                    ++noTruth;
                    std::printf("   %-42s %6zu tiles      -  (no text sibling — mod case)\n",
                        b.filename().string().c_str(), tb.tileCount());
                }
            } catch (const std::exception& e) {
                ++failed;
                std::printf("   %-46s FAILED: %s\n", b.filename().string().c_str(), e.what());
            }
        }

        std::cout << "\n   parsed " << ok << " / " << bins.size()
                  << "   verified against text: " << verified
                  << "   no text sibling: " << noTruth << "   failed: " << failed << '\n';
        std::cout << "   total tiles: " << tiles << '\n';
        std::cout << "   text tiles cross-checked: " << matchedTiles << " / " << textTiles;
        if (matchedTiles == textTiles && textTiles > 0) {
            std::cout << "   => BINARY PARSER CONFIRMED across every file with a text sibling";
        }
        std::cout << '\n';
        return (failed == 0 && matchedTiles == textTiles) ? 0 : 1;
    }

    std::cerr << "usage: tile_oracle emit <out> | check <in> | solveall <dir>\n";
    return 2;
}

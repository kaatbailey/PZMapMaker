// Cross-language oracle for CellData. See CellOracle.java.
//   emit <pack> <hdr>                       write a synthetic cell pair
//   loadwrite <pack> <hdr>                  load, confirm byte round-trip
//   checkedit <pack> <hdr>                  confirm a Java-written fill is present
#include "celldata.hpp"
#include "le.hpp"
#include "lew.hpp"
#include "lotheader.hpp"
#include "lotpack.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>

using namespace pzformat;

namespace {

LotHeader synthHeader() {
    LotHeader h;
    h.b42 = true; h.version = 1;
    h.levelsAbove = 8; h.levelsBelow = 8;
    h.minLevel = -2; h.unknown12 = 5;
    h.tileNames = {"floors_exterior_natural_01_0", "walls_interior_house_01_11",
                   "blends_natural_01_64"};
    h.chunkGrid.assign(static_cast<std::size_t>(kGridBytes), std::byte{0});
    h.fullyConsumed = true;
    return h;
}

std::vector<std::byte> synthPack() {
    const int chunkCount = 4, levels = 8, spl = kSquaresPerLevel;
    auto body = [&](bool put) {
        LEW w; std::int32_t run = 0;
        for (int sq = 0; sq < levels * spl; ++sq) {
            if (put && sq == 0) {
                if (run > 0) { w.i32(-1).i32(run); run = 0; }
                w.i32(2).i32(7).i32(0);
            } else ++run;
        }
        if (run > 0) w.i32(-1).i32(run);
        return w.take();
    };
    std::vector<std::vector<std::byte>> bodies;
    bodies.push_back(body(true));
    for (int i = 1; i < chunkCount; ++i) bodies.push_back(body(false));
    LEW w;
    w.ascii("LOTP").i32(1).i32(chunkCount);
    std::int64_t off = 12 + 8 * chunkCount;
    for (const auto& b : bodies) { w.i64(off); off += static_cast<std::int64_t>(b.size()); }
    for (const auto& b : bodies) w.bytes(b);
    return w.take();
}

void writeFile(const char* path, std::span<const std::byte> b) {
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

} // namespace

int main(int argc, char** argv) {
    const std::string cmd = argc > 1 ? argv[1] : "";

    if (cmd == "emit") {
        writeFile(argv[2], synthPack());
        writeFile(argv[3], synthHeader().write());
        std::cout << "cpp  emit: wrote synthetic cell pair\n";
        return 0;
    }

    if (cmd == "loadwrite") {
        const auto pack = readAllBytes(argv[2]);
        const auto hdr = LotHeader::read(readAllBytes(argv[3]));
        CellData c = CellData::load(pack, hdr);
        const auto out = c.writeLotPack();
        const bool same = out.size() == pack.size()
            && std::equal(pack.begin(), pack.end(), out.begin());
        std::cout << "cpp  loadwrite: cellSize=" << c.cellSize()
                  << " levels=" << c.levelCount() << " minLevel=" << c.minLevel()
                  << " nonEmpty=" << c.nonEmptySquares()
                  << " round-trip " << (same ? "IDENTICAL" : "DIFFERS") << '\n';
        return same ? 0 : 1;
    }

    if (cmd == "checkedit") {
        const auto pack = readAllBytes(argv[2]);
        const auto hdr = LotHeader::read(readAllBytes(argv[3]));
        CellData c = CellData::load(pack, hdr);
        const auto n = c.tileNamesAt(3, 3, 0);
        const bool ok = n.size() == 1 && n[0] == "oracle_fill_01_0";
        std::cout << "cpp  checkedit: fill present=" << (ok ? "true" : "false")
                  << " nonEmpty=" << c.nonEmptySquares() << '\n';
        return ok ? 0 : 1;
    }

    std::cerr << "usage: cell_oracle emit|loadwrite|checkedit <pack> <hdr> [editpack] [edithdr]\n";
    return 2;
}

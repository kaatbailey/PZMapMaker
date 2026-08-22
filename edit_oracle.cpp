// Cross-language oracle for CellEditor. See EditOracle.java. Empty TileIndex so
// it runs without real .tiles; the script exercises journal, grouping, undo,
// redo and classification-independent ops. Both trees must emit identical bytes.
//   emit <pack> <hdr>
#include "celldata.hpp"
#include "celleditor.hpp"
#include "lotheader.hpp"
#include "tileindex.hpp"

#include <fstream>
#include <iostream>

using namespace pzformat;

namespace {
void writeFile(const char* path, std::span<const std::byte> b) {
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::cerr << "usage: edit_oracle <pack> <hdr>\n"; return 2; }

    TileIndex ti; // empty
    LotHeader h = CellData::newHeader({"t0", "t1", "t2", "t3"}, 0, 7);
    CellData c = CellData::blank(std::move(h), 4);
    CellEditor ed(c, ti);

    ed.begin("script");
    ed.setFloor(2, 2, 0, "t0");
    ed.setFloor(2, 2, 0, "t1");
    ed.addObject(2, 2, 0, "t2");
    ed.addObject(3, 3, 0, "t3");
    ed.setRoom(3, 3, 0, 9);
    ed.end();
    ed.clearSquare(3, 3, 0);
    ed.undo();
    ed.undo();
    ed.redo();

    writeFile(argv[1], c.writeLotPack());
    writeFile(argv[2], c.writeLotHeader());
    std::cout << "cpp  edit: undoDepth=" << ed.undoDepth()
              << " canRedo=" << (ed.canRedo() ? "true" : "false")
              << " nonEmpty=" << c.nonEmptySquares() << '\n';
    return 0;
}

// Cross-language oracle, C++ side. See Oracle.java.
//
//   check <in> <out>   read a Java-written lotheader, re-write it, report match
//   emitpack <out>     write the synthetic 2x2-chunk lotpack for Java to verify
#include "le.hpp"
#include "lew.hpp"
#include "lotheader.hpp"
#include "lotpack.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace pzformat;

namespace {

void writeFile(const std::string& path, std::span<const std::byte> bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

std::string firstDiff(std::span<const std::byte> a, std::span<const std::byte> b) {
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "offset %zu: %02X vs %02X", i,
                          std::to_integer<unsigned>(a[i]), std::to_integer<unsigned>(b[i]));
            return buf;
        }
    }
    return "length " + std::to_string(a.size()) + " vs " + std::to_string(b.size());
}

// Same synthetic cell as tests/test_lotpack.cpp.
std::vector<std::byte> synthCell() {
    auto body0 = [] { LEW w; w.i32(2).i32(5).i32(7);   w.i32(-1).i32(63); return w.take(); };
    auto body1 = [] { LEW w; w.i32(-1).i32(64);
                      w.i32(4).i32(9).i32(11).i32(12).i32(13);
                      w.i32(-1).i32(63); return w.take(); };
    auto body2 = [] { LEW w; w.i32(1).i32(3);          w.i32(-1).i32(63); return w.take(); };
    auto body3 = [] { LEW w; w.i32(-1).i32(63); w.i32(2).i32(-1).i32(42); return w.take(); };

    const std::vector<std::vector<std::byte>> bodies{body0(), body1(), body2(), body3()};
    LEW w;
    w.ascii("LOTP").i32(1).i32(4);
    std::int64_t off = 12 + 8 * 4;
    for (const auto& b : bodies) { w.i64(off); off += static_cast<std::int64_t>(b.size()); }
    for (const auto& b : bodies) w.bytes(b);
    return w.take();
}

} // namespace

int main(int argc, char** argv) {
    const std::string cmd = argc > 1 ? argv[1] : "";

    if (cmd == "check") {
        const auto fromJava = readAllBytes(argv[2]);
        const LotHeader h = LotHeader::read(fromJava);
        const auto rewritten = h.write();

        const bool same = rewritten.size() == fromJava.size()
            && std::equal(fromJava.begin(), fromJava.end(), rewritten.begin());

        std::cout << "cpp  check: read java output, " << fromJava.size()
                  << " bytes, re-write " << (same ? "IDENTICAL" : "DIFFERS")
                  << ", b42=" << (h.b42 ? "true" : "false")
                  << ", tiles=" << h.tileNames.size()
                  << ", rooms=" << h.rooms.size()
                  << ", buildings=" << h.buildings.size()
                  << ", roomRefs=" << h.roomRefs()
                  << ", minLevel=" << h.minLevel
                  << ", levelCount=" << h.levelCount()
                  << ", fullyConsumed=" << (h.fullyConsumed ? "true" : "false")
                  << '\n';

        if (!same) {
            std::cout << "  first diff at " << firstDiff(fromJava, rewritten) << '\n';
            return 1;
        }

        // Spot-check decoded content, not just byte agreement: identical bytes
        // could still mean both sides mis-parsed identically, but these values
        // were chosen by the Java side and are checked here.
        bool ok = true;
        auto expect = [&](bool c, const char* what) {
            if (!c) { std::cout << "  content check failed: " << what << '\n'; ok = false; }
        };
        expect(h.tileNames.size() == 5, "tile count");
        expect(h.tileNames[4].empty(), "empty tile name survives");
        expect(h.tileNames[3] == std::string("tile\x80\xFF_high_bytes"), "high-byte tile name");
        expect(h.minLevel == -2, "negative minLevel");
        expect(h.levelCount() == 10, "levelCount = 7 - (-2) + 1");
        expect(h.rooms.size() == 3, "room count");
        expect(h.rooms[0].rects.size() == 2, "room 0 rect count");
        expect(h.rooms[0].rects[1].w == 3, "room 0 rect 1 w");
        expect(h.rooms[0].objects[1].a == -1, "negative object field");
        expect(h.rooms[2].name.empty(), "empty room name");
        expect(h.rooms[2].floor == -2, "negative floor");
        expect(h.buildings.size() == 3, "building count");
        expect(h.buildings[2].empty(), "zero-room building");
        expect(h.roomRefs() == 3, "room refs");
        expect(h.chunkGrid.size() == 1024, "grid size");
        expect(std::to_integer<unsigned>(h.chunkGrid[1]) == 10u, "grid byte 1 = 1*7+3");
        if (!ok) return 1;

        writeFile(argv[3], rewritten);
        std::cout << "cpp  check: content assertions passed\n";
        return 0;
    }

    if (cmd == "emitpack") {
        const auto cell = synthCell();
        writeFile(argv[2], cell);
        std::cout << "cpp  emitpack: " << cell.size() << " bytes\n";
        return 0;
    }

    std::cerr << "usage: oracle check <in> <out> | oracle emitpack <out>\n";
    return 2;
}

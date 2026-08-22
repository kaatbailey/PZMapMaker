// Cross-language oracle for PackFile, plus the sprite-name independent check.
//   emit  <out>       write a synthetic legacy pack for Java to verify
//   spritecount <dir> load every .pack and report the name-set size
#include "le.hpp"
#include "lew.hpp"
#include "packfile.hpp"
#include "spritenames.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>

using namespace pzformat;

namespace {

std::vector<std::byte> minimalPng(std::uint32_t w, std::uint32_t h) {
    LEW s;
    for (int b : {0x89, int('P'), int('N'), int('G'), 0x0D, 0x0A, 0x1A, 0x0A}) s.u8(b);
    auto be = [&](std::uint32_t v) {
        s.u8(static_cast<int>((v >> 24) & 0xFF)).u8(static_cast<int>((v >> 16) & 0xFF))
         .u8(static_cast<int>((v >> 8) & 0xFF)).u8(static_cast<int>(v & 0xFF));
    };
    be(13); s.ascii("IHDR"); be(w); be(h); s.u8(8).u8(6).u8(0).u8(0).u8(0); be(0);
    be(0);  s.ascii("IEND"); be(0);
    return s.take();
}

std::vector<std::byte> synthLegacyPack() {
    const auto png0 = minimalPng(16, 16);
    const auto png1 = minimalPng(8, 8);
    LEW w;
    w.i32(2);
    w.lenString("JumboTrees1x");
    w.i32(1).i32(1);
    w.lenString("jumbo_0");
    w.i32(0).i32(0).i32(16).i32(16).i32(0).i32(0).i32(16).i32(16);
    w.bytes(png0);
    w.i32(static_cast<std::int32_t>(0xDEADBEEF));
    w.lenString("JumboTrees2x");
    w.i32(1).i32(1);
    w.lenString("jumbo2_0");
    w.i32(0).i32(0).i32(8).i32(8).i32(0).i32(0).i32(8).i32(8);
    w.bytes(png1);
    return w.take();
}

} // namespace

int main(int argc, char** argv) {
    const std::string cmd = argc > 1 ? argv[1] : "";

    if (cmd == "emit") {
        const auto bytes = synthLegacyPack();
        std::ofstream(argv[2], std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        const auto pf = PackFile::read(bytes);
        std::cout << "cpp  emit: " << bytes.size() << " bytes, " << pf.pages().size()
                  << " pages, pzpk=" << (pf.pzpk() ? "true" : "false") << '\n';
        return 0;
    }

    if (cmd == "spritecount") {
        const auto r = loadSpriteNames(argv[2]);
        std::cout << "cpp  spritecount: " << r.names.size()
                  << " names from " << r.packsRead << "/" << r.packsTotal << " packs"
                  << ", vegetation_trees_01_0 present="
                  << (r.names.count("vegetation_trees_01_0") ? "true" : "false") << '\n';
        if (!r.failedPacks.empty()) {
            std::cout << "   unparsed packs: " << r.failedPacks.size() << '\n';
        }
        return 0;
    }

    if (cmd == "grep") {
        // List every sprite name beginning with argv[3], sorted. Confirms what
        // a given sheet's sprites are actually called.
        const auto r = loadSpriteNames(argv[2]);
        const std::string prefix = argc > 3 ? argv[3] : "";
        std::vector<std::string> hits;
        for (const auto& n : r.names) {
            if (n.rfind(prefix, 0) == 0) hits.push_back(n);
        }
        std::sort(hits.begin(), hits.end());
        std::cout << hits.size() << " names matching '" << prefix << "' (of "
                  << r.names.size() << " total):\n";
        for (const auto& n : hits) std::cout << "   " << n << '\n';
        return 0;
    }

    std::cerr << "usage: pack_oracle emit <out> | spritecount <dir> | grep <dir> <prefix>\n";
    return 2;
}

#include "packfile.hpp"

#include "lew.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace pzformat {

namespace {

std::string hexPrefix(std::span<const std::byte> a) {
    std::string s;
    char buf[4];
    const std::size_t n = std::min<std::size_t>(8, a.size());
    for (std::size_t i = 0; i < n; ++i) {
        std::snprintf(buf, sizeof buf, "%02X ", std::to_integer<unsigned>(a[i]));
        s += buf;
    }
    if (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

void requirePngMagic(const PackFile::Page& page, std::size_t pngStart,
                     bool (*isMagic)(std::span<const std::byte>)) {
    if (!isMagic(page.png)) {
        throw ParseError("page '" + page.name + "': expected PNG magic at offset "
                         + std::to_string(pngStart) + " but found " + hexPrefix(page.png)
                         + " -- entry table for this page was misparsed");
    }
}

} // namespace

bool PackFile::startsWithPngMagic(std::span<const std::byte> a) {
    return a.size() >= kPngMagic.size()
        && std::equal(kPngMagic.begin(), kPngMagic.end(), a.begin());
}

PackFile PackFile::read(const std::filesystem::path& file) {
    return read(readAllBytes(file));
}

PackFile PackFile::read(std::span<const std::byte> data) {
    PackFile pack;
    LE r(data);

    // B42 prepends "PZPK" + version. B41 files start straight at the page
    // count. Both still ship in 42.20.
    const auto lead = r.view(4);
    if (std::equal(lead.begin(), lead.end(), reinterpret_cast<const std::byte*>(kMagic))) {
        pack.pzpk_ = true;
        pack.version_ = r.i32();
    } else {
        r.seek(0);
    }

    const std::int32_t numPages = r.i32();
    if (numPages < 0 || numPages > 100'000) {
        throw ParseError("implausible page count " + std::to_string(numPages)
                         + " -- this is probably not a .pack file, or the layout changed");
    }

    pack.pages_.reserve(static_cast<std::size_t>(numPages));
    pack.pageUnknown_.reserve(static_cast<std::size_t>(numPages));
    pack.pageSeparator_.reserve(static_cast<std::size_t>(numPages));

    for (std::int32_t i = 0; i < numPages; ++i) {
        Page page;
        page.name = r.lenString();
        const std::int32_t numEntries = r.i32();
        pack.pageUnknown_.push_back(r.i32());
        if (numEntries < 0 || numEntries > 1'000'000) {
            throw ParseError("page '" + page.name + "': implausible entry count "
                             + std::to_string(numEntries) + " at offset "
                             + std::to_string(r.pos() - 8));
        }

        page.entries.reserve(static_cast<std::size_t>(numEntries));
        for (std::int32_t j = 0; j < numEntries; ++j) {
            Entry e;
            e.name = r.lenString();
            e.x = r.i32();  e.y = r.i32();  e.w = r.i32();  e.h = r.i32();
            e.ox = r.i32(); e.oy = r.i32(); e.fx = r.i32(); e.fy = r.i32();
            page.entries.push_back(std::move(e));
        }

        if (pack.pzpk_) {
            const std::int32_t pngLen = r.i32();
            const std::size_t pngStart = r.pos();
            if (pngLen < 0 || static_cast<std::size_t>(pngLen) > r.remaining()) {
                throw ParseError("page '" + page.name + "': bad PNG length "
                                 + std::to_string(pngLen) + " at offset "
                                 + std::to_string(pngStart - 4) + " (only "
                                 + std::to_string(r.remaining()) + " bytes left)");
            }
            page.png = r.bytes(static_cast<std::size_t>(pngLen));
            requirePngMagic(page, pngStart, &startsWithPngMagic);
            pack.pageSeparator_.push_back(false);
        } else {
            const std::size_t pngStart = r.pos();
            const std::size_t pngLen = legacyPngLength(r, pngStart, page.name);
            page.png = r.bytes(pngLen);
            requirePngMagic(page, pngStart, &startsWithPngMagic);

            // Separator between pages; absent after the last one in every
            // observed file. Consumed only if actually present.
            bool sep = false;
            if (r.remaining() >= 4) {
                const std::size_t save = r.pos();
                if (static_cast<std::uint32_t>(r.i32()) == kPageSeparator) {
                    sep = true;
                } else {
                    r.seek(save);
                }
            }
            pack.pageSeparator_.push_back(sep);
        }
        pack.pages_.push_back(std::move(page));
    }
    return pack;
}

std::size_t PackFile::legacyPngLength(LE& r, std::size_t start, const std::string& pageName) {
    // Walk chunk headers from past the 8-byte signature to IEND. The legacy
    // layout carries no length prefix. Leaves the cursor back at `start`.
    std::size_t p = start + 8;
    while (true) {
        r.seek(p);
        if (r.remaining() < 8) {
            throw ParseError("page '" + pageName + "': PNG starting at "
                             + std::to_string(start) + " has no IEND chunk");
        }
        const auto hdr = r.view(8);
        const std::int32_t len = static_cast<std::int32_t>(
              (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(hdr[0])) << 24)
            | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(hdr[1])) << 16)
            | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(hdr[2])) << 8)
            |  static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(hdr[3])));
        char type[5] = {
            static_cast<char>(std::to_integer<std::uint8_t>(hdr[4])),
            static_cast<char>(std::to_integer<std::uint8_t>(hdr[5])),
            static_cast<char>(std::to_integer<std::uint8_t>(hdr[6])),
            static_cast<char>(std::to_integer<std::uint8_t>(hdr[7])),
            '\0',
        };
        if (len < 0) {
            throw ParseError("page '" + pageName + "': PNG chunk '" + type + "' at "
                             + std::to_string(p) + " has negative length "
                             + std::to_string(len));
        }
        p += 8 + static_cast<std::size_t>(len) + 4; // header + data + CRC
        if (std::string_view(type, 4) == "IEND") break;
    }
    r.seek(start);
    return p - start;
}

std::vector<std::byte> PackFile::write() const {
    LEW w;
    if (pzpk_) {
        w.ascii(std::string_view(kMagic, 4));
        w.i32(version_);
    }
    w.i32(static_cast<std::int32_t>(pages_.size()));

    for (std::size_t i = 0; i < pages_.size(); ++i) {
        const Page& page = pages_[i];
        w.lenString(page.name);
        w.i32(static_cast<std::int32_t>(page.entries.size()));
        w.i32(i < pageUnknown_.size() ? pageUnknown_[i] : 1);

        for (const auto& e : page.entries) {
            w.lenString(e.name);
            w.i32(e.x).i32(e.y).i32(e.w).i32(e.h);
            w.i32(e.ox).i32(e.oy).i32(e.fx).i32(e.fy);
        }

        if (pzpk_) {
            w.i32(static_cast<std::int32_t>(page.png.size()));
            w.bytes(page.png);
        } else {
            w.bytes(page.png);
            const bool sep = i < pageSeparator_.size()
                ? pageSeparator_[i]
                : (i < pages_.size() - 1);
            if (sep) w.i32(static_cast<std::int32_t>(kPageSeparator));
        }
    }
    return w.take();
}

void PackFile::extractPages(const std::filesystem::path& dir) const {
    std::filesystem::create_directories(dir);
    for (const auto& p : pages_) {
        std::string safe;
        for (char c : p.name) {
            const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                         || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
            safe.push_back(ok ? c : '_');
        }
        std::ofstream out(dir / (safe + ".png"), std::ios::binary);
        out.write(reinterpret_cast<const char*>(p.png.data()),
                  static_cast<std::streamsize>(p.png.size()));
    }
}

} // namespace pzformat

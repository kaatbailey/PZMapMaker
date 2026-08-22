// Port of PackFile.java.
//
// Project Zomboid .pack texture atlas.
//
// TWO LAYOUTS ship in 42.20 and both are handled:
//
//   B42 ("PZPK"):  char[4] "PZPK", int32 version, int32 numPages.
//                  Each page's PNG is length-prefixed.
//   B41 (legacy):  starts directly at numPages. Each page's PNG has NO length
//                  prefix -- you walk its chunks to IEND -- and pages are
//                  separated by the sentinel 0xDEADBEEF.
//
// The entry table is identical in both, AND SO IS the per-page int32 that
// follows numEntries. An earlier reader believed that int32 was PZPK-only;
// that one wrong assumption is why all 11 retail legacy atlases failed -- it
// skipped the field, then read its value as the first entry's name length and
// derailed on byte one. JumboTrees1x/2x.pack are legacy, so every tile in
// vegetation_trees_01 appeared to have no sprite.
//
//   [optional] char[4]   "PZPK"
//   [optional] int32     version
//   int32                numPages
//   page * numPages:
//       lenString        pageName
//       int32            numEntries
//       int32            unknown            present in BOTH layouts, carried opaquely
//       entry * numEntries:
//           lenString    entryName
//           int32 x, y, w, h, ox, oy, fx, fy
//       [PZPK]  int32    pngByteLength
//       byte[]           pngBytes           (magic 89 50 4E 47)
//       [legacy] int32   0xDEADBEEF         page separator
//
// The PNG magic check at the end of each page is the self-validating anchor: if
// the page parses and lands exactly on a PNG header, the entry table was read
// correctly. Round-tripping proves reader and writer agree, nothing more; the
// independent check is the sprite join (SpriteNames should rise well above
// 45,028 and vegetation_trees_01 tiles should resolve).
#pragma once

#include "le.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace pzformat {

class PackFile {
public:
    static constexpr std::array<std::byte, 8> kPngMagic = {
        std::byte{0x89}, std::byte{'P'}, std::byte{'N'}, std::byte{'G'},
        std::byte{0x0D}, std::byte{0x0A}, std::byte{0x1A}, std::byte{0x0A},
    };
    static constexpr char kMagic[4] = {'P', 'Z', 'P', 'K'};
    static constexpr std::uint32_t kPageSeparator = 0xDEADBEEFu;

    struct Entry {
        std::string name;
        std::int32_t x = 0, y = 0, w = 0, h = 0, ox = 0, oy = 0, fx = 0, fy = 0;
    };

    struct Page {
        std::string name;
        std::vector<Entry> entries;
        std::vector<std::byte> png;

        /// PNG IHDR width/height are big-endian at byte offsets 16 and 20.
        std::int32_t pngWidth()  const { return png.empty() ? -1 : beInt(16); }
        std::int32_t pngHeight() const { return png.empty() ? -1 : beInt(20); }

    private:
        std::int32_t beInt(std::size_t o) const {
            return static_cast<std::int32_t>(
                  (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(png[o]))     << 24)
                | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(png[o + 1])) << 16)
                | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(png[o + 2])) << 8)
                |  static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(png[o + 3])));
        }
    };

    /// Reads the whole file into memory. Prefer the span overload with a
    /// MappedFile when scanning many packs.
    static PackFile read(const std::filesystem::path& file);
    static PackFile read(std::span<const std::byte> data);

    /// Serialise back to bytes. Round-trips a retail pack byte-identically.
    std::vector<std::byte> write() const;

    /// Extract every page atlas as PageName.png into dir.
    void extractPages(const std::filesystem::path& dir) const;

    const std::vector<Page>& pages() const noexcept { return pages_; }

    bool pzpk() const noexcept { return pzpk_; }
    std::int32_t version() const noexcept { return version_; }
    /// Per-page opaque int32, present in both layouts.
    const std::vector<std::int32_t>& pageUnknown() const noexcept { return pageUnknown_; }
    /// Whether each page was followed by the 0xDEADBEEF separator (legacy).
    /// Recorded rather than derived, so a file ending in one round-trips.
    const std::vector<bool>& pageSeparator() const noexcept { return pageSeparator_; }

private:
    static std::size_t legacyPngLength(LE& r, std::size_t start, const std::string& pageName);
    static bool startsWithPngMagic(std::span<const std::byte> a);

    std::vector<Page> pages_;
    std::vector<std::int32_t> pageUnknown_;
    std::vector<bool> pageSeparator_;
    bool pzpk_ = false;
    std::int32_t version_ = 0;
};

} // namespace pzformat

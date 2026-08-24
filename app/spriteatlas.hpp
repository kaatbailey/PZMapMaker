// SpriteAtlas — app-layer sprite loader for the C3 viewport (step 4).
//
// Bridges the dependency-free library (PackFile, which yields PNG bytes + entry
// rectangles) to the GL viewport (which needs RGBA pixels in a texture array).
// PNG decode is done with QImage — Qt is already linked in the app layer, so the
// library stays C++20 dependency-free. Nothing here is committed to the oracle.
//
// STRATEGY (matters at scale): a full media dir holds ~46,540 sprites across
// dozens of packs; a single cell uses ~100. So we do NOT decode everything. We
// build a lightweight index first (name -> which pack, which page, which rect),
// which only reads entry tables, not pixels. Then buildLayers() decodes just the
// pages that the requested names touch, and blits only the needed sprite rects.
//
// The name match is exact: a cell's LotHeader.tileNames entries ARE pack entry
// names (see spritenames.cpp — the sprite-name set is exactly the union of pack
// entry names). A name with no pack entry is the "authored tile with no atlas
// sprite" case STATE flagged; buildLayers reports it rather than guessing.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace pzmm {

class SpriteAtlas {
public:
    // One RGBA sprite ready to upload as an atlas layer.
    struct Layer {
        int w = 0, h = 0;             // trimmed sprite pixel size (from pack entry)
        int ox = 0, oy = 0;           // sprite offset within its logical tile
        int fx = 0, fy = 0;           // logical tile size (64x128 for floors,
                                       // 128x256 for tall walls, etc.). PZ places
                                       // sprite top-left at anchor - (fx/2, fy-32) + (ox, oy).
        std::vector<std::uint8_t> rgba;  // w*h*4, or empty if the name had no sprite
        bool found = false;
    };

    // Index every .pack in dir: name -> (pack path, page index, entry index).
    // Reads entry tables only (no PNG decode). Returns the number of names
    // indexed, or throws if the dir has no readable packs.
    std::size_t indexDir(const std::filesystem::path& texturePackDir);

    bool ready() const noexcept { return !loc_.empty(); }
    std::size_t nameCount() const noexcept { return loc_.size(); }

    // Decode + blit the sprites for exactly these names, in order. layers[i]
    // corresponds to names[i]; a name with no pack entry yields found=false.
    // Pages are decoded lazily and cached for the duration of the call.
    std::vector<Layer> buildLayers(const std::vector<std::string>& names);

    // How many of the last buildLayers names had no sprite (diagnostic).
    int lastMissing() const noexcept { return lastMissing_; }

private:
    struct Loc { std::filesystem::path pack; int page; int entry; };
    std::unordered_map<std::string, Loc> loc_;
    int lastMissing_ = 0;
};

} // namespace pzmm

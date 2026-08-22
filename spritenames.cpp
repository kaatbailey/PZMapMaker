#include "spritenames.hpp"

#include "le.hpp"
#include "packfile.hpp"

#include <algorithm>
#include <stdexcept>

namespace pzformat {

SpriteNamesResult loadSpriteNames(const std::filesystem::path& texturePackDir) {
    std::vector<std::filesystem::path> packs;
    for (const auto& e : std::filesystem::directory_iterator(texturePackDir)) {
        if (e.is_regular_file() && e.path().extension() == ".pack") {
            packs.push_back(e.path());
        }
    }
    std::sort(packs.begin(), packs.end());

    SpriteNamesResult out;
    out.packsTotal = static_cast<int>(packs.size());

    for (const auto& p : packs) {
        try {
            const PackFile pf = PackFile::read(p);
            for (const auto& page : pf.pages()) {
                for (const auto& e : page.entries) out.names.insert(e.name);
            }
            ++out.packsRead;
        } catch (const std::exception&) {
            out.failedPacks.push_back(p.filename().string());
        }
    }

    if (out.names.empty()) {
        throw std::runtime_error("no sprite names loaded from " + texturePackDir.string()
                                 + " -- palette validation would pass vacuously");
    }
    return out;
}

} // namespace pzformat

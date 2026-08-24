#include "spriteatlas.hpp"

#include "packfile.hpp"

#include <QImage>

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace pzmm {

std::size_t SpriteAtlas::indexDir(const std::filesystem::path& dir) {
    loc_.clear();
    std::vector<std::filesystem::path> packs;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        if (e.is_regular_file() && e.path().extension() == ".pack")
            packs.push_back(e.path());
    }
    std::sort(packs.begin(), packs.end());

    int readOk = 0;
    for (const auto& p : packs) {
        try {
            const pzformat::PackFile pf = pzformat::PackFile::read(p);
            for (int pi = 0; pi < static_cast<int>(pf.pages().size()); ++pi) {
                const auto& page = pf.pages()[pi];
                for (int ei = 0; ei < static_cast<int>(page.entries.size()); ++ei) {
                    // First writer wins on duplicate names (matches SpriteNames
                    // set semantics; duplicates across packs are rare/benign).
                    loc_.emplace(page.entries[ei].name, Loc{p, pi, ei});
                }
            }
            ++readOk;
        } catch (const std::exception&) {
            // Skip unreadable packs; indexDir succeeds if any pack read.
        }
    }
    if (loc_.empty())
        throw std::runtime_error("no sprite names indexed from " + dir.string());
    std::printf("[SpriteAtlas] indexed %zu sprite names from %d/%zu packs\n",
                loc_.size(), readOk, packs.size());
    std::fflush(stdout);
    return loc_.size();
}

std::vector<SpriteAtlas::Layer>
SpriteAtlas::buildLayers(const std::vector<std::string>& names) {
    lastMissing_ = 0;
    std::vector<Layer> out(names.size());

    // Group requested names by pack so each pack is opened once, and within a
    // pack decode a page's PNG only the first time it is needed.
    // packPath -> list of (outIndex, name)
    std::unordered_map<std::string, std::vector<std::pair<int, std::string>>> byPack;
    for (int i = 0; i < static_cast<int>(names.size()); ++i) {
        auto it = loc_.find(names[i]);
        if (it == loc_.end()) { out[i].found = false; ++lastMissing_;
            std::printf("[SpriteAtlas] MISSING: %s\n", names[i].c_str());
            continue; }
        byPack[it->second.pack.string()].emplace_back(i, names[i]);
    }

    for (auto& [packStr, wanted] : byPack) {
        pzformat::PackFile pf;
        try {
            pf = pzformat::PackFile::read(std::filesystem::path(packStr));
        } catch (const std::exception&) {
            for (auto& [oi, nm] : wanted) { out[oi].found = false; ++lastMissing_; }
            continue;
        }
        // Cache decoded page images within this pack (page index -> QImage).
        std::unordered_map<int, QImage> pageImg;

        for (auto& [oi, nm] : wanted) {
            const auto it = loc_.find(nm);
            const int pi = it->second.page;
            const auto& page = pf.pages()[pi];
            const auto& entry = page.entries[it->second.entry];

            // Decode this page's PNG once.
            auto pit = pageImg.find(pi);
            if (pit == pageImg.end()) {
                QImage img;
                const bool ok = img.loadFromData(
                    reinterpret_cast<const uchar*>(page.png.data()),
                    static_cast<int>(page.png.size()), "PNG");
                if (ok) img = img.convertToFormat(QImage::Format_RGBA8888);
                pit = pageImg.emplace(pi, ok ? img : QImage()).first;
            }
            const QImage& img = pit->second;
            if (img.isNull()) { out[oi].found = false; ++lastMissing_; continue; }

            // Clamp the entry rect to the page (defensive against bad data).
            const int ex = std::max(0, entry.x);
            const int ey = std::max(0, entry.y);
            const int ew = std::min(entry.w, img.width()  - ex);
            const int eh = std::min(entry.h, img.height() - ey);
            if (ew <= 0 || eh <= 0) { out[oi].found = false; ++lastMissing_; continue; }

            // Extract the sprite sub-rect as its own QImage.
            QImage sprite = img.copy(ex, ey, ew, eh);

            // Cap sprite size. The GL atlas is a texture ARRAY: every layer is
            // sized to the largest sprite, so one 744x982 jumbo sprite would
            // blow all ~4000 layers up to that size (11+ GB -> GL_INVALID_VALUE,
            // black render). Downscale anything over the cap; standard PZ tiles
            // (<=128x256) are unaffected, big trees lose resolution but render.
            constexpr int kCap = 256;
            if (sprite.width() > kCap || sprite.height() > kCap) {
                sprite = sprite.scaled(std::min(sprite.width(), kCap),
                                       std::min(sprite.height(), kCap),
                                       Qt::KeepAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_RGBA8888);
            }
            const int sw = sprite.width(), sh = sprite.height();

            Layer L;
            L.w = sw; L.h = sh; L.ox = entry.ox; L.oy = entry.oy;
            L.rgba.resize(static_cast<size_t>(sw) * sh * 4);
            for (int row = 0; row < sh; ++row) {
                const uchar* src = sprite.constScanLine(row);
                std::copy(src, src + size_t(sw) * 4,
                          L.rgba.data() + size_t(row) * sw * 4);
            }
            L.found = true;
            out[oi] = std::move(L);
        }
    }

    std::printf("[SpriteAtlas] built %zu layers, %d missing sprite(s)\n",
                names.size(), lastMissing_);
    std::fflush(stdout);
    return out;
}

} // namespace pzmm

#include "tiledefs.hpp"

#include "le.hpp" // readAllBytes

#include <algorithm>
#include <charconv>

namespace pzformat {

namespace {

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return std::string(s.substr(a, b - a));
}

/// Java Integer.parseInt(trim(x)). Throws on garbage, as the Java did.
int parseInt(std::string_view s) {
    const std::string t = trim(s);
    int v = 0;
    const auto* first = t.data();
    const auto* last = t.data() + t.size();
    if (!t.empty() && t[0] == '+') ++first;
    const auto [p, ec] = std::from_chars(first, last, v);
    if (ec != std::errc{} || p != last) {
        throw ParseError("not an integer: '" + t + "'");
    }
    return v;
}

} // namespace

TileDefs TileDefs::readAll(const std::filesystem::path& mediaDir) {
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(mediaDir)) {
        if (!e.is_regular_file()) continue;
        const std::string n = e.path().filename().string();
        if (n.size() > 10 && n.compare(n.size() - 10, 10, ".tiles.txt") == 0) {
            files.push_back(e.path());
        }
    }
    std::sort(files.begin(), files.end());

    TileDefs td;
    for (const auto& f : files) td.parse(f);
    return td;
}

void TileDefs::parse(const std::filesystem::path& file) {
    const auto bytes = readAllBytes(file);
    parseText(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

void TileDefs::parseText(std::string_view text) {
    // The Java parses into ArrayLists whose element addresses never move. Here
    // tiles accumulate inside tilesets_, and the name index is (re)built from
    // scratch at the end of the file so no stored pointer outlives a vector
    // reallocation.
    const std::size_t tilesetBase = tilesets_.size();

    Tileset* ts = nullptr;
    bool haveTile = false;
    Tile tile;
    std::string pendingName;
    bool havePending = false;
    int depth = 0;
    std::string blockKind;

    auto forEachLine = [&](auto&& fn) {
        std::size_t i = 0;
        while (i < text.size()) {
            std::size_t j = text.find('\n', i);
            if (j == std::string_view::npos) j = text.size();
            fn(text.substr(i, j - i));
            i = j + 1;
        }
    };

    forEachLine([&](std::string_view rawLine) {
        const std::string line = trim(rawLine);
        if (line.empty()) return;

        if (line.rfind("//", 0) == 0) {
            pendingName = trim(std::string_view(line).substr(2));
            havePending = true;
            return;
        }
        if (line == "tileset") { blockKind = "tileset"; return; }
        if (line == "tile")    { blockKind = "tile";    return; }
        if (line == "{") {
            ++depth;
            if (blockKind == "tileset") {
                tilesets_.emplace_back();
                ts = &tilesets_.back();
            } else if (blockKind == "tile") {
                tile = Tile{};
                haveTile = true;
            }
            blockKind.clear();
            return;
        }
        if (line == "}") {
            --depth;
            if (haveTile) {
                finishTile(ts, std::move(tile), havePending ? pendingName : std::string{});
                haveTile = false;
                tile = Tile{};
                havePending = false;
                pendingName.clear();
            } else if (depth == 0) {
                ts = nullptr;
            }
            return;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos) return;
        const std::string key = trim(std::string_view(line).substr(0, eq));
        const std::string val = trim(std::string_view(line).substr(eq + 1));

        if (haveTile) {
            if (key == "xy") {
                const auto comma = val.find(',');
                tile.x = parseInt(std::string_view(val).substr(0, comma));
                tile.y = parseInt(std::string_view(val).substr(comma + 1));
            } else {
                tile.props.put(key, val);
            }
        } else if (ts != nullptr) {
            if (key == "file") {
                ts->file = val;
            } else if (key == "id") {
                ts->id = parseInt(val);
            } else if (key == "size") {
                const auto comma = val.find(',');
                ts->width = parseInt(std::string_view(val).substr(0, comma));
                ts->height = parseInt(std::string_view(val).substr(comma + 1));
            }
        }
    });

    (void)tilesetBase;
    rebuildIndex();
}

void TileDefs::finishTile(Tileset* ts, Tile&& tile, const std::string& commentName) {
    if (ts == nullptr) return;
    tile.tileset = ts->file;
    tile.index = tile.y * ts->width + tile.x;
    tile.name = ts->file + "_" + std::to_string(tile.index);
    if (!commentName.empty() && commentName != tile.name) {
        ++nameMismatches_;
        if (mismatchSamples_.size() < 8) {
            mismatchSamples_.push_back("computed " + tile.name + " but comment says " + commentName);
        }
    }
    ts->tiles.push_back(std::move(tile));
    // Index is rebuilt at end of file; do not store a pointer here.
}

void TileDefs::rebuildIndex() {
    byName_.clear();
    ordered_.clear();
    tilesetByFile_.clear();

    std::size_t total = 0;
    for (const auto& ts : tilesets_) total += ts.tiles.size();
    byName_.reserve(total);
    ordered_.reserve(total);

    for (const auto& ts : tilesets_) {
        if (!ts.file.empty()) tilesetByFile_.emplace(ts.file, &ts);
        for (const auto& t : ts.tiles) {
            // LinkedHashMap.put semantics: last write wins on the map, but the
            // ordered list preserves first-seen position with updated pointer.
            auto [it, inserted] = byName_.insert_or_assign(t.name, &t);
            if (inserted) {
                ordered_.emplace_back(t.name, &t);
            } else {
                for (auto& [n, p] : ordered_) if (n == t.name) { p = &t; break; }
            }
        }
    }
}

} // namespace pzformat

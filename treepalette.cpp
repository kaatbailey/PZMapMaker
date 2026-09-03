// treepalette.cpp — port of TreePalette.java. See the header for the four port
// notes; the one that bites is NOTE 1, the sorted iteration.

#include "treepalette.hpp"

#include <algorithm>
#include <charconv>
#include <string_view>

namespace pzformat {

namespace {

/// Java's Integer.parseInt(v.trim()) inside a try/catch that `continue`s on
/// NumberFormatException. Returns false where Java would have thrown.
///
/// Java's trim() strips every char <= U+0020, which is wider than isspace() and
/// includes control characters; reproduced explicitly rather than reached for
/// via std::isspace, whose behaviour is locale-dependent and whose argument
/// must be cast to unsigned char to avoid UB on negative chars.
///
/// parseInt accepts a leading '+' or '-' and nothing else non-numeric.
/// from_chars accepts '-' but NOT '+', so a leading '+' is handled here; and
/// from_chars must consume the WHOLE trimmed string, or Java would have thrown.
bool parseIntStrict(std::string_view v, int& out) {
    std::size_t b = 0, e = v.size();
    while (b < e && static_cast<unsigned char>(v[b]) <= 0x20) b++;
    while (e > b && static_cast<unsigned char>(v[e - 1]) <= 0x20) e--;
    std::string_view t = v.substr(b, e - b);
    if (t.empty()) return false;

    bool plus = false;
    if (t.front() == '+') { plus = true; t.remove_prefix(1); if (t.empty()) return false; }

    int value = 0;
    const char* first = t.data();
    const char* last = t.data() + t.size();
    const std::from_chars_result r = std::from_chars(first, last, value);
    if (r.ec != std::errc{} || r.ptr != last) return false;
    if (plus && value < 0) return false;   // "+-3" — from_chars would accept the '-'
    out = value;
    return true;
}

} // namespace

TreePalette TreePalette::pick(const TileIndex& ti,
                              const std::unordered_set<std::string>& sprites) {
    TreePalette p;

    // SORTED, NOT RAW. See PORT NOTE 1. TileIndex::names() walks an
    // unordered_map, so without this the two trees could not agree even in
    // principle, and the order decides which tile lands on ~7,700 squares.
    std::vector<std::string> names = ti.names();
    std::sort(names.begin(), names.end());

    for (const std::string& n : names) {
        const Tile* t = ti.get(n);
        if (t == nullptr || t->tileset != SHEET) continue;
        if (ti.kindOf(n) != TileIndex::Kind::Vegetation) continue;

        const std::optional<std::string> v = t->props.get("tree");
        if (!v.has_value() || v->empty()) continue;

        // `solid` separates trunks from ground cover on this sheet: the
        // non-solid VEGETATION entries are bushes and grass clumps.
        if (!t->props.contains("solid")) continue;

        int size = 0;
        if (!parseIntStrict(*v, size)) continue;   // Java: catch, continue

        p.bySize_[size].push_back(n);
        p.all.push_back(n);
    }

    // Appended AFTER the sheet tiles, not sorted in with them. See PORT NOTE 4.
    if (sprites.count(STUMP) != 0) {
        p.hasStump = true;
        p.all.push_back(STUMP);
    }
    return p;
}

const std::vector<std::string>* TreePalette::tilesNear(int size) const {
    const auto exact = bySize_.find(size);
    if (exact != bySize_.end() && !exact->second.empty()) return &exact->second;

    // Nearest available, low side first at each distance. The asymmetry is
    // load-bearing: at d=1 a size-1 list wins over a size-2 list.
    for (int d = 1; d <= 8; d++) {
        const auto lo = bySize_.find(size - d);
        if (lo != bySize_.end() && !lo->second.empty()) return &lo->second;
        const auto hi = bySize_.find(size + d);
        if (hi != bySize_.end() && !hi->second.empty()) return &hi->second;
    }
    return nullptr;
}

std::string TreePalette::toString() const {
    std::string sb;
    for (const auto& [size, tiles] : bySize_) {   // std::map: ascending, like TreeMap
        if (!sb.empty()) sb += ", ";
        sb += "size ";
        sb += std::to_string(size);
        sb += ": ";
        sb += std::to_string(tiles.size());
        sb += " tiles";
    }
    return sb + (hasStump ? ", stumps available" : ", NO stump tile")
           + "  (no sprites by design; the engine substitutes species art)";
}

} // namespace pzformat

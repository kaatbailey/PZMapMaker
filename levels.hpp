#pragma once

#include <cstdint>

namespace pzformat {

/// The whole of LotPack's dependency on the lotheader.
/// minLevel is LotHeader::minLevel; maxLevel is LotHeader::unknown12, the
/// trailer field at +12 whose meaning is inferred from the level count and not
/// otherwise identified.
struct LevelRange {
    std::int32_t minLevel = 0;
    std::int32_t maxLevel = 0;

    std::int32_t levelCount() const noexcept { return maxLevel - minLevel + 1; }
};

} // namespace pzformat

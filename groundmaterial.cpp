// groundmaterial.cpp — the constant table, transcribed from
// GroundMaterial.java's enum constant list.
//
// DECLARATION ORDER IS THE CONTRACT. Java's values() returns declaration order
// and the oracle digests it, so this array must stay in the order the Java file
// lists. Note ROAD_04 before ROAD_03: declaration order is deliberately not
// rank order, and sorting this table would be a silent behaviour change.
//
// The numbers here are transcription, not derivation. Every one of them is
// measured data from STATE §26/§27 or from the sheets themselves. If a value
// looks wrong, check it against blends_natural_01 / blends_street_01 and the
// Java file — do not compute a "corrected" one.

#include "groundmaterial.hpp"

namespace pzformat {

namespace {

// Shorthand so the table below lines up with the Java source it mirrors.
constexpr std::array<int, 4> S4(int a, int b, int c, int d) { return {a, b, c, d}; }
constexpr std::array<int, 4> S2(int a, int b) { return {a, b, 0, 0}; }

} // namespace

const std::array<GroundMaterial, 14>& GroundMaterial::values() {
    //                     ord  name             FloorMaterial   sheet    block  solid variants            n  sets rank
    static const std::array<GroundMaterial, 14> kValues{{
        {  0, "GRASS_DARK",   "Grass_Dark",   NATURAL, 16, S4(16, 21, 22, 23),    4, 2,  0 },
        {  1, "GRASS_MEDIUM", "Grass_Medium", NATURAL, 32, S4(32, 37, 38, 39),    4, 2,  1 },
        {  2, "GRASS_LIGHT",  "Grass_Light",  NATURAL, 48, S4(48, 53, 54, 55),    4, 2,  2 },
        {  3, "SAND",         "Sand",         NATURAL,  0, S4( 0,  5,  6,  7),    4, 2,  3 },
        {  4, "DIRT_GRASS",   "Dirt_Grass",   NATURAL, 80, S4(80, 85, 86, 87),    4, 2,  4 },
        {  5, "DIRT",         "Dirt",         NATURAL, 64, S4(64, 69, 70, 71),    4, 2,  5 },
        {  6, "CLAY",         "Clay",         NATURAL, 96, S4(96, 101, 102, 103), 4, 2,  6 },

        // Roads. Road_01 and Road_02 have only TWO solid variants — B+6 and
        // B+7 are spriteless in blends_street_01. One mask variant set, not two.
        {  7, "ROAD_01",      "Road_01",      STREET,   0, S2( 0,  5),            2, 1, 10 },
        {  8, "ROAD_02",      "Road_02",      STREET,  16, S2(16, 21),            2, 1, 11 },
        {  9, "ROAD_04",      "Road_04",      STREET,  48, S4(48, 53, 54, 55),    4, 1, 12 },
        { 10, "ROAD_03",      "Road_03",      STREET,  32, S4(32, 37, 38, 39),    4, 1, 13 },
        { 11, "ROAD_05",      "Road_05",      STREET,  64, S4(64, 69, 70, 71),    4, 1, 14 },
        { 12, "ROAD_07",      "Road_07",      STREET,  96, S4(96, 101, 102, 103), 4, 1, 15 },
        { 13, "ROAD_06",      "Road_06",      STREET,  80, S4(80, 85, 86, 87),    4, 1, 16 },
    }};
    return kValues;
}

const GroundMaterial* GroundMaterial::byFloorMaterial(std::string_view s) {
    // Linear scan in declaration order, as Java does. First match wins; the
    // floorMaterial values are distinct, so order does not affect the result —
    // but the scan order is kept anyway, because "does not affect the result"
    // is a claim about today's table, not about the type.
    for (const GroundMaterial& m : values()) {
        if (m.floorMaterial == s) return &m;
    }
    return nullptr;
}

} // namespace pzformat

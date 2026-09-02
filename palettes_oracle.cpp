// palettes_oracle.cpp — C++ side of the palette cross-language oracle.
// Must emit a digest byte-identical to PalettesOracle.java's.
//
// See PalettesOracle.java for why this exists: §40 measured ten deliberate
// mutations of BuildingPlan surviving its own self-test unchanged, so the
// self-test is not the oracle. This digest emits every field of every constant
// and every draw of every seed.
//
// CORPUS RULE (STATE §39): arithmetic and JavaRandom only, no transcendentals,
// so both trees build identical inputs before the unit under test runs.
//
// GroundMaterial only at time of writing. TreePalette is DEFERRED on the
// A2-gate (STATE §25) — deliberate, see FINDINGS_F4.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <cstring>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "groundmaterial.hpp"
#include "maskrule.hpp"
#include "groundpalette.hpp"
#include "tilepalette.hpp"
#include "spritenames.hpp"
#include <algorithm>
#include <filesystem>
#include "java_random.hpp"

using pzformat::GroundMaterial;
using pzformat::MaskRule;
using pzformat::GroundPalette;
using pzformat::TilePalette;
using pzformat::JavaRandom;

namespace {

std::string esc(std::string_view s) {
    std::string b;
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == '\\') b += "\\\\";
        else if (c == '\t') b += "\\t";
        else if (c == '\n') b += "\\n";
        else if (c == '\r') b += "\\r";
        else if (c < 0x20) {
            char tmp[8];
            std::snprintf(tmp, sizeof(tmp), "\\x%02x", c);
            b += tmp;
        } else {
            b += ch;
        }
    }
    return b;
}

/// Raw IEEE-754 bits, so a double is compared exactly rather than printed.
std::string bits(double d) {
    std::uint64_t u = 0;
    static_assert(sizeof(u) == sizeof(d), "double is not 64-bit");
    std::memcpy(&u, &d, sizeof(u));
    char tmp[32];
    std::snprintf(tmp, sizeof(tmp), "%016llx", static_cast<unsigned long long>(u));
    return tmp;
}

std::string ints(const std::vector<int>& v) {
    std::string b;
    for (std::size_t i = 0; i < v.size(); i++) {
        if (i > 0) b += ',';
        b += std::to_string(v[i]);
    }
    return b;
}

void groundMaterial(std::vector<std::string>& out, int seeds) {
    const auto& values = GroundMaterial::values();

    for (const GroundMaterial& m : values) {
        out.push_back("GM\t" + std::to_string(m.ordinal) + "\t" + std::string(m.name)
                      + "\t" + esc(m.floorMaterial)
                      + "\t" + esc(m.sheet)
                      + "\t" + std::to_string(m.block)
                      + "\t" + ints(m.solidIndices())
                      + "\t" + std::to_string(m.variantSets)
                      + "\t" + std::to_string(m.rank));
    }

    // Same insertion sort as the Java side — not std::sort, so both trees run
    // the same algorithm rather than two library sorts that happen to agree.
    std::vector<const GroundMaterial*> byRank;
    byRank.reserve(values.size());
    for (const GroundMaterial& m : values) byRank.push_back(&m);
    for (std::size_t i = 1; i < byRank.size(); i++) {
        const GroundMaterial* key = byRank[i];
        std::ptrdiff_t j = static_cast<std::ptrdiff_t>(i) - 1;
        while (j >= 0 && byRank[static_cast<std::size_t>(j)]->rank > key->rank) {
            byRank[static_cast<std::size_t>(j) + 1] = byRank[static_cast<std::size_t>(j)];
            j--;
        }
        byRank[static_cast<std::size_t>(j + 1)] = key;
    }
    for (const GroundMaterial* m : byRank) {
        out.push_back("GMRANK\t" + std::to_string(m->rank) + "\t" + std::string(m->name)
                      + "\t" + std::to_string(m->ordinal));
    }

    for (const GroundMaterial& a : values) {
        for (const GroundMaterial& b : values) {
            out.push_back("GMOUT\t" + std::string(a.name) + "\t" + std::string(b.name)
                          + "\t" + (a.outranks(&b) ? "1" : "0"));
        }
        out.push_back("GMOUT\t" + std::string(a.name) + "\tNULL\t"
                      + (a.outranks(nullptr) ? "1" : "0"));
    }

    std::vector<std::string> probes;
    for (const GroundMaterial& m : values) probes.emplace_back(m.floorMaterial);
    probes.push_back("");
    probes.push_back(" ");
    probes.push_back("grass_dark");
    probes.push_back("GRASS_DARK");
    probes.push_back("Grass_Dark ");
    probes.push_back(" Grass_Dark");
    probes.push_back("Grass_Darkk");
    probes.push_back("Grass_Dar");
    probes.push_back("Road_08");
    probes.push_back("Road_00");
    probes.push_back("blends_natural_01_");
    probes.push_back("Water");
    for (const std::string& p : probes) {
        const GroundMaterial* m = GroundMaterial::byFloorMaterial(p);
        out.push_back("GMBFM\t" + esc(p) + "\t" + (m == nullptr ? "NULL" : std::string(m->name)));
    }

    for (int s = 0; s < seeds; s++) {
        for (const GroundMaterial& m : values) {
            JavaRandom rng(s);
            out.push_back("GMSOLID\t" + std::to_string(s) + "\t" + std::string(m.name)
                          + "\t" + esc(m.solid(rng)));
        }
    }

    JavaRandom shared(20260901LL);
    for (int i = 0; i < seeds * 4; i++) {
        const GroundMaterial& m = values[static_cast<std::size_t>(i) % values.size()];
        out.push_back("GMSEQ\t" + std::to_string(i) + "\t" + std::string(m.name)
                      + "\t" + esc(m.solid(shared)));
    }

    for (const GroundMaterial& m : values) {
        std::vector<int> a = m.solidIndices();
        for (int& x : a) x = -999;
        out.push_back("GMCLONE\t" + std::string(m.name) + "\t" + ints(m.solidIndices()));
    }
}

// ------------------------------------------------------------------- MR ---
// See PalettesOracle.java for what each sub-section catches. The one worth
// repeating here: side() draws from the rng ONLY when variantSets > 1, so the
// draw count is branch-dependent, and MRSEQ is the only section that can see a
// port which draws unconditionally.

constexpr int kMrBlocks[] = {0, 16, 32, 48, 64, 80, 96, 240, -16};
constexpr std::size_t kMrBlockCount = sizeof(kMrBlocks) / sizeof(kMrBlocks[0]);

MaskRule::DirSet setOf(int bits) {
    return MaskRule::DirSet::fromBits(static_cast<std::uint8_t>(bits));
}

void maskRule(std::vector<std::string>& out, int seeds) {
    for (const MaskRule::Dir d : MaskRule::kDirs) {
        out.push_back(std::string("MRDIR\t") + MaskRule::name(d)
                      + "\t" + std::to_string(MaskRule::ord(d))
                      + "\t" + std::to_string(MaskRule::dx(d))
                      + "\t" + std::to_string(MaskRule::dy(d))
                      + "\t" + MaskRule::name(MaskRule::opposite(d)));
    }

    for (const int block : kMrBlocks) {
        for (const MaskRule::Dir a : MaskRule::kDirs) {
            for (const MaskRule::Dir b : MaskRule::kDirs) {
                if (a == b || MaskRule::opposite(a) == b) continue;  // not adjacent
                out.push_back("MRCORNER\t" + std::to_string(block)
                              + "\t" + MaskRule::name(a) + "\t" + MaskRule::name(b)
                              + "\t" + std::to_string(MaskRule::corner(block, a, b)));
            }
        }
    }

    const int reps = seeds / 10;
    for (int s = 0; s < reps; s++) {
        for (int bits = 0; bits < 16; bits++) {
            for (const int block : kMrBlocks) {
                for (int vs = 1; vs <= 2; vs++) {
                    JavaRandom rng(s);
                    const std::vector<int> m = MaskRule::masks(block, setOf(bits), vs, rng);
                    out.push_back("MRSET\t" + std::to_string(s) + "\t" + std::to_string(bits)
                                  + "\t" + std::to_string(block) + "\t" + std::to_string(vs)
                                  + "\t" + ints(m));
                }
            }
        }
    }

    JavaRandom shared(20260901LL);
    for (int i = 0; i < seeds * 4; i++) {
        const int bits = i % 16;
        const int block = kMrBlocks[(static_cast<std::size_t>(i) / 16) % kMrBlockCount];
        const int vs = 1 + (i % 2);
        const std::vector<int> m = MaskRule::masks(block, setOf(bits), vs, shared);
        out.push_back("MRSEQ\t" + std::to_string(i) + "\t" + std::to_string(bits)
                      + "\t" + std::to_string(block) + "\t" + std::to_string(vs)
                      + "\t" + ints(m));
    }
}

// ------------------------------------------------------------------- GP ---
// See PalettesOracle.java for why the corpus is synthetic and which branches it
// exists to reach. The candidate list order below MUST match gpCandidates()
// exactly — it drives the per-candidate rng draws, so a different order gives
// both trees different inputs before the unit under test runs (§39).

std::vector<std::string> gpCandidates() {
    std::vector<std::string> c;
    for (const auto& g : GroundPalette::groups())
        for (const int idx : g.indices)
            c.push_back(std::string(GroundPalette::BASE_SHEET) + std::to_string(idx));
    for (const int idx : GroundPalette::DIRT)
        c.push_back(std::string(GroundPalette::BASE_SHEET) + std::to_string(idx));
    for (const int idx : GroundPalette::DIRT_GRASS)
        c.push_back(std::string(GroundPalette::BASE_SHEET) + std::to_string(idx));
    for (int row = 0; row < 10; row++)
        for (int col = 0; col < 8; col++)
            c.push_back(std::string(GroundPalette::TUFT_SHEET)
                        + std::to_string(row * 8 + col));
    return c;
}

/// Inclusion percentage per candidate, by corpus mode. See PalettesOracle.java
/// for what each mode is for; the tables must match exactly.
std::int32_t gpThreshold(int mode, bool isTuft) {
    switch (mode) {
        case 0: return 100;
        case 1: return isTuft ? 0 : 100;
        case 2: return isTuft ? 100 : 95;
        case 3: return isTuft ? 50 : 75;
        case 4: return isTuft ? 100 : 0;
        case 5: return isTuft ? 10 : 90;
        case 6: return isTuft ? 20 : 100;
        default: return 50;
    }
}

pzformat::Tile mkTile(const std::string& name) {
    pzformat::Tile t;
    t.name = name;
    return t;
}

std::string groundStr(const GroundPalette::Ground& g) {
    return esc(g.base) + "\t" + (g.tuft.has_value() ? esc(*g.tuft) : "NULL");
}

void groundPalette(std::vector<std::string>& out, int seeds) {
    const std::vector<std::string> cand = gpCandidates();

    for (int c = 0; c < seeds; c++) {
        // MODE, not uniform randomness — see PalettesOracle.java for why the
        // first version of this corpus was byte-identical and still reached
        // almost none of the branches.
        const int mode = c % 8;
        JavaRandom rng(c);
        pzformat::TileIndex ti;
        std::unordered_set<std::string> sprites;
        for (const std::string& n : cand) {
            const std::int32_t a = rng.nextInt(100);
            const std::int32_t b = rng.nextInt(100);
            const bool isTuft = n.rfind(GroundPalette::TUFT_SHEET, 0) == 0;
            const std::int32_t p = gpThreshold(mode, isTuft);
            if (a < p) ti.add(mkTile(n));
            if (b < p) sprites.insert(n);
        }

        try {
            const GroundPalette gp = GroundPalette::pick(ti, sprites);

            std::string allJoin;
            for (std::size_t i = 0; i < gp.all.size(); i++) {
                if (i > 0) allJoin += ',';
                allJoin += esc(gp.all[i]);
            }
            out.push_back("GPALL\t" + std::to_string(c) + "\t"
                          + std::to_string(gp.all.size()) + "\t" + allJoin);
            out.push_back("GPTOSTR\t" + std::to_string(c) + "\t" + esc(gp.toString()));

            JavaRandom rr(static_cast<std::int64_t>(c) * 7919LL + 1);
            for (int i = 0; i < 20; i++) {
                out.push_back("GPROLL\t" + std::to_string(c) + "\t" + std::to_string(i)
                              + "\t" + groundStr(gp.roll(rr)));
            }
        } catch (const std::runtime_error& e) {
            // The MESSAGE, not just the fact — see PalettesOracle.java.
            out.push_back("GPTHROW\t" + std::to_string(c) + "\t" + esc(e.what()));
            continue;
        }
    }

    pzformat::TileIndex full;
    std::unordered_set<std::string> fullSprites;
    for (const std::string& n : cand) { full.add(mkTile(n)); fullSprites.insert(n); }
    const GroundPalette gpFull = GroundPalette::pick(full, fullSprites);
    out.push_back("GPFULL\t" + std::to_string(gpFull.all.size()) + "\t"
                  + esc(gpFull.toString()));
    JavaRandom shared(20260901LL);
    for (int i = 0; i < seeds * 4; i++) {
        out.push_back("GPSEQ\t" + std::to_string(i) + "\t" + groundStr(gpFull.roll(shared)));
    }

    for (int row = 0; row < 10; row++) {
        for (int col = 0; col < 8; col++) {
            const std::string n = std::string(GroundPalette::TUFT_SHEET)
                                + std::to_string(row * 8 + col);
            out.push_back("GPROWW\t" + esc(n) + "\t"
                          + bits(GroundPalette::rowWeightOf(n)));
        }
    }
}

// ------------------------------------------------------------------- TP ---
// See PalettesOracle.java for the mode table and what each mode reaches.
// The name pool, flag vocabulary and draw sequence must match it exactly.

// PLANTED corpus — see PalettesOracle.java for why random property soup
// reached none of TilePalette's conjunctive predicates, and what each mode
// ablates. The plant table, mode table and draw sequence must match exactly.

struct TpPlant { const char* name; const char* flags; const char* material; };

const TpPlant kTpPlant[] = {
    {"blends_natural_01_0",               "grassFloor solidfloor", ""},
    {"blends_natural_02_0",               "water solidfloor",      ""},
    {"blends_street_01_0",                "solidfloor",            ""},
    {"floors_interior_tilesandwood_01_0", "solidfloor",            "Wood"},
    {"walls_exterior_house_01_0",         "WallN",                 ""},
    {"walls_exterior_house_01_1",         "WallW",                 ""},
    {"walls_exterior_house_01_2",         "WallNW",                ""},
    {"walls_exterior_house_01_3",         "WallSE",                ""},
    {"walls_exterior_house_01_4",         "DoorWallN",             ""},
    {"walls_exterior_house_01_5",         "DoorWallW",             ""},
    {"walls_exterior_house_02_0",         "WallN",                 ""},
    {"walls_exterior_house_02_1",         "WallW",                 ""},
    {"walls_exterior_house_02_2",         "WallNW",                ""},
    {"walls_exterior_house_02_3",         "WallSE",                ""},
    {"walls_exterior_house_02_4",         "DoorWallN",             ""},
    {"walls_exterior_house_02_5",         "DoorWallW",             ""},
    {"walls_exterior_wooden_01_0",        "WallN",                 ""},
    {"walls_exterior_wooden_01_1",        "WallW",                 ""},
    {"walls_exterior_wooden_01_2",        "WallNW",                ""},
    {"walls_exterior_wooden_01_3",        "WallSE",                ""},
    {"walls_exterior_wooden_01_4",        "DoorWallN",             ""},
    {"walls_exterior_wooden_01_5",        "DoorWallW",             ""},
    {"walls_exterior_wooden_02_0",        "WallN",                 ""},
    {"walls_exterior_wooden_02_1",        "WallW",                 ""},
    {"walls_exterior_wooden_02_2",        "WallNW",                ""},
    {"walls_exterior_wooden_02_3",        "WallSE",                ""},
    {"walls_exterior_wooden_02_4",        "DoorWallN",             ""},
    {"walls_exterior_wooden_02_5",        "DoorWallW",             ""},
    {"walls_exterior_house_low_01_0",     "WallN",                 ""},
    {"walls_exterior_house_low_01_1",     "WallW",                 ""},
    {"walls_exterior_house_low_01_2",     "WallNW",                ""},
    {"walls_exterior_house_low_01_3",     "WallSE",                ""},
    {"walls_exterior_house_low_01_4",     "DoorWallN",             ""},
    {"walls_exterior_house_low_01_5",     "DoorWallW",             ""},
    {"walls_interior_house_01_0",         "WallN",                 ""},
    {"walls_interior_house_01_1",         "WallW",                 ""},
    {"walls_interior_house_01_2",         "WallNW",                ""},
    {"walls_interior_house_01_3",         "WallSE",                ""},
    {"walls_interior_house_01_4",         "DoorWallN",             ""},
    {"walls_interior_house_01_5",         "DoorWallW",             ""},
};
constexpr std::size_t kTpPlantCount = sizeof(kTpPlant) / sizeof(kTpPlant[0]);

bool tpStarts(const std::string& s, const char* p) {
    const std::string pre(p);
    return s.size() >= pre.size() && s.compare(0, pre.size(), pre) == 0;
}

bool tpIsFirstChoice(const std::string& n) {
    return tpStarts(n, "blends_natural_01_")
        || tpStarts(n, "blends_natural_02_")
        || tpStarts(n, "blends_street_01_")
        || tpStarts(n, "floors_interior_tilesandwood_01_")
        || tpStarts(n, "walls_exterior_house_01_")
        || tpStarts(n, "walls_interior_house_01_");
}

std::vector<std::string> tpSplit(const std::string& s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i <= s.size()) {
        const std::size_t j = s.find(' ', i);
        if (j == std::string::npos) { out.push_back(s.substr(i)); break; }
        out.push_back(s.substr(i, j - i));
        i = j + 1;
    }
    return out;
}

std::string optStr(const std::optional<std::string>& o) {
    return o.has_value() ? esc(*o) : "NULL";
}

void tilePalette(std::vector<std::string>& out, int seeds) {
    for (int c = 0; c < seeds; c++) {
        const int mode = c % 8;
        JavaRandom rng(c);
        pzformat::TileIndex ti;
        std::unordered_set<std::string> sprites;

        for (std::size_t si = 0; si < kTpPlantCount; si++) {
            const TpPlant& spec = kTpPlant[si];
            const std::int32_t keep = rng.nextInt(100);

            std::string name = spec.name;
            if (mode == 5) name = "zz_misc_" + name;

            bool inTi = true, inSpr = true;
            switch (mode) {
                case 1: inSpr = !tpIsFirstChoice(name); break;
                case 2: inSpr = false; break;
                case 3: inTi = false; break;
                case 4: inTi = !tpIsFirstChoice(name); break;
                case 7: inTi = keep < 50; break;
                default: break;
            }
            if (!inTi) continue;

            pzformat::Tile t;
            t.name = name;
            for (const std::string& f : tpSplit(spec.flags)) t.props.put(f, "");
            if (std::string(spec.material) != "") t.props.put("Material", spec.material);
            if (mode == 6) {
                t.props.put("CustomName", "Planted " + std::to_string(c % 7));
                if (std::string(spec.material) == "") t.props.put("Material", "Brick");
            }
            ti.add(t);
            if (inSpr) sprites.insert(name);
        }

        for (int i = 0; i < 12; i++) {
            const std::int32_t k = rng.nextInt(100);
            const std::string n = std::string(i % 3 == 0 ? "overlay_wall_" : "floors_misc_")
                                + std::to_string(i);
            pzformat::Tile t;
            t.name = n;
            t.props.put(k < 50 ? "WallOverlay" : "FloorOverlay", "");
            ti.add(t);
            if (k < 70) sprites.insert(n);
        }

        TilePalette p = TilePalette::pick(ti, sprites);
        out.push_back("TPPICK\t" + std::to_string(c) + "\t" + std::to_string(mode)
                      + "\t" + (p.complete() ? "true" : "false")
                      + "\t" + std::to_string(p.droppedNoSprite)
                      + "\t" + std::to_string(p.all.size()));
        out.push_back("TPTOSTR\t" + std::to_string(c) + "\t" + esc(p.toString()));

        std::string allJoin;
        for (std::size_t i = 0; i < p.all.size(); i++) {
            if (i > 0) allJoin += ',';
            allJoin += esc(p.all[i]);
        }
        out.push_back("TPALL\t" + std::to_string(c) + "\t" + allJoin);

        try {
            p.verify();
            out.push_back("TPVERIFY\t" + std::to_string(c) + "\tOK");
        } catch (const std::runtime_error& e) {
            out.push_back("TPVERIFY\t" + std::to_string(c) + "\t" + esc(e.what()));
        }

        for (int k = 0; k < 8; k++) {
            const bool nn = (k & 1) != 0, ww = (k & 2) != 0, in = (k & 4) != 0;
            out.push_back("TPJOIN\t" + std::to_string(c) + "\t" + std::to_string(k)
                          + "\t" + optStr(p.wallJoin(nn, ww, in)));
        }

        const std::vector<TilePalette::WallSkin> skins =
            TilePalette::discoverSkins(ti, sprites);
        out.push_back("TPSKINN\t" + std::to_string(c) + "\t" + std::to_string(skins.size()));
        for (std::size_t i = 0; i < skins.size(); i++) {
            const auto& sk = skins[i];
            out.push_back("TPSKIN\t" + std::to_string(c) + "\t" + std::to_string(i)
                          + "\t" + esc(sk.label())
                          + "\t" + esc(sk.wallN) + "\t" + esc(sk.wallW)
                          + "\t" + esc(sk.wallNW) + "\t" + esc(sk.wallSE)
                          + "\t" + esc(sk.doorN) + "\t" + esc(sk.doorW));
        }
    }
}

// --------------------------------------------------------------- VANILLA ---
// See PalettesOracle.java. The two VIN lines fingerprint the INPUTS first: if
// tile/sprite counts or name-set hashes diverge, the fault is in TileIndex::load
// or PackFile, not in the palette units.

/// FNV-1a 64 over sorted names joined by '\n'. Must match the Java exactly.
std::string fnv1a(const std::vector<std::string>& sortedNames) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (const std::string& n : sortedNames) {
        for (const char c : n) {
            h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
            h *= 0x100000001b3ULL;
        }
        h ^= static_cast<std::uint64_t>('\n');
        h *= 0x100000001b3ULL;
    }
    char tmp[32];
    std::snprintf(tmp, sizeof(tmp), "%016llx", static_cast<unsigned long long>(h));
    return tmp;
}

void vanilla(std::vector<std::string>& out, const std::string& mediaDir) {
    const std::filesystem::path media(mediaDir);
    const pzformat::TileIndex ti = pzformat::TileIndex::load(media);
    const pzformat::SpriteNamesResult sr =
        pzformat::loadSpriteNames(media / "texturepacks");
    const std::unordered_set<std::string>& sprites = sr.names;

    std::vector<std::string> tn = ti.names();
    std::sort(tn.begin(), tn.end());
    std::vector<std::string> sn(sprites.begin(), sprites.end());
    std::sort(sn.begin(), sn.end());

    out.push_back("VIN\ttiles\t" + std::to_string(tn.size()) + "\t" + fnv1a(tn));
    out.push_back("VIN\tsprites\t" + std::to_string(sn.size()) + "\t" + fnv1a(sn));

    const GroundPalette gp = GroundPalette::pick(ti, sprites);
    std::string gpAll;
    for (std::size_t i = 0; i < gp.all.size(); i++) {
        if (i > 0) gpAll += ',';
        gpAll += esc(gp.all[i]);
    }
    out.push_back("VGP\t" + std::to_string(gp.all.size()) + "\t" + gpAll);
    out.push_back("VGPSTR\t" + esc(gp.toString()));
    JavaRandom grng(20260901LL);
    for (int i = 0; i < 20000; i++) {
        out.push_back("VGPROLL\t" + std::to_string(i) + "\t" + groundStr(gp.roll(grng)));
    }

    TilePalette tp = TilePalette::pick(ti, sprites);
    out.push_back("VTP\t" + std::string(tp.complete() ? "true" : "false")
                  + "\t" + std::to_string(tp.droppedNoSprite)
                  + "\t" + std::to_string(tp.all.size()));
    out.push_back("VTPSTR\t" + esc(tp.toString()));
    std::string tpAll;
    for (std::size_t i = 0; i < tp.all.size(); i++) {
        if (i > 0) tpAll += ',';
        tpAll += esc(tp.all[i]);
    }
    out.push_back("VTPALL\t" + tpAll);
    try {
        tp.verify();
        out.push_back("VTPVERIFY\tOK");
    } catch (const std::runtime_error& e) {
        out.push_back("VTPVERIFY\t" + esc(e.what()));
    }
    for (int k = 0; k < 8; k++) {
        out.push_back("VTPJOIN\t" + std::to_string(k) + "\t"
                      + optStr(tp.wallJoin((k & 1) != 0, (k & 2) != 0, (k & 4) != 0)));
    }
    const std::vector<TilePalette::WallSkin> skins =
        TilePalette::discoverSkins(ti, sprites);
    out.push_back("VTPSKINN\t" + std::to_string(skins.size()));
    for (std::size_t i = 0; i < skins.size(); i++) {
        const auto& sk = skins[i];
        out.push_back("VTPSKIN\t" + std::to_string(i) + "\t" + esc(sk.label())
                      + "\t" + esc(sk.wallN) + "\t" + esc(sk.wallW)
                      + "\t" + esc(sk.wallNW) + "\t" + esc(sk.wallSE)
                      + "\t" + esc(sk.doorN) + "\t" + esc(sk.doorW));
    }

    for (const GroundMaterial& m : GroundMaterial::values()) {
        std::string present;
        for (const int idx : m.solidIndices()) {
            const std::string n = std::string(m.sheet) + std::to_string(idx);
            present += (ti.get(n) != nullptr) ? 'T' : '-';
            present += (sprites.find(n) != sprites.end()) ? 'S' : '-';
            present += ' ';
        }
        while (!present.empty() && present.back() == ' ') present.pop_back();
        out.push_back("VGM\t" + std::string(m.name) + "\t" + esc(present));
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: pz_palettes_oracle <out-path> [seeds]\n");
        return 2;
    }
    const int seeds = argc > 2 ? std::atoi(argv[2]) : 5000;

    std::vector<std::string> out;

    if (argc > 3) {
        // Vanilla leg ONLY — see PalettesOracle.java.
        try {
            vanilla(out, argv[3]);
        } catch (const std::exception& e) {
            // A clean message beats terminate(): the usual cause is a wrong
            // media path, and both trees must REFUSE rather than emit an empty
            // digest that would compare equal for the wrong reason.
            std::fprintf(stderr, "vanilla leg failed: %s\n", e.what());
            return 3;
        }
        std::string vb;
        for (const std::string& line : out) { vb += line; vb += '\n'; }
        std::ofstream vf(argv[1], std::ios::binary);
        vf.write(vb.data(), static_cast<std::streamsize>(vb.size()));
        vf.close();
        std::printf("PalettesOracle c++  VANILLA: %zu lines, %zu bytes -> %s\n",
                    out.size(), vb.size(), argv[1]);
        return 0;
    }

    groundMaterial(out, seeds);
    maskRule(out, seeds);
    groundPalette(out, seeds);
    tilePalette(out, seeds);

    std::string blob;
    for (const std::string& line : out) { blob += line; blob += '\n'; }

    std::ofstream f(argv[1], std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 2;
    }
    f.write(blob.data(), static_cast<std::streamsize>(blob.size()));
    f.close();

    std::printf("PalettesOracle c++ : %zu lines, %zu bytes -> %s\n",
                out.size(), blob.size(), argv[1]);
    return 0;
}

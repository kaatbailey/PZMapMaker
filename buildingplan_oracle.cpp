// buildingplan_oracle.cpp — C++ side of the BuildingPlan cross-language
// oracle. Must emit a byte-identical digest to BuildingPlanOracle.java.
//
// See BuildingPlanOracle.java for why this exists alongside the self-test:
// the self-test prints room COUNTS only and survived six deliberate C++
// mutations unchanged. This digest emits every room of every layout.
//
// CORPUS RULE (STATE §39): arithmetic and JavaRandom only, no transcendentals,
// so both trees build identical inputs before the unit under test runs.

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "buildingplan.hpp"
#include "java_random.hpp"

using pzformat::BuildingPlan;
using pzformat::JavaRandom;

namespace {

std::string esc(const std::string& s) {
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

std::string bits(double d) {
    std::uint64_t u = 0;
    static_assert(sizeof(u) == sizeof(d), "double is not 64-bit");
    std::memcpy(&u, &d, sizeof(u));
    char tmp[32];
    std::snprintf(tmp, sizeof(tmp), "%016llx", static_cast<unsigned long long>(u));
    return tmp;
}

std::string join(const std::vector<std::string>& v) {
    std::string b;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i > 0) b += ',';
        b += esc(v[i]);
    }
    return b;
}

const std::vector<std::string>& occ() {
    static const std::vector<std::string> kOcc = {
        "Residential", "Commercial", "Agriculture", "Industrial",
        "Assembly", "Education", "Unclassified"};
    return kOcc;
}

void emit(std::vector<std::string>& out, const char* tag, int idx, const char* facing,
          int x, int y, int w, int h, std::int64_t seed,
          const std::vector<std::string>& types,
          const std::vector<BuildingPlan::Room>& rooms) {

    out.push_back(std::string(tag) + "L\t" + std::to_string(idx) + "\t" + facing + "\t" +
                  std::to_string(x) + "\t" + std::to_string(y) + "\t" + std::to_string(w) +
                  "\t" + std::to_string(h) + "\t" + std::to_string(seed) + "\t" +
                  std::to_string(rooms.size()) + "\t" + join(types));

    for (std::size_t i = 0; i < rooms.size(); ++i) {
        const BuildingPlan::Room& r = rooms[i];
        out.push_back(std::string(tag) + "R\t" + std::to_string(idx) + "\t" +
                      std::to_string(i) + "\t" + esc(r.type) + "\t" + std::to_string(r.x) +
                      "\t" + std::to_string(r.y) + "\t" + std::to_string(r.w) + "\t" +
                      std::to_string(r.h) + "\t" + (r.entrance ? "1" : "0") + "\t" +
                      std::to_string(r.area()) + "\t" + (r.canTakeDoor() ? "1" : "0"));
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: pz_buildingplan_oracle <out.txt>\n");
        return 2;
    }

    std::vector<std::string> out;

    // -----------------------------------------------------------------
    // Part 1 — small pure helpers.
    // -----------------------------------------------------------------

    for (int n = 0; n <= 20; ++n) {
        out.push_back("HC\t" + std::to_string(n) + "\t" +
                      bits(BuildingPlan::hallChance(n)));
    }

    // ORDER MATTERS — see BuildingPlanOracle.java. openBetween short-circuits
    // and draws only for the livingroom/kitchen core; core pairs are
    // interleaved AFTER non-core ones so that a port drawing unconditionally
    // shifts a value the digest actually prints.
    const std::vector<std::string> pairs = {
        "livingroom", "livingroom", "livingroom", "kitchen",
        "bedroom", "kitchen", "kitchen", "livingroom",
        "kitchen", "bathroom", "livingroom", "kitchen",
        "hall", "livingroom", "kitchen", "livingroom"};
    JavaRandom ob(777);
    for (std::size_t i = 0; i < pairs.size(); i += 2) {
        for (int k = 0; k < 40; ++k) {
            out.push_back("OB\t" + std::to_string(i) + "\t" + std::to_string(k) + "\t" +
                          esc(pairs[i]) + "\t" + esc(pairs[i + 1]) + "\t" +
                          (BuildingPlan::openBetween(pairs[i], pairs[i + 1], ob) ? "1" : "0"));
        }
    }

    JavaRandom pk(31337);
    for (int k = 0; k < 500; ++k) {
        out.push_back("PK\t" + std::to_string(k) + "\t" +
                      esc(BuildingPlan::pick(pk, 0.52, "a", "b")));
    }

    for (int w = 0; w <= 24; ++w) {
        for (int h = 0; h <= 24; ++h) {
            out.push_back("AS\t" + std::to_string(w) + "\t" + std::to_string(h) + "\t" +
                          bits(BuildingPlan::aspect(w, h)) + "\t" +
                          std::to_string(BuildingPlan::minRooms(w, h)) + "\t" +
                          std::to_string(BuildingPlan::ceilDiv(w, h)));
        }
    }
    for (int v = -20; v <= 20; ++v) {
        for (int lo = -5; lo <= 5; ++lo) {
            for (int hi = -5; hi <= 5; ++hi) {
                out.push_back("CL\t" + std::to_string(v) + "\t" + std::to_string(lo) +
                              "\t" + std::to_string(hi) + "\t" +
                              std::to_string(BuildingPlan::clamp(v, lo, hi)));
            }
        }
    }

    // -----------------------------------------------------------------
    // Part 2 — recipe.
    // -----------------------------------------------------------------

    int rc = 0;
    for (std::size_t oi = 0; oi < occ().size(); ++oi) {
        for (int ob2 = 0; ob2 < 2; ++ob2) {
            for (int area = 1; area <= 900; area += 7) {
                JavaRandom r(static_cast<std::int64_t>(area) * 131 +
                             static_cast<std::int64_t>(oi) * 17 + ob2);
                const std::vector<std::string> types =
                    BuildingPlan::recipe(area, occ()[oi], ob2 == 1, r);
                out.push_back("RC\t" + std::to_string(rc) + "\t" + std::to_string(area) +
                              "\t" + esc(occ()[oi]) + "\t" + std::to_string(ob2) + "\t" +
                              std::to_string(types.size()) + "\t" + join(types));
                ++rc;
            }
        }
    }

    // -----------------------------------------------------------------
    // Part 3 — allocateWeightedSizes.
    // -----------------------------------------------------------------

    const std::vector<std::vector<std::string>> lists = {
        {"bedroom"},
        {"bedroom", "bathroom"},
        {"bathroom", "bedroom", "closet"},
        {"livingroom", "kitchen", "bedroom", "bathroom"},
        {"closet", "closet", "closet", "closet", "closet"},
        {"hall", "livingroom", "kitchen", "bedroom", "bedroom", "bathroom"},
        {"unknownroomtype", "bedroom"},
    };

    int ai = 0;
    for (const auto& list : lists) {
        for (int length = 0; length <= 60; ++length) {
            for (int min = 1; min <= 4; ++min) {
                const std::vector<int> sizes =
                    BuildingPlan::allocateWeightedSizes(list, length, min);
                std::string b;
                for (std::size_t i = 0; i < sizes.size(); ++i) {
                    if (i > 0) b += ',';
                    b += std::to_string(sizes[i]);
                }
                out.push_back("AW\t" + std::to_string(ai) + "\t" + std::to_string(length) +
                              "\t" + std::to_string(min) + "\t" +
                              std::to_string(sizes.size()) + "\t" + b);
                ++ai;
            }
        }
    }

    // -----------------------------------------------------------------
    // Part 4 — the trim family.
    // -----------------------------------------------------------------

    int ti = 0;
    for (const auto& list : lists) {
        for (int cap = 0; cap <= 8; ++cap) {
            out.push_back("TR\t" + std::to_string(ti) + "\t" + std::to_string(cap) + "\t" +
                          join(BuildingPlan::trimRowRooms(list, cap)));
            ++ti;
        }
        for (int w = 1; w <= 30; w += 3) {
            for (int h = 1; h <= 30; h += 3) {
                out.push_back("TD\t" + std::to_string(ti) + "\t" + std::to_string(w) +
                              "\t" + std::to_string(h) + "\t" +
                              join(BuildingPlan::trimDwellingRooms(w, h, list)));
                std::vector<std::string> side(list);
                BuildingPlan::trimSideRoomCount(side, h);
                out.push_back("TS\t" + std::to_string(ti) + "\t" + std::to_string(w) +
                              "\t" + std::to_string(h) + "\t" + join(side));
                out.push_back("TC\t" + std::to_string(ti) + "\t" + std::to_string(w) +
                              "\t" + std::to_string(h) + "\t" +
                              join(BuildingPlan::trimToCapacity(list, w, h)));
                ++ti;
            }
        }
        out.push_back("FO\t" + std::to_string(ti) + "\t" +
                      std::to_string(BuildingPlan::findOptionalRoomFromEnd(list)) + "\t" +
                      std::to_string(BuildingPlan::findBedroomIndex(list)) + "\t" +
                      std::to_string(BuildingPlan::countBedrooms(list)));
        ++ti;
    }

    // -----------------------------------------------------------------
    // Part 5 — fixed layout cases (the self-test's own footprints).
    // -----------------------------------------------------------------

    const int cases[][2] = {
        {12, 10}, {16, 10}, {24, 7},  {15, 14}, {22, 14}, {20, 17}, {5, 4},
        {30, 6},  {4, 3},   {18, 12}, {25, 16}, {30, 20}, {40, 20}};
    const std::size_t nCases = sizeof(cases) / sizeof(cases[0]);

    int fi = 0;
    for (const BuildingPlan::Facing facing : BuildingPlan::facingValues()) {
        for (std::size_t c = 0; c < nCases; ++c) {
            const std::int64_t seed = static_cast<std::int64_t>(cases[c][0]) * 71 +
                                      static_cast<std::int64_t>(cases[c][1]) * 31;
            JavaRandom r(seed);
            const std::vector<std::string> types =
                BuildingPlan::recipe(cases[c][0] * cases[c][1], "Residential", false, r);
            const std::vector<BuildingPlan::Room> rooms =
                BuildingPlan::plan(0, 0, cases[c][0], cases[c][1], types, facing, r);
            emit(out, "F", fi, BuildingPlan::facingName(facing), 0, 0, cases[c][0],
                 cases[c][1], seed, types, rooms);
            ++fi;
        }
    }

    // -----------------------------------------------------------------
    // Part 6 — randomised corpus, 12,000 layouts.
    // -----------------------------------------------------------------

    const std::vector<BuildingPlan::Facing>& facings = BuildingPlan::facingValues();

    JavaRandom gen(20260901LL);
    for (int k = 0; k < 12000; ++k) {
        const int w = 1 + gen.nextInt(60);
        const int h = 1 + gen.nextInt(60);
        const int x = gen.nextInt(400) - 200;
        const int y = gen.nextInt(400) - 200;
        const BuildingPlan::Facing facing =
            facings[static_cast<std::size_t>(gen.nextInt(static_cast<std::int32_t>(facings.size())))];
        const std::string occName =
            occ()[static_cast<std::size_t>(gen.nextInt(static_cast<std::int32_t>(occ().size())))];
        const bool outb = gen.nextDouble() < 0.15;
        const std::int64_t seed = gen.nextLong();

        JavaRandom r(seed);
        std::vector<std::string> types = BuildingPlan::recipe(w * h, occName, outb, r);

        // Occasionally force a hall so hubHallLayout is reached on small
        // footprints too, not only on 420+ tile ones.
        // NOTE: the gen.nextDouble() is drawn unconditionally in Java because
        // && evaluates left to right and the draw is the LEFT operand.
        if (gen.nextDouble() < 0.20 && types.size() > 1) {
            types.push_back("hall");
        }

        const std::vector<BuildingPlan::Room> rooms =
            BuildingPlan::plan(x, y, w, h, types, facing, r);
        emit(out, "K", k, BuildingPlan::facingName(facing), x, y, w, h, seed, types, rooms);
    }

    // -----------------------------------------------------------------
    // Part 7 — the two-argument plan overload (SOUTH default).
    // -----------------------------------------------------------------

    JavaRandom gen2(20260902LL);
    for (int k = 0; k < 2000; ++k) {
        const int w = 1 + gen2.nextInt(40);
        const int h = 1 + gen2.nextInt(40);
        const std::int64_t seed = gen2.nextLong();
        JavaRandom r(seed);
        const std::vector<std::string> types =
            BuildingPlan::recipe(w * h, "Residential", false, r);
        const std::vector<BuildingPlan::Room> rooms =
            BuildingPlan::plan(0, 0, w, h, types, r);
        emit(out, "D", k, "DEFAULT", 0, 0, w, h, seed, types, rooms);
    }

    // -----------------------------------------------------------------
    // Part 8 — arbitrary type lists (orderings recipe() never produces),
    // plus reorderPrivateRooms directly. See BuildingPlanOracle.java.
    // -----------------------------------------------------------------

    const std::vector<std::string> palette = {
        "livingroom", "kitchen", "bathroom", "bedroom", "kidsbedroom",
        "closet", "laundry", "garage", "diningroom", "office", "janitor",
        "hall", "lobby", "barn", "shed", "garagestorage", "empty",
        "unknownroomtype"};
    const std::int32_t pn = static_cast<std::int32_t>(palette.size());

    JavaRandom gen3(20260903LL);
    for (int k = 0; k < 6000; ++k) {
        const int n = 1 + gen3.nextInt(9);
        std::vector<std::string> types;
        for (int i = 0; i < n; ++i) {
            types.push_back(palette[static_cast<std::size_t>(gen3.nextInt(pn))]);
        }
        const int w = 1 + gen3.nextInt(45);
        const int h = 1 + gen3.nextInt(45);
        const int x = gen3.nextInt(100) - 50;
        const int y = gen3.nextInt(100) - 50;
        const BuildingPlan::Facing facing =
            facings[static_cast<std::size_t>(gen3.nextInt(static_cast<std::int32_t>(facings.size())))];
        const std::int64_t seed = gen3.nextLong();

        JavaRandom r(seed);
        const std::vector<BuildingPlan::Room> rooms =
            BuildingPlan::plan(x, y, w, h, types, facing, r);
        emit(out, "P", k, BuildingPlan::facingName(facing), x, y, w, h, seed, types, rooms);
    }

    JavaRandom rp(20260904LL);
    JavaRandom rpGen(20260905LL);
    for (int k = 0; k < 4000; ++k) {
        const int n = 1 + rpGen.nextInt(7);
        std::vector<std::string> types;
        for (int i = 0; i < n; ++i) {
            types.push_back(palette[static_cast<std::size_t>(rpGen.nextInt(pn))]);
        }
        BuildingPlan::reorderPrivateRooms(types, rp);
        out.push_back("RP\t" + std::to_string(k) + "\t" + std::to_string(types.size()) +
                      "\t" + join(types));
    }

    std::ofstream f(argv[1], std::ios::binary);
    for (const std::string& line : out) {
        f << line << '\n';
    }
    f.close();

    std::printf("cpp  buildingplan: %zu lines -> %s\n", out.size(), argv[1]);
    return 0;
}

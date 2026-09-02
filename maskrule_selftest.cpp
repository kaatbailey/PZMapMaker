// maskrule_selftest.cpp — port of MaskRule.java's main().
//
// Self-test: reproduce vanilla from neighbour materials alone.
//
// Every case is a real square of Muldraugh 42_40 recorded in STATE §26, with
// the mask tiles actually present in the lotpack. If the rule cannot regenerate
// them it is wrong, and this catches it in a second without touching map data.
//
// THE BAR (chunk Definition of Done): this must match Java's output EXACTLY,
// INCLUDING ITS EXIT CODE. Step 4's BuildingPlan self-test failed on both sides
// and matching the failure was the correct outcome. Here Java SUCCEEDS —
// measured at PZMapCreation 022d938: 15 lines, "9 / 9 cases pass", exit 0 — so
// this must succeed identically. A port that exits 1 is wrong for exactly the
// same reason step 4's would have been wrong to exit 0.
//
// AND THE BAR IS NOT EVIDENCE. STATE §29 records this self-test passing 8/8
// while N and W were transposed, because the set-to-offset cases never exercise
// the neighbour lookup. The direction-vector block below is the ninth case,
// added in response. §40 then measured the general shape: ten deliberate
// mutations of BuildingPlan left ITS self-test byte-identical. A green run here
// means the port reproduces Java, not that the rule is right. The digest in
// palettes_oracle is the instrument that can fail.
//
// Note the counting: 12 detail lines are printed but 9 cases are counted — the
// four `dir` lines are ONE case (pass++ fires once after the loop). A port
// emitting "12 / 12" has diverged even with every individual line matching.

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "java_random.hpp"
#include "maskrule.hpp"

using pzformat::JavaRandom;
using pzformat::MaskRule;
using Dir = MaskRule::Dir;
using DirSet = MaskRule::DirSet;

namespace {

constexpr int DARK = 16;  // Grass_Dark block base

/// java.util.Arrays.toString(int[]) — "[]", "[26]", "[24, 27]".
std::string arraysToString(const std::vector<int>& a) {
    std::string s = "[";
    for (std::size_t i = 0; i < a.size(); i++) {
        if (i > 0) s += ", ";
        s += std::to_string(a[i]);
    }
    s += "]";
    return s;
}

std::string sorted(const std::vector<int>& a) {
    std::vector<int> c = a;
    std::sort(c.begin(), c.end());
    return arraysToString(c);
}

/// Java's String.format("%-22s", s) — left-justify, pad to 22, never truncate.
std::string pad22(const std::string& s) {
    std::string out = s;
    while (out.size() < 22) out += ' ';
    return out;
}

/// True when 400 draws all land in `accepted` and every option appears at
/// least once.
///
/// Compares SORTED, because the direction set has no defined iteration order
/// and the tile order within a square is not load-bearing. An earlier version
/// compared exact sequences and failed on correct output — the checker was
/// wrong, not the rule.
bool check(const std::string& label, DirSet dirs,
           const std::vector<std::vector<int>>& accepted) {
    JavaRandom rng(12345);
    std::vector<std::string> want;
    for (const auto& a : accepted) want.push_back(sorted(a));
    std::set<std::string> seen;
    std::vector<int> bad;
    bool haveBad = false;

    for (int i = 0; i < 400 && !haveBad; i++) {
        const std::vector<int> got = MaskRule::masks(DARK, dirs, 2, rng);
        const std::string key = sorted(got);
        if (std::find(want.begin(), want.end(), key) != want.end()) {
            seen.insert(key);
        } else {
            bad = got;
            haveBad = true;
        }
    }

    const std::set<std::string> wantDistinct(want.begin(), want.end());
    const bool allSeen = seen.size() == wantDistinct.size();
    const std::string verdict =
        haveBad ? "FAIL unexpected " + arraysToString(bad)
                : (allSeen ? "PASS" : "FAIL a variant never appeared");
    std::printf("%s %s\n", pad22(label).c_str(), verdict.c_str());
    return !haveBad && allSeen;
}

} // namespace

int main() {
    int pass = 0, fail = 0;

    // Direction vectors, asserted against the grid convention. The
    // set-to-offset cases below all passed while N and W were transposed,
    // because they never exercise the neighbour lookup. This does.
    const int want[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};  // N, W, E, S
    const Dir order[4] = {Dir::N, Dir::W, Dir::E, Dir::S};
    bool dirsOk = true;
    for (int i = 0; i < 4; i++) {
        const Dir d = order[i];
        const bool ok = MaskRule::dx(d) == want[i][0]
                        && MaskRule::dy(d) == want[i][1]
                        && MaskRule::ord(d) == i;
        if (!ok) dirsOk = false;
        std::printf("%s %s  dx=%d dy=%d ord=%d\n",
                    pad22(std::string("dir ") + MaskRule::name(d)).c_str(),
                    ok ? "PASS" : "FAIL",
                    MaskRule::dx(d), MaskRule::dy(d), MaskRule::ord(d));
    }
    for (const Dir d : MaskRule::kDirs) {
        const Dir o = MaskRule::opposite(d);
        if (MaskRule::dx(o) != -MaskRule::dx(d) || MaskRule::dy(o) != -MaskRule::dy(d)) {
            dirsOk = false;
            std::printf("dir opposite            FAIL %s vs %s\n",
                        MaskRule::name(d), MaskRule::name(o));
        }
    }
    if (!dirsOk) fail++;
    pass++;

    // (116,200) Grass_Medium base, Grass_Dark to the E only.
    // Vanilla wrote _26 at y=199,200 and _30 at y=201 — both variants of
    // the E side tile, which is why variant choice is random.
    fail += check("single side E", DirSet::of(Dir::E), {{26}, {30}}) ? 0 : 1;
    pass++;

    // (114,200) and (113,201): Grass_Dark to the N and W. One corner tile.
    fail += check("adjacent N+W", DirSet::of(Dir::N, Dir::W), {{17}}) ? 0 : 1;
    pass++;

    // (118,199): Grass_Dark to the S and W.
    fail += check("adjacent S+W", DirSet::of(Dir::S, Dir::W), {{19}}) ? 0 : 1;
    pass++;

    // (111,201) and (117,202): Grass_Dark to the E and N.
    fail += check("adjacent E+N", DirSet::of(Dir::E, Dir::N), {{20}}) ? 0 : 1;
    pass++;

    // (117,198): Grass_Dark to the N and S — OPPOSITE, so two side tiles,
    // never a corner.
    fail += check("opposite N+S", DirSet::of(Dir::N, Dir::S),
                  {{24, 27}, {28, 31}, {24, 31}, {27, 28}}) ? 0 : 1;
    pass++;

    // (111,199): Grass_Dark to the E, N and S. Vanilla wrote _20 (E+N) and
    // _18 (E+S) — two corners sharing E, not a corner plus a side.
    fail += check("three E+N+S", DirSet::of(Dir::E, Dir::N, Dir::S),
                  {{20, 18}}) ? 0 : 1;
    pass++;

    // (112,200): an isolated Grass_Medium square, Grass_Dark on all four
    // sides. Vanilla wrote _17, _20, _19, _18 — all four corners.
    fail += check("all four", DirSet::of(Dir::N, Dir::W, Dir::E, Dir::S),
                  {{17, 20, 19, 18}}) ? 0 : 1;
    pass++;

    // Street blocks have ONE variant set. Road_02 is block 16 in
    // blends_street_01; vanilla (90,190) carried _26 (E) and _25 (W).
    JavaRandom roadRng(1);
    const std::vector<int> roadE = MaskRule::masks(16, DirSet::of(Dir::E), 1, roadRng);
    const bool roadOk = roadE == std::vector<int>{26};
    std::printf("%s %s  got %s\n", pad22("street single set").c_str(),
                roadOk ? "PASS" : "FAIL", arraysToString(roadE).c_str());
    if (!roadOk) fail++;
    pass++;

    std::printf("\n%d / %d cases pass\n", pass - fail, pass);
    if (fail > 0) {
        std::printf("The rule does not reproduce vanilla. Do not wire it in.\n");
        return 1;
    }
    std::printf("Rule reproduces every recorded vanilla square.\n");
    return 0;
}

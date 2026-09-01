// buildingplan_selftest.cpp — port of BuildingPlan.java's main (line 2614)
// and its footprint check (line 3245). This is the PRIMARY oracle for port
// step 4: the C++ output must match `java -cp out pzformat.BuildingPlan`
// line for line, including the exit code.
//
// IMPORTANT: as of 2026-09-01 the JAVA self-test FAILS — "no corridor-shaped
// rooms" reports worst 5.7 at NORTH 40x20 bathroom[23,4 17x3], and Java exits
// 1. Matching Java means REPRODUCING THAT FAILURE. A C++ port that "passes"
// has diverged. See the FINDINGS block.
//
// The %.1f on the aspect line is not cosmetic. Java's Formatter rounds
// HALF_UP over the SHORTEST round-trip decimal representation of the double;
// C's printf rounds half-to-even over the EXACT value. The two disagree
// wherever the shortest representation is a tie at the second decimal —
// aspect() returns a ratio of small integers, so ties like 27/20 = 1.35 are
// reachable and the disagreement is direction-dependent. javaFormat1f
// reproduces Java.

#include "buildingplan.hpp"

#include <charconv>
#include <cstdio>
#include <string>
#include <vector>

namespace pzformat {

// ---------------------------------------------------------------------
// Java's %.1f
// ---------------------------------------------------------------------

std::string BuildingPlan::javaFormat1f(double v) {
    const bool negative = std::signbit(v);
    if (negative) v = -v;

    // Shortest round-trip digits, as Double.toString produces them in JDK 19+.
    char buf[64];
    const auto res = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::scientific);
    const std::string sci(buf, res.ptr);

    // Split "d.dddde±dd" into a digit string and a decimal exponent.
    const std::size_t epos = sci.find('e');
    std::string mantissa = sci.substr(0, epos);
    const int exponent = std::stoi(sci.substr(epos + 1));

    std::string digits;
    for (const char c : mantissa) {
        if (c != '.') digits += c;
    }

    // Value == 0.<digits> * 10^(exponent + 1).
    int pointAt = exponent + 1;

    // Round HALF_UP to one digit after the decimal point, i.e. keep
    // (pointAt + 1) digits.
    const int keep = pointAt + 1;

    if (keep <= 0) {
        // Rounds to 0.0, unless keep == 0 and the leading digit is >= 5.
        if (keep == 0 && !digits.empty() && digits[0] >= '5') {
            digits = "1";
            pointAt = 1;
        } else {
            digits = "0";
            pointAt = 1;
        }
    } else if (static_cast<int>(digits.size()) > keep) {
        const bool roundUp = digits[static_cast<std::size_t>(keep)] >= '5';
        digits.resize(static_cast<std::size_t>(keep));
        if (roundUp) {
            int i = keep - 1;
            for (; i >= 0; --i) {
                if (digits[static_cast<std::size_t>(i)] != '9') {
                    ++digits[static_cast<std::size_t>(i)];
                    break;
                }
                digits[static_cast<std::size_t>(i)] = '0';
            }
            if (i < 0) {
                digits.insert(digits.begin(), '1');
                ++pointAt;
            }
        }
    }

    // Pad so there are exactly pointAt integer digits and one fractional.
    while (static_cast<int>(digits.size()) < pointAt + 1) digits += '0';

    std::string intPart = pointAt <= 0 ? "0" : digits.substr(0, static_cast<std::size_t>(pointAt));
    const std::string fracPart =
        digits.substr(static_cast<std::size_t>(pointAt < 0 ? 0 : pointAt), 1);

    if (intPart.empty()) intPart = "0";

    return (negative ? "-" : "") + intPart + "." + fracPart;
}

std::string BuildingPlan::roomsToString(const std::vector<Room>& rooms) {
    // java.util.AbstractCollection.toString
    std::string s = "[";
    for (std::size_t i = 0; i < rooms.size(); ++i) {
        if (i > 0) s += ", ";
        s += rooms[i].toString();
    }
    s += "]";
    return s;
}

// ---------------------------------------------------------------------
// FOOTPRINT CHECK  (Java line 3245)
// ---------------------------------------------------------------------

int BuildingPlan::check(const std::string& label, int w, int h,
                        const std::vector<Room>& rooms) {
    // Java: int[w][h], zero-initialised.
    std::vector<std::vector<int>> grid(static_cast<std::size_t>(w),
                                       std::vector<int>(static_cast<std::size_t>(h), 0));

    int overlaps = 0;

    for (const Room& room : rooms) {
        for (int i = room.x; i < room.x + room.w; ++i) {
            for (int j = room.y; j < room.y + room.h; ++j) {
                if (i < 0 || j < 0 || i >= w || j >= h) {
                    ++overlaps;
                    continue;
                }
                // Java: if (grid[i][j]++ > 0) — post-increment, tests the
                // value BEFORE the increment.
                if (grid[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)]++ > 0) {
                    ++overlaps;
                }
            }
        }
    }

    int gaps = 0;
    for (const auto& column : grid) {
        for (const int value : column) {
            if (value == 0) ++gaps;
        }
    }

    int invalid = 0;
    for (const Room& room : rooms) {
        if (room.w < 1 || room.h < 1) ++invalid;
    }

    const bool ok = !rooms.empty() && gaps == 0 && overlaps == 0 && invalid == 0;

    std::printf("%-35s %s  %d rooms, %d gaps, %d overlaps\n", label.c_str(),
                ok ? "PASS" : "FAIL", static_cast<int>(rooms.size()), gaps, overlaps);

    if (!ok) {
        for (const Room& room : rooms) {
            std::printf("      %s\n", room.toString().c_str());
        }
        return 1;
    }

    return 0;
}

// ---------------------------------------------------------------------
// SELF TEST  (Java main, line 2614)
// ---------------------------------------------------------------------

int BuildingPlan::selfTest() {
    int fail = 0;

    JavaRandom rng(42);

    const int cases[][2] = {
        {12, 10}, {16, 10}, {24, 7},  {15, 14}, {22, 14}, {20, 17}, {5, 4},
        {30, 6},  {4, 3},   {18, 12}, {25, 16}, {30, 20}, {40, 20},
    };
    const std::size_t nCases = sizeof(cases) / sizeof(cases[0]);

    // Basic tiling test.
    for (const Facing facing : facingValues()) {
        for (std::size_t c = 0; c < nCases; ++c) {
            const int w = cases[c][0];
            const int h = cases[c][1];

            const std::vector<std::string> types = recipe(w * h, "Residential", false, rng);
            const std::vector<Room> rooms = plan(0, 0, w, h, types, facing, rng);

            const std::string label = std::string(facingName(facing)) + " " +
                                      std::to_string(w) + "x" + std::to_string(h);
            fail += check(label, w, h, rooms);
        }
    }

    // Livingroom must always touch the road/front.
    int badFront = 0;
    for (const Facing facing : facingValues()) {
        for (int i = 0; i < 100; ++i) {
            JavaRandom r(i + 1000);

            const std::vector<std::string> types = recipe(250, "Residential", false, r);
            const std::vector<Room> rooms = plan(0, 0, 20, 15, types, facing, r);

            const Room* living = nullptr;
            for (const Room& room : rooms) {
                if (room.type == "livingroom") {
                    living = &room;
                    break;
                }
            }

            if (living == nullptr) {
                ++badFront;
                continue;
            }

            bool onFront = false;
            switch (facing) {
                case Facing::NORTH: onFront = living->y == 0; break;
                case Facing::SOUTH: onFront = living->y + living->h == 15; break;
                case Facing::WEST:  onFront = living->x == 0; break;
                case Facing::EAST:  onFront = living->x + living->w == 20; break;
            }

            if (!onFront) ++badFront;
        }
    }

    std::printf("%-35s %s  %d misplaced\n", "livingroom at front",
                badFront == 0 ? "PASS" : "FAIL", badFront);
    if (badFront > 0) ++fail;

    // Bedrooms must never be marked as exterior-door rooms.
    int badBedroomDoors = 0;
    int markedDoors = 0;
    for (const Facing facing : facingValues()) {
        for (int i = 0; i < 100; ++i) {
            JavaRandom r(i + 5000);

            const std::vector<std::string> types = recipe(300, "Residential", false, r);
            const std::vector<Room> rooms = plan(0, 0, 20, 15, types, facing, r);

            for (const Room& room : rooms) {
                if (!room.entrance) continue;
                ++markedDoors;
                if (!room.canTakeDoor()) ++badBedroomDoors;
            }
        }
    }

    std::printf("%-35s %s  %d bad of %d marked\n", "no bedroom entrances",
                badBedroomDoors == 0 ? "PASS" : "FAIL", badBedroomDoors, markedDoors);
    if (badBedroomDoors > 0) ++fail;

    // Agriculture must always produce a barn.
    // Java evaluates the recipe(...) argument before calling plan(...), and
    // both draw from the same `rng`; hoisted so the order is fixed here too.
    const std::vector<std::string> barnTypes = recipe(132, "Agriculture", false, rng);
    const std::vector<Room> barn = plan(0, 0, 12, 11, barnTypes, Facing::SOUTH, rng);

    const bool barnOk = barn.size() == 1 && barn[0].type == "barn";

    std::printf("%-35s %s  %s\n", "Agriculture -> barn", barnOk ? "PASS" : "FAIL",
                roomsToString(barn).c_str());
    if (!barnOk) ++fail;

    // Dwelling recipe must always contain a bathroom.
    int noBath = 0;
    for (int i = 0; i < 500; ++i) {
        JavaRandom r(i);
        const std::vector<std::string> types = recipe(100, "Residential", false, r);

        bool hasBath = false;
        for (const std::string& t : types) {
            if (t == "bathroom") {
                hasBath = true;
                break;
            }
        }
        if (!hasBath) ++noBath;
    }

    std::printf("%-35s %s  %d without\n", "dwelling always has bath",
                noBath == 0 ? "PASS" : "FAIL", noBath);
    if (noBath > 0) ++fail;

    // NEW HUB TEST. A 300-tile house is deliberately NOT supposed to receive
    // a hall — the architectural change from the previous version.
    int unwantedSmallHalls = 0;
    for (int i = 0; i < 200; ++i) {
        JavaRandom r(i + 9000);
        const std::vector<std::string> types = recipe(300, "Residential", false, r);
        const std::vector<Room> rooms = plan(0, 0, 20, 15, types, Facing::SOUTH, r);

        for (const Room& room : rooms) {
            if (room.type == "hall") ++unwantedSmallHalls;
        }
    }

    std::printf("%-35s %s  %d halls\n", "300-tile house has NO hall",
                unwantedSmallHalls == 0 ? "PASS" : "FAIL", unwantedSmallHalls);
    if (unwantedSmallHalls > 0) ++fail;

    // A genuinely large house should receive a hall. 500 tiles produces
    // enough bedrooms to justify one.
    int missingLargeHalls = 0;
    for (int i = 0; i < 200; ++i) {
        JavaRandom r(i + 12000);
        const std::vector<std::string> types = recipe(500, "Residential", false, r);
        const std::vector<Room> rooms = plan(0, 0, 25, 20, types, Facing::SOUTH, r);

        bool hall = false;
        for (const Room& room : rooms) {
            if (room.type == "hall") {
                hall = true;
                break;
            }
        }
        if (!hall) ++missingLargeHalls;
    }

    std::printf("%-35s %s  %d missing\n", "large house gets hall",
                missingLargeHalls == 0 ? "PASS" : "FAIL", missingLargeHalls);
    if (missingLargeHalls > 0) ++fail;

    // Check that the 300-tile no-hall layout actually contains the core rooms.
    int missingCore = 0;
    for (int i = 0; i < 200; ++i) {
        JavaRandom r(i + 15000);
        const std::vector<std::string> types = recipe(300, "Residential", false, r);
        const std::vector<Room> rooms = plan(0, 0, 20, 15, types, Facing::SOUTH, r);

        bool living = false, kitchen = false, bedroom = false, bath = false;
        for (const Room& room : rooms) {
            if (room.type == "livingroom") living = true;
            else if (room.type == "kitchen") kitchen = true;
            else if (room.type == "bedroom" || room.type == "kidsbedroom") bedroom = true;
            else if (room.type == "bathroom") bath = true;
        }

        if (!living || !kitchen || !bedroom || !bath) ++missingCore;
    }

    std::printf("%-35s %s  %d missing\n", "300-tile house has core rooms",
                missingCore == 0 ? "PASS" : "FAIL", missingCore);
    if (missingCore > 0) ++fail;

    // Aspect check. Halls are intentionally excluded because a hall is
    // allowed to be elongated. The private-room packing should not create
    // extreme corridor-shaped bedrooms/bathrooms.
    double worst = 0.0;
    std::string worstAt;

    for (const Facing facing : facingValues()) {
        for (std::size_t c = 0; c < nCases; ++c) {
            const int w = cases[c][0];
            const int h = cases[c][1];

            // Java: new Random(w * 71L + h * 31L) — long arithmetic.
            JavaRandom r(static_cast<std::int64_t>(w) * 71 + static_cast<std::int64_t>(h) * 31);

            const std::vector<std::string> types = recipe(w * h, "Residential", false, r);
            const std::vector<Room> rooms = plan(0, 0, w, h, types, facing, r);

            for (const Room& room : rooms) {
                if (room.type == "hall") continue;

                const double a = aspect(room.w, room.h);
                if (a > worst) {
                    worst = a;
                    worstAt = std::string(facingName(facing)) + " " + std::to_string(w) +
                              "x" + std::to_string(h) + " " + room.toString();
                }
            }
        }
    }

    std::printf("%-35s %s  worst %s  %s\n", "no corridor-shaped rooms",
                worst <= 4.5 ? "PASS" : "FAIL", javaFormat1f(worst).c_str(),
                worstAt.c_str());
    if (worst > 4.5) ++fail;

    std::printf("\n");

    if (fail == 0) {
        std::printf("all BuildingPlan hub-layout tests pass\n");
        return 0;
    }

    std::printf("%d BuildingPlan tests FAILED\n", fail);
    return 1;
}

}  // namespace pzformat

int main() {
    return pzformat::BuildingPlan::selfTest();
}

// buildingplan.cpp — port of pzformat/BuildingPlan.java.
//
// DWELLING LAYOUT (Java class doc, kept verbatim in substance because it is
// the justification for the grammar):
//
// Dwellings use a HUB MODEL.
//
// SMALL / MEDIUM HOUSES — the livingroom is the front/public hub.
//
//              FRONT / ROAD
//                    |
//        +-----------+--------+
//        |       LIVING       |
//        |       ROOM     | K |
//        |                | I |
//        +----------------+---+
//        | BEDROOM        |BED|
//        +---------+------+---+
//        | BATH    | BEDROOM  |
//        +---------+----------+
//
// There is deliberately NO HALL. The livingroom and kitchen occupy the
// valuable public/front portion; bedrooms and service rooms directly abut
// that public zone.
//
// LARGE HOUSES — once the dwelling is genuinely large enough, a hall/spine
// can appear. The hall is not used merely because a house has two bedrooms:
//
//   small/medium:  livingroom -> kitchen/private rooms
//   large:         livingroom/kitchen -> hall -> private rooms
//
// The old front/middle/back banding grammar has been removed from the
// dwelling path. Non-dwellings continue to use weighted recursive splitting.

#include "buildingplan.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace pzformat {

namespace {

// ---------------------------------------------------------------------
// ROOM WEIGHTS  (Java line 155: a LinkedHashMap filled in a static block)
//
// Insertion-ordered, matching Java's declaration order. Read-only lookup —
// see hazard 3 in the header for why the order is not load-bearing today.
// ---------------------------------------------------------------------
const std::vector<std::pair<const char*, double>>& weightTable() {
    static const std::vector<std::pair<const char*, double>> kWeights = {
        {"closet", 2.0},      {"bathroom", 6.0},  {"laundry", 6.0},
        {"janitor", 9.0},     {"kidsbedroom", 15.0}, {"bedroom", 16.0},
        {"diningroom", 20.0}, {"kitchen", 24.0},  {"office", 27.0},
        {"garage", 30.0},     {"hall", 40.0},     {"livingroom", 42.0},
    };
    return kWeights;
}

// ---------------------------------------------------------------------
// EXTERIOR DOORS  (Java line 184: java.util.Set.of(...))
//
// Sorted, so the C++ side has a defined order even though only membership
// is ever asked. Java's Set.of iteration order is randomised per JVM run.
// ---------------------------------------------------------------------
const std::vector<const char*>& entranceTable() {
    static const std::vector<const char*> kEntrance = {
        "barn", "diningroom", "garagestorage", "hall", "kitchen",
        "laundry", "livingroom", "lobby", "shed",
    };
    return kEntrance;
}

/// java.util.List.remove(Object): removes the FIRST occurrence, returns
/// whether anything was removed.
bool removeFirst(std::vector<std::string>& v, const std::string& value) {
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == value) {
            v.erase(v.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

/// java.util.List.indexOf(Object), or -1.
int indexOf(const std::vector<std::string>& v, const std::string& value) {
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == value) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace

// =====================================================================
// ROOM / FACING
// =====================================================================

std::string BuildingPlan::Room::toString() const {
    std::string s = type;
    s += "[";
    s += std::to_string(x);
    s += ",";
    s += std::to_string(y);
    s += " ";
    s += std::to_string(w);
    s += "x";
    s += std::to_string(h);
    s += "]";
    if (entrance) s += "*";
    return s;
}

BuildingPlan::Facing BuildingPlan::opposite(Facing f) {
    switch (f) {
        case Facing::NORTH: return Facing::SOUTH;
        case Facing::SOUTH: return Facing::NORTH;
        case Facing::EAST:  return Facing::WEST;
        case Facing::WEST:  return Facing::EAST;
    }
    return Facing::NORTH;
}

const char* BuildingPlan::facingName(Facing f) {
    switch (f) {
        case Facing::NORTH: return "NORTH";
        case Facing::SOUTH: return "SOUTH";
        case Facing::EAST:  return "EAST";
        case Facing::WEST:  return "WEST";
    }
    return "?";
}

const std::vector<BuildingPlan::Facing>& BuildingPlan::facingValues() {
    // Java's declaration order, which Facing.values() preserves.
    static const std::vector<Facing> kValues = {Facing::NORTH, Facing::SOUTH,
                                                Facing::EAST, Facing::WEST};
    return kValues;
}

double BuildingPlan::weightFor(const std::string& room) {
    for (const auto& [name, weight] : weightTable()) {
        if (room == name) return weight;
    }
    return WEIGHT_DEFAULT;
}

bool BuildingPlan::isEntranceType(const std::string& type) {
    for (const char* e : entranceTable()) {
        if (type == e) return true;
    }
    return false;
}

// =====================================================================
// PUBLIC HELPERS
// =====================================================================

bool BuildingPlan::openBetween(const std::string& a, const std::string& b,
                               JavaRandom& rng) {
    const bool core = (a == "livingroom" && b == "kitchen") ||
                      (a == "kitchen" && b == "livingroom");
    // Java: core && rng.nextDouble() < LK_OPEN. && short-circuits, so the
    // draw does NOT happen when core is false. Reproduced exactly.
    return core && rng.nextDouble() < LK_OPEN;
}

double BuildingPlan::hallChance(int rooms) {
    if (rooms <= 3) return 0.0;
    if (rooms <= 5) return 0.05;
    if (rooms <= 7) return 0.35;
    if (rooms <= 10) return 0.70;
    return 0.90;
}

// =====================================================================
// RECIPE
// =====================================================================

std::string BuildingPlan::pick(JavaRandom& rng, double p, const std::string& a,
                               const std::string& b) {
    return rng.nextDouble() < p ? a : b;
}

std::vector<std::string> BuildingPlan::recipe(int area, const std::string& occ,
                                              bool outbuilding, JavaRandom& rng) {
    if (occ == "Agriculture") {
        return {"barn"};
    }

    if (outbuilding) {
        return {pick(rng, 0.70, "garagestorage", "shed")};
    }

    if (area <= 24) {
        // HAZARD 2. Java evaluates method arguments left to right, so the
        // INNER pick draws before the OUTER one. C++ leaves the order of
        // function arguments indeterminately sequenced, so writing this as
        // one nested expression would let the compiler draw them the other
        // way and shift every subsequent value out of this generator.
        const std::string inner = pick(rng, 0.50, "empty", "shed");
        return {pick(rng, 0.52, "garagestorage", inner)};
    }

    // Every normal dwelling starts with these four core rooms.
    std::vector<std::string> rooms = {"livingroom", "kitchen", "bathroom", "bedroom"};

    if (area > 60 && rng.nextDouble() < 0.62) rooms.push_back("closet");
    if (area > 110 && rng.nextDouble() < 0.16) rooms.push_back("laundry");
    if (area > 140 && rng.nextDouble() < 0.08) rooms.push_back("garage");
    if (area > 130 && rng.nextDouble() < 0.09) rooms.push_back("diningroom");
    if (area > 100 && rng.nextDouble() < 0.20) rooms.push_back("kidsbedroom");

    // Additional bedrooms grow slowly.
    const int extra = std::max(0, (area - 200) / 100);
    int beds = 1;
    for (int i = 0; i < extra && beds < BEDROOM_MAX; ++i) {
        rooms.push_back(rng.nextDouble() < 0.24 ? "kidsbedroom" : "bedroom");
        ++beds;
    }

    // Second bathroom belongs to genuinely larger houses.
    if (beds >= 4 && area >= SECOND_BATH_AREA) {
        rooms.push_back("bathroom");
    }

    return rooms;
}

// =====================================================================
// MAIN PLAN ENTRY
// =====================================================================

std::vector<BuildingPlan::Room> BuildingPlan::plan(int x, int y, int w, int h,
                                                   const std::vector<std::string>& types,
                                                   Facing facing, JavaRandom& rng) {
    std::vector<Room> out;

    if (w < 1 || h < 1 || types.empty()) {
        return out;
    }

    // A single room owns the entire footprint.
    if (types.size() == 1) {
        const std::string& type = types[0];
        out.emplace_back(type, x, y, w, h, isEntranceType(type));
        return out;
    }

    // Dwelling detection.
    const bool dwelling = indexOf(types, "livingroom") >= 0;

    // Non-dwellings retain the generic recursive splitter.
    if (!dwelling) {
        std::vector<std::string> rooms = trimToCapacity(types, w, h);
        split(out, x, y, w, h, rooms, rng);
        return out;
    }

    // Extremely small footprints cannot support a full dwelling grammar.
    if (std::min(w, h) < MIN_ROOM * 2) {
        const std::string fallback = pick(rng, 0.70, "garagestorage", "shed");
        out.emplace_back(fallback, x, y, w, h, isEntranceType(fallback));
        return out;
    }

    std::vector<std::string> rooms(types);

    const bool requestedHall = removeFirst(rooms, "hall");

    // Remove the public anchors. The hub planner places them.
    removeFirst(rooms, "livingroom");
    const bool hasKitchen = removeFirst(rooms, "kitchen");

    // Protect the four conceptual core rooms by trimming only the secondary
    // list.
    rooms = trimDwellingRooms(w, h, rooms);

    return hubLayout(out, x, y, w, h, facing, hasKitchen, rooms, requestedHall, rng);
}

std::vector<BuildingPlan::Room> BuildingPlan::plan(int x, int y, int w, int h,
                                                   const std::vector<std::string>& types,
                                                   JavaRandom& rng) {
    return plan(x, y, w, h, types, Facing::SOUTH, rng);
}

// =====================================================================
// HUB LAYOUT
// =====================================================================

std::vector<BuildingPlan::Room> BuildingPlan::hubLayout(
    std::vector<Room>& out, int x, int y, int w, int h, Facing facing, bool hasKitchen,
    const std::vector<std::string>& secondary, bool requestedHall, JavaRandom& rng) {

    const int area = w * h;
    const int bedroomCount = countBedrooms(secondary);

    bool useHall = requestedHall ||
                   (area >= LARGE_HOUSE_AREA && bedroomCount >= LARGE_HOUSE_BEDROOMS);

    // Narrow buildings cannot support a hall.
    const int cross = crossAxis(w, h, facing);
    if (useHall && cross < HALL_MIN + MIN_ROOM * 2) {
        useHall = false;
    }

    // Long narrow houses use the row grammar.
    if (std::min(w, h) <= MIN_ROOM + MIN_LIVING_SIDE) {
        return hubRowLayout(out, x, y, w, h, facing, hasKitchen, secondary, rng);
    }

    if (useHall) {
        return hubHallLayout(out, x, y, w, h, facing, hasKitchen, secondary, rng);
    }

    // The normal path for the majority of houses. No hallway.
    return hubNoHallLayout(out, x, y, w, h, facing, hasKitchen, secondary, rng);
}

// ---------------------------------------------------------------------
// NO-HALL HUB
//
// The front portion is divided into LIVINGROOM | KITCHEN rather than
// LIVINGROOM | HALL; the back portion is divided directly into private
// rooms. There is no wasted circulation rectangle.
// ---------------------------------------------------------------------

std::vector<BuildingPlan::Room> BuildingPlan::hubNoHallLayout(
    std::vector<Room>& out, int x, int y, int w, int h, Facing facing, bool hasKitchen,
    const std::vector<std::string>& secondary, JavaRandom& rng) {

    const bool frontAlongY = facing == Facing::NORTH || facing == Facing::SOUTH;
    const int depth = frontAlongY ? h : w;
    const int cross = frontAlongY ? w : h;

    // If the house is too shallow, use the linear hub.
    if (depth < MIN_LIVING_SIDE + MIN_ROOM) {
        return hubRowLayout(out, x, y, w, h, facing, hasKitchen, secondary, rng);
    }

    // The public zone occupies roughly 44-48% of the depth. This makes the
    // livingroom substantial rather than a narrow front strip.
    int livingDepth;
    if (depth <= 8) {
        livingDepth = std::min(MIN_LIVING_SIDE, depth - MIN_ROOM);
    } else {
        // Java line 798: Math.round.
        livingDepth = clamp(static_cast<int>(javaRound(depth * 0.46)), MIN_LIVING_SIDE,
                            depth - MIN_ROOM);
    }

    const Rect front = frontRect(x, y, w, h, facing, livingDepth);
    const Rect back = backRect(x, y, w, h, facing, livingDepth);

    // If there is no kitchen, the livingroom owns the entire public zone.
    if (!hasKitchen || cross < MIN_LIVING_SIDE + MIN_KITCHEN_SIDE) {
        out.emplace_back("livingroom", front.x, front.y, front.w, front.h, true);
    } else {
        // The kitchen gets a meaningful portion of the frontage, but the
        // livingroom remains the larger room. Typical 20-wide house:
        //     living 12 | kitchen 8
        // rather than:
        //     living 7 | hall 6 | kitchen 7
        // Java line 860: Math.round.
        const int kitchenCross = clamp(static_cast<int>(javaRound(cross * 0.38)),
                                       MIN_KITCHEN_SIDE, cross - MIN_LIVING_SIDE);
        const int livingCross = cross - kitchenCross;

        // Randomly choose which side of the public zone the kitchen uses, so
        // that not every house reads identically, while preserving the
        // grammar.
        const bool kitchenFirst = rng.nextBoolean();

        Rect living;
        Rect kitchen;

        if (frontAlongY) {
            if (kitchenFirst) {
                kitchen = Rect(front.x, front.y, kitchenCross, front.h);
                living = Rect(front.x + kitchenCross, front.y, livingCross, front.h);
            } else {
                living = Rect(front.x, front.y, livingCross, front.h);
                kitchen = Rect(front.x + livingCross, front.y, kitchenCross, front.h);
            }
        } else {
            if (kitchenFirst) {
                kitchen = Rect(front.x, front.y, front.w, kitchenCross);
                living = Rect(front.x, front.y + kitchenCross, front.w, livingCross);
            } else {
                living = Rect(front.x, front.y, front.w, livingCross);
                kitchen = Rect(front.x, front.y + livingCross, front.w, kitchenCross);
            }
        }

        // Livingroom is the preferred exterior entrance. Kitchen remains
        // door-capable but is not automatically the front entrance.
        out.emplace_back("livingroom", living.x, living.y, living.w, living.h, true);
        out.emplace_back("kitchen", kitchen.x, kitchen.y, kitchen.w, kitchen.h, false);
    }

    // No private rooms means the public zone plus remaining footprint cannot
    // be left unfilled.
    if (secondary.empty()) {
        // Only for unusual externally supplied recipes.
        if (back.w > 0 && back.h > 0) {
            const std::string filler = hasKitchen ? "kitchen" : "livingroom";
            out.emplace_back(filler, back.x, back.y, back.w, back.h, false);
        }
        return out;
    }

    // Private rooms are packed directly across the back of the house:
    //     living/kitchen
    //     ----------------
    //     bedroom | bath | bedroom
    // Every private room touches the public zone; no hidden corridor.
    packPrivateZone(out, back, secondary, frontAlongY, rng);

    return out;
}

void BuildingPlan::packPrivateZone(std::vector<Room>& out, const Rect& region,
                                   const std::vector<std::string>& rooms,
                                   bool frontAlongY, JavaRandom& rng) {
    if (rooms.empty() || region.w <= 0 || region.h <= 0) {
        return;
    }

    std::vector<std::string> local(rooms);

    const int cross = frontAlongY ? region.w : region.h;
    const int capacity = std::max(1, cross / MIN_ROOM);

    local = trimRowRooms(std::move(local), capacity);

    if (local.empty()) {
        return;
    }

    // Preserve a useful ordering: bathroom/service rooms toward the kitchen
    // side, bedrooms in the remaining larger pieces, plus a little variation.
    reorderPrivateRooms(local, rng);

    packAcross(out, region, local, frontAlongY);
}

void BuildingPlan::reorderPrivateRooms(std::vector<std::string>& rooms, JavaRandom& rng) {
    if (rooms.size() <= 1) {
        return;
    }

    // Find a bathroom and move it near the beginning.
    const int bath = indexOf(rooms, "bathroom");
    if (bath > 0) {
        std::string value = rooms[static_cast<std::size_t>(bath)];
        rooms.erase(rooms.begin() + bath);
        // Java: rooms.add(Math.min(1, rooms.size()), value) — index computed
        // AFTER the removal.
        const std::size_t at = std::min<std::size_t>(1, rooms.size());
        rooms.insert(rooms.begin() + static_cast<std::ptrdiff_t>(at), std::move(value));
    }

    // Occasionally reverse the remaining orientation so houses do not all
    // read identically.
    if (rng.nextDouble() < 0.35) {
        std::reverse(rooms.begin(), rooms.end());
    }
}

void BuildingPlan::packAcross(std::vector<Room>& out, const Rect& region,
                              std::vector<std::string> rooms, bool splitAlongX) {
    if (rooms.empty()) {
        return;
    }

    const int length = splitAlongX ? region.w : region.h;
    int n = static_cast<int>(rooms.size());

    if (length < n * MIN_ROOM) {
        rooms = trimRowRooms(rooms, std::max(1, length / MIN_ROOM));
        n = static_cast<int>(rooms.size());
    }

    if (n == 0) {
        return;
    }

    const std::vector<int> sizes = allocateWeightedSizes(rooms, length, MIN_ROOM);

    int at = 0;
    for (int i = 0; i < n; ++i) {
        const int size = sizes[static_cast<std::size_t>(i)];
        if (splitAlongX) {
            out.emplace_back(rooms[static_cast<std::size_t>(i)], region.x + at, region.y,
                             size, region.h, false);
        } else {
            out.emplace_back(rooms[static_cast<std::size_t>(i)], region.x, region.y + at,
                             region.w, size, false);
        }
        at += size;
    }
}

std::vector<int> BuildingPlan::allocateWeightedSizes(const std::vector<std::string>& rooms,
                                                     int length, int minimum) {
    int n = static_cast<int>(rooms.size());
    std::vector<int> sizes(static_cast<std::size_t>(n), 0);

    int minimumUsed = n * minimum;

    if (minimumUsed > length) {
        const int capacity = std::max(1, length / std::max(1, minimum));
        n = std::min(n, capacity);
        sizes.assign(static_cast<std::size_t>(n), 0);
        minimumUsed = n * minimum;
    }

    const int extra = std::max(0, length - minimumUsed);

    // Java: weightOf(rooms.subList(0, n)) — the possibly-shortened prefix.
    const std::vector<std::string> prefix(rooms.begin(),
                                          rooms.begin() + static_cast<std::ptrdiff_t>(n));
    const double total = weightOf(prefix);

    int distributed = 0;

    for (int i = 0; i < n; ++i) {
        int add;
        if (i == n - 1) {
            add = extra - distributed;
        } else {
            // Java line 1309: Math.round.
            add = static_cast<int>(
                javaRound(extra * weightFor(rooms[static_cast<std::size_t>(i)]) / total));
            add = std::max(0, add);
        }
        sizes[static_cast<std::size_t>(i)] = minimum + add;
        distributed += add;
    }

    int actual = 0;
    for (const int size : sizes) actual += size;

    if (n > 0) {
        sizes[static_cast<std::size_t>(n - 1)] += length - actual;
    }

    return sizes;
}

// ---------------------------------------------------------------------
// HALL LAYOUT FOR LARGE HOUSES
//
// The hall becomes a spine only when the house has enough scale to justify
// spending floor area on circulation.
// ---------------------------------------------------------------------

std::vector<BuildingPlan::Room> BuildingPlan::hubHallLayout(
    std::vector<Room>& out, int x, int y, int w, int h, Facing facing, bool hasKitchen,
    const std::vector<std::string>& secondary, JavaRandom& rng) {

    const bool frontAlongY = facing == Facing::NORTH || facing == Facing::SOUTH;
    const int depth = frontAlongY ? h : w;
    const int cross = frontAlongY ? w : h;

    // Java line 1383: Math.round.
    int hallCross =
        clamp(static_cast<int>(javaRound(cross * 0.30)), HALL_MIN, HALL_MAX);
    hallCross = std::min(hallCross, cross - MIN_ROOM * 2);

    if (hallCross < HALL_MIN) {
        return hubNoHallLayout(out, x, y, w, h, facing, hasKitchen, secondary, rng);
    }

    const int leftCross = (cross - hallCross) / 2;
    const int rightCross = cross - hallCross - leftCross;

    if (leftCross < MIN_ROOM || rightCross < MIN_ROOM) {
        return hubNoHallLayout(out, x, y, w, h, facing, hasKitchen, secondary, rng);
    }

    const Rect hall = hallRect(x, y, w, h, facing, hallCross, leftCross);

    // Hall touches the front exterior.
    out.emplace_back("hall", hall.x, hall.y, hall.w, hall.h, true);

    Rect sideA;
    Rect sideB;

    if (frontAlongY) {
        sideA = Rect(x, y, leftCross, h);
        sideB = Rect(x + leftCross + hallCross, y, rightCross, h);
    } else {
        sideA = Rect(x, y, w, leftCross);
        sideB = Rect(x, y + leftCross + hallCross, w, rightCross);
    }

    // Livingroom is front of side A.
    std::vector<std::string> sideARooms;
    sideARooms.push_back("livingroom");

    std::vector<std::string> privateRooms(secondary);

    // Kitchen is front of side B.
    std::vector<std::string> sideBRooms;
    if (hasKitchen) {
        sideBRooms.push_back("kitchen");
    }

    // Distribute bedrooms between the two wings.
    const int desiredA = std::max(1, 1 + (countBedrooms(privateRooms) + 1) / 2);

    // Java's loop header carries NO increment; i advances only in the else
    // branch, because a removal already shifts the next element into place.
    for (int i = 0; i < static_cast<int>(privateRooms.size()) &&
                    static_cast<int>(sideARooms.size()) < desiredA;) {
        const std::string type = privateRooms[static_cast<std::size_t>(i)];
        if (isBedroom(type)) {
            sideARooms.push_back(type);
            privateRooms.erase(privateRooms.begin() + i);
        } else {
            ++i;
        }
    }

    sideBRooms.insert(sideBRooms.end(), privateRooms.begin(), privateRooms.end());

    // Ensure both wings remain physically packable. These MUTATE in place.
    trimSideRoomCount(sideARooms, depth);
    trimSideRoomCount(sideBRooms, depth);

    packSide(out, sideA, sideARooms, facing, true, rng);
    packSide(out, sideB, sideBRooms, facing, false, rng);

    return out;
}

// ---------------------------------------------------------------------
// Very narrow dwelling fallback. Preserves the hub sequence without
// creating a corridor.
// ---------------------------------------------------------------------

std::vector<BuildingPlan::Room> BuildingPlan::hubRowLayout(
    std::vector<Room>& out, int x, int y, int w, int h, Facing facing, bool hasKitchen,
    const std::vector<std::string>& secondary, JavaRandom& rng) {

    const bool alongX = w >= h;
    const int longSide = alongX ? w : h;

    std::vector<std::string> rooms;
    rooms.push_back("livingroom");
    if (hasKitchen) {
        rooms.push_back("kitchen");
    }
    rooms.insert(rooms.end(), secondary.begin(), secondary.end());

    const int capacity = std::max(1, longSide / MIN_ROOM);
    rooms = trimRowRooms(std::move(rooms), capacity);

    const bool reverse = (alongX && facing == Facing::EAST) ||
                         (!alongX && facing == Facing::SOUTH);

    if (reverse) {
        std::reverse(rooms.begin(), rooms.end());
    }

    packRow(out, x, y, w, h, rooms, alongX, rng);

    return out;
}

// ---------------------------------------------------------------------
// HALL SIDE PACKING
// ---------------------------------------------------------------------

void BuildingPlan::packSide(std::vector<Room>& out, const Rect& side,
                            std::vector<std::string> rooms, Facing facing,
                            bool livingSide, JavaRandom& rng) {
    (void)livingSide;  // Java accepts and ignores it too.

    if (rooms.empty()) {
        return;
    }

    const bool alongY = facing == Facing::NORTH || facing == Facing::SOUTH;
    const int length = alongY ? side.h : side.w;

    if (length < static_cast<int>(rooms.size()) * MIN_ROOM) {
        rooms = trimRowRooms(rooms, std::max(1, length / MIN_ROOM));
    }

    std::vector<std::string> ordered(rooms);

    // Put the first logical room on the road-facing end.
    const bool reverse = facing == Facing::SOUTH || facing == Facing::EAST;
    if (reverse) {
        std::reverse(ordered.begin(), ordered.end());
    }

    packLinear(out, side, ordered, alongY, true, rng);
}

void BuildingPlan::packLinear(std::vector<Room>& out, const Rect& region,
                              std::vector<std::string> rooms, bool alongY,
                              bool firstRoomMayTakeDoor, JavaRandom& rng) {
    (void)rng;  // Java takes it and never draws; kept so the signature matches.

    if (rooms.empty() || region.w <= 0 || region.h <= 0) {
        return;
    }

    const int length = alongY ? region.h : region.w;

    if (length < static_cast<int>(rooms.size()) * MIN_ROOM) {
        rooms = trimRowRooms(rooms, std::max(1, length / MIN_ROOM));
    }

    if (rooms.empty()) {
        return;
    }

    const std::vector<int> sizes = allocateWeightedSizes(rooms, length, MIN_ROOM);

    int at = 0;
    for (std::size_t i = 0; i < rooms.size(); ++i) {
        const std::string& type = rooms[i];
        const int size = sizes[i];

        const bool entrance = firstRoomMayTakeDoor && i == 0 && isEntranceType(type);

        if (alongY) {
            out.emplace_back(type, region.x, region.y + at, region.w, size, entrance);
        } else {
            out.emplace_back(type, region.x + at, region.y, size, region.h, entrance);
        }

        at += size;
    }
}

void BuildingPlan::packRow(std::vector<Room>& out, int x, int y, int w, int h,
                           std::vector<std::string> rooms, bool alongX, JavaRandom& rng) {
    if (rooms.empty()) {
        return;
    }
    const Rect region(x, y, w, h);
    packLinear(out, region, std::move(rooms), !alongX, true, rng);
}

// ---------------------------------------------------------------------
// RECTANGLE HELPERS
// ---------------------------------------------------------------------

BuildingPlan::Rect BuildingPlan::frontRect(int x, int y, int w, int h, Facing facing,
                                           int depth) {
    switch (facing) {
        case Facing::NORTH: return Rect(x, y, w, depth);
        case Facing::SOUTH: return Rect(x, y + h - depth, w, depth);
        case Facing::WEST:  return Rect(x, y, depth, h);
        case Facing::EAST:  return Rect(x + w - depth, y, depth, h);
    }
    return Rect();
}

BuildingPlan::Rect BuildingPlan::backRect(int x, int y, int w, int h, Facing facing,
                                          int frontDepth) {
    switch (facing) {
        case Facing::NORTH: return Rect(x, y + frontDepth, w, h - frontDepth);
        case Facing::SOUTH: return Rect(x, y, w, h - frontDepth);
        case Facing::WEST:  return Rect(x + frontDepth, y, w - frontDepth, h);
        case Facing::EAST:  return Rect(x, y, w - frontDepth, h);
    }
    return Rect();
}

BuildingPlan::Rect BuildingPlan::hallRect(int x, int y, int w, int h, Facing facing,
                                          int hallCross, int leftCross) {
    const bool alongY = facing == Facing::NORTH || facing == Facing::SOUTH;
    if (alongY) {
        return Rect(x + leftCross, y, hallCross, h);
    }
    return Rect(x, y + leftCross, w, hallCross);
}

// ---------------------------------------------------------------------
// ROOM LIST MANAGEMENT
// ---------------------------------------------------------------------

int BuildingPlan::countBedrooms(const std::vector<std::string>& rooms) {
    int n = 0;
    for (const std::string& type : rooms) {
        if (isBedroom(type)) ++n;
    }
    return n;
}

bool BuildingPlan::isBedroom(const std::string& type) {
    return type == "bedroom" || type == "kidsbedroom";
}

int BuildingPlan::findBedroomIndex(const std::vector<std::string>& rooms) {
    for (std::size_t i = 0; i < rooms.size(); ++i) {
        if (isBedroom(rooms[i])) return static_cast<int>(i);
    }
    return -1;
}

bool BuildingPlan::sideAHasRoom(const std::vector<std::string>& rooms, int depth) {
    return static_cast<int>(rooms.size()) * MIN_ROOM < depth;
}

bool BuildingPlan::roomCountNeedsMoreCapacity(const std::vector<std::string>& rooms,
                                              int depth) {
    return static_cast<int>(rooms.size()) * MIN_ROOM > depth;
}

void BuildingPlan::trimSideRoomCount(std::vector<std::string>& rooms, int depth) {
    const int capacity = std::max(1, depth / MIN_ROOM);

    while (static_cast<int>(rooms.size()) > capacity) {
        int remove = findOptionalRoomFromEnd(rooms);
        if (remove < 0) {
            remove = static_cast<int>(rooms.size()) - 1;
        }
        rooms.erase(rooms.begin() + remove);
    }
}

std::vector<std::string> BuildingPlan::trimDwellingRooms(
    int w, int h, const std::vector<std::string>& secondary) {

    std::vector<std::string> rooms(secondary);

    const int capacity = std::max(1, (w / MIN_ROOM) * (h / MIN_ROOM));
    const int secondaryCapacity = std::max(0, capacity - 4);

    while (static_cast<int>(rooms.size()) > secondaryCapacity) {
        int remove = findOptionalRoomFromEnd(rooms);
        if (remove < 0) {
            remove = static_cast<int>(rooms.size()) - 1;
        }
        rooms.erase(rooms.begin() + remove);
    }

    return rooms;
}

int BuildingPlan::findOptionalRoomFromEnd(const std::vector<std::string>& rooms) {
    for (int i = static_cast<int>(rooms.size()) - 1; i >= 0; --i) {
        const std::string& type = rooms[static_cast<std::size_t>(i)];
        if (type == "closet" || type == "laundry" || type == "garage" ||
            type == "diningroom" || type == "office" || type == "janitor") {
            return i;
        }
    }
    return -1;
}

std::vector<std::string> BuildingPlan::trimRowRooms(std::vector<std::string> rooms,
                                                    int capacity) {
    if (capacity <= 0) {
        return {};
    }

    while (static_cast<int>(rooms.size()) > capacity) {
        int remove = findOptionalRoomFromEnd(rooms);
        if (remove < 0) {
            remove = static_cast<int>(rooms.size()) - 1;
        }
        rooms.erase(rooms.begin() + remove);
    }

    return rooms;
}

std::vector<std::string> BuildingPlan::trimToCapacity(const std::vector<std::string>& rooms,
                                                      int w, int h) {
    const int capacity = std::max(1, (w / MIN_ROOM) * (h / MIN_ROOM));
    return trimRowRooms(rooms, capacity);
}

// ---------------------------------------------------------------------
// GENERIC RECURSIVE SPLITTER  (non-dwellings only)
// ---------------------------------------------------------------------

void BuildingPlan::split(std::vector<Room>& out, int x, int y, int w, int h,
                         std::vector<std::string> rooms, JavaRandom& rng) {
    if (rooms.empty() || w <= 0 || h <= 0) {
        return;
    }

    if (rooms.size() == 1) {
        out.emplace_back(rooms[0], x, y, w, h);
        return;
    }

    const int capacity = std::max(1, (w / MIN_ROOM) * (h / MIN_ROOM));

    std::vector<std::string> local(rooms);
    while (static_cast<int>(local.size()) > capacity) {
        local.pop_back();
    }

    if (local.size() == 1) {
        out.emplace_back(local[0], x, y, w, h);
        return;
    }

    const std::size_t half = local.size() / 2;

    std::vector<std::string> a(local.begin(),
                               local.begin() + static_cast<std::ptrdiff_t>(half));
    std::vector<std::string> b(local.begin() + static_cast<std::ptrdiff_t>(half),
                               local.end());

    const double wa = weightOf(a);
    const double wb = weightOf(b);
    const double frac = wa / (wa + wb);

    // Java line 2317: Math.round.
    int vCut = static_cast<int>(javaRound(w * frac));
    const bool vOk = w >= MIN_ROOM * 2 && vCut >= MIN_ROOM && w - vCut >= MIN_ROOM;

    // Java line 2327: Math.round.
    int hCut = static_cast<int>(javaRound(h * frac));
    const bool hOk = h >= MIN_ROOM * 2 && hCut >= MIN_ROOM && h - hCut >= MIN_ROOM;

    if (!vOk && !hOk) {
        out.emplace_back(local[0], x, y, w, h);
        return;
    }

    if (vOk) {
        vCut = clamp(vCut, MIN_ROOM, w - MIN_ROOM);
    }
    if (hOk) {
        hCut = clamp(hCut, MIN_ROOM, h - MIN_ROOM);
    }

    bool vertical;
    if (vOk && hOk) {
        const double vScore = std::max(aspect(vCut, h), aspect(w - vCut, h));
        const double hScore = std::max(aspect(w, hCut), aspect(w, h - hCut));
        vertical = vScore <= hScore;
    } else {
        vertical = vOk;
    }

    if (vertical) {
        split(out, x, y, vCut, h, a, rng);
        split(out, x + vCut, y, w - vCut, h, b, rng);
    } else {
        split(out, x, y, w, hCut, a, rng);
        split(out, x, y + hCut, w, h - hCut, b, rng);
    }
}

// ---------------------------------------------------------------------
// MATH / GEOMETRY
// ---------------------------------------------------------------------

int BuildingPlan::crossAxis(int w, int h, Facing facing) {
    switch (facing) {
        case Facing::NORTH:
        case Facing::SOUTH: return w;
        case Facing::EAST:
        case Facing::WEST:  return h;
    }
    return w;
}

int BuildingPlan::depthAxis(int w, int h, Facing facing) {
    switch (facing) {
        case Facing::NORTH:
        case Facing::SOUTH: return h;
        case Facing::EAST:
        case Facing::WEST:  return w;
    }
    return h;
}

int BuildingPlan::clamp(int v, int lo, int hi) {
    if (hi < lo) {
        return lo;
    }
    return std::max(lo, std::min(hi, v));
}

double BuildingPlan::aspect(int w, int h) {
    const int shortSide = std::max(1, std::min(w, h));
    const int longSide = std::max(w, h);
    return longSide / static_cast<double>(shortSide);
}

double BuildingPlan::weightOf(const std::vector<std::string>& rooms) {
    double sum = 0.0;
    for (const std::string& room : rooms) {
        sum += weightFor(room);
    }
    return sum <= 0.0 ? 1.0 : sum;
}

double BuildingPlan::weightSum(const std::vector<std::string>& rooms) {
    double sum = 0.0;
    for (const std::string& room : rooms) {
        sum += weightFor(room);
    }
    return sum;
}

int BuildingPlan::ceilDiv(int a, int b) {
    return (a + b - 1) / std::max(1, b);
}

int BuildingPlan::minRooms(int w, int h) {
    const int longSide = std::max(w, h);
    const int shortSide = std::max(1, std::min(w, h));
    const int n = static_cast<int>(std::ceil(longSide / (shortSide * ROOM_MAX_ASPECT)));
    return std::max(1, n);
}

}  // namespace pzformat

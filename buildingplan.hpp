// buildingplan.hpp — port of pzformat/BuildingPlan.java (3,347 lines).
//
// Lays out the inside of a building:
//     footprint + occupancy + room recipe -> typed room rectangles
// Pure. No map, no I/O. Zero pzformat dependencies; needs only JavaRandom.
//
// The Java doc comment is preserved in the .cpp because it is the
// justification for the grammar, not decoration.
//
// PORTING HAZARDS IN THIS UNIT. Each of these passes a casual eyeball and
// each would silently desynchronise the oracle:
//
//  1. java.lang.Math.round(x) is floor(x + 0.5), NOT std::round, which is
//     round-half-away-from-zero. Six sites (Java lines 798, 860, 1309, 1383,
//     2317, 2327). Reused from footprintsnap.hpp as javaRound.
//
//  2. ARGUMENT EVALUATION ORDER. Java specifies left-to-right evaluation of
//     method arguments (JLS 15.12.4.2). recipe() contains a nested pick:
//         pick(rng, 0.52, "garagestorage", pick(rng, 0.50, "empty", "shed"))
//     so the INNER pick draws from the RNG first, then the outer one. C++
//     leaves the order of function arguments indeterminately sequenced even
//     in C++20, so the compiler is free to draw them the other way round and
//     every subsequent draw in that generator is shifted. The nested call is
//     hoisted into a named temporary below. This is NOT in STATE's scan.
//
//  3. WEIGHT is a LinkedHashMap in Java, but it is only ever read through
//     getOrDefault — weightOf, weightSum and allocateWeightedSizes all
//     iterate the ROOM LIST and look WEIGHT up by key. Nothing iterates the
//     map. Insertion order is therefore NOT a behavioural contract here,
//     contrary to STATE §39 and PROMPT_BUILDINGPLAN. An insertion-ordered
//     container is used anyway, because it costs nothing and the claim
//     becomes true again the moment someone adds an iteration.
//
//  4. ENTRANCE is java.util.Set.of(...), whose iteration order is randomised
//     per JVM run in Java 9+. Confirmed used only via contains() (Java lines
//     108, 479, 537, 1795). A sorted array is used here. If a future edit
//     iterates the Java set, Java itself becomes nondeterministic and no
//     oracle can ever clear.
//
//  5. Java's %.1f rounds HALF_UP on the exact decimal value of the double;
//     C's printf rounds half-to-even. aspect() returns a ratio of small
//     integers, so exact ties are reachable — aspect(13,4) is exactly 3.25,
//     which Java prints as 3.3 and glibc prints as 3.2. javaFormat1f below
//     reproduces Java. This only matters for the self-test's report line,
//     but the self-test IS the primary oracle.
//
//  6. trimSideRoomCount and reorderPrivateRooms MUTATE their argument list
//     in place (Java passes the reference); every other trim* takes a
//     defensive copy at the call site. The by-reference / by-value split
//     below mirrors that exactly.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "footprintsnap.hpp"  // javaRound
#include "java_random.hpp"

namespace pzformat {

class BuildingPlan {
public:
    // -----------------------------------------------------------------
    // ROOM
    // -----------------------------------------------------------------

    /// A planned room.
    ///
    /// entrance is a writer hint: the room touches an exterior face and may
    /// receive an exterior door.
    struct Room {
        std::string type;
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        bool entrance = false;

        Room() = default;
        Room(std::string t, int rx, int ry, int rw, int rh, bool ent)
            : type(std::move(t)), x(rx), y(ry), w(rw), h(rh), entrance(ent) {}
        /// Java's 5-arg canonical-record shortcut: entrance defaults false.
        Room(std::string t, int rx, int ry, int rw, int rh)
            : type(std::move(t)), x(rx), y(ry), w(rw), h(rh), entrance(false) {}

        int area() const { return w * h; }
        bool canTakeDoor() const { return isEntranceType(type); }

        /// Java Room.toString(): type[x,y wxh] with a trailing * when entrance.
        std::string toString() const;
    };

    // -----------------------------------------------------------------
    // FACING
    // -----------------------------------------------------------------

    /// Which side of the footprint faces the road/front.
    enum class Facing { NORTH, SOUTH, EAST, WEST };

    static Facing opposite(Facing f);
    static const char* facingName(Facing f);
    /// Java's Facing.values() order, which the self-test iterates.
    static const std::vector<Facing>& facingValues();

    // -----------------------------------------------------------------
    // CONSTANTS  (Java lines 155-248)
    // -----------------------------------------------------------------

    static constexpr double WEIGHT_DEFAULT = 15.0;

    /// Probability that a livingroom/kitchen boundary can be opened.
    static constexpr double LK_OPEN = 0.88;

    static constexpr int MIN_ROOM = 3;
    static constexpr int MIN_BEDROOM = 5;
    static constexpr int MIN_CLOSET = 2;
    static constexpr int MIN_LIVING_SIDE = 4;
    static constexpr int MIN_KITCHEN_SIDE = 3;
    static constexpr int HALL_MIN = 4;
    static constexpr int HALL_MAX = 7;
    static constexpr double ROOM_MAX_ASPECT = 4.0;
    static constexpr int A_LIVING = 32;
    static constexpr int A_KITCHEN = 21;
    static constexpr int A_BED = 14;
    static constexpr int A_BATH = 6;
    static constexpr int BEDROOM_MAX = 8;
    static constexpr int SECOND_BATH_AREA = 240;

    /// A hall is a feature of a genuinely large dwelling. Deliberately much
    /// higher than the old 120/200 thresholds: a 20x15 = 300 house gets none.
    static constexpr int LARGE_HOUSE_AREA = 420;
    /// ...and needs enough private rooms for the hall to make sense.
    static constexpr int LARGE_HOUSE_BEDROOMS = 3;

    /// WEIGHT lookup. Insertion-ordered; see hazard 3 above.
    static double weightFor(const std::string& room);
    /// Rooms allowed an exterior door. Bedrooms deliberately excluded.
    static bool isEntranceType(const std::string& type);

    // -----------------------------------------------------------------
    // PUBLIC API
    // -----------------------------------------------------------------

    /// True when the livingroom/kitchen boundary may be opened.
    static bool openBetween(const std::string& a, const std::string& b, JavaRandom& rng);

    /// Legacy-compatible hall probability. The dwelling planner no longer
    /// uses this as the primary architectural decision; kept because other
    /// code may call it.
    static double hallChance(int rooms);

    /// Build the room recipe before geometry is applied.
    static std::vector<std::string> recipe(int area, const std::string& occ,
                                           bool outbuilding, JavaRandom& rng);

    static std::string pick(JavaRandom& rng, double p, const std::string& a,
                            const std::string& b);

    /// Main layout entry point. Dwellings use the hub grammar; non-dwellings
    /// use recursive splitting.
    static std::vector<Room> plan(int x, int y, int w, int h,
                                  const std::vector<std::string>& types,
                                  Facing facing, JavaRandom& rng);

    /// Backwards-compatible entry point. SOUTH remains the default facing.
    static std::vector<Room> plan(int x, int y, int w, int h,
                                  const std::vector<std::string>& types,
                                  JavaRandom& rng);

    // -----------------------------------------------------------------
    // INTERNALS — exposed because the oracle digests them individually.
    // Porting a 3,347-line unit as one black box means a divergence at the
    // end tells you nothing about where it came from (PROMPT step 3 lesson).
    // -----------------------------------------------------------------

    struct Rect {
        int x = 0, y = 0, w = 0, h = 0;
        Rect() = default;
        Rect(int rx, int ry, int rw, int rh) : x(rx), y(ry), w(rw), h(rh) {}
        int area() const { return w * h; }
    };

    static std::vector<Room> hubLayout(std::vector<Room>& out, int x, int y, int w, int h,
                                       Facing facing, bool hasKitchen,
                                       const std::vector<std::string>& secondary,
                                       bool requestedHall, JavaRandom& rng);

    static std::vector<Room> hubNoHallLayout(std::vector<Room>& out, int x, int y, int w,
                                             int h, Facing facing, bool hasKitchen,
                                             const std::vector<std::string>& secondary,
                                             JavaRandom& rng);

    static std::vector<Room> hubHallLayout(std::vector<Room>& out, int x, int y, int w,
                                           int h, Facing facing, bool hasKitchen,
                                           const std::vector<std::string>& secondary,
                                           JavaRandom& rng);

    static std::vector<Room> hubRowLayout(std::vector<Room>& out, int x, int y, int w,
                                          int h, Facing facing, bool hasKitchen,
                                          const std::vector<std::string>& secondary,
                                          JavaRandom& rng);

    static void packPrivateZone(std::vector<Room>& out, const Rect& region,
                                const std::vector<std::string>& rooms, bool frontAlongY,
                                JavaRandom& rng);

    /// MUTATES rooms in place, as Java does.
    static void reorderPrivateRooms(std::vector<std::string>& rooms, JavaRandom& rng);

    static void packAcross(std::vector<Room>& out, const Rect& region,
                           std::vector<std::string> rooms, bool splitAlongX);

    static std::vector<int> allocateWeightedSizes(const std::vector<std::string>& rooms,
                                                  int length, int minimum);

    static void packSide(std::vector<Room>& out, const Rect& side,
                         std::vector<std::string> rooms, Facing facing, bool livingSide,
                         JavaRandom& rng);

    static void packLinear(std::vector<Room>& out, const Rect& region,
                           std::vector<std::string> rooms, bool alongY,
                           bool firstRoomMayTakeDoor, JavaRandom& rng);

    static void packRow(std::vector<Room>& out, int x, int y, int w, int h,
                        std::vector<std::string> rooms, bool alongX, JavaRandom& rng);

    static Rect frontRect(int x, int y, int w, int h, Facing facing, int depth);
    static Rect backRect(int x, int y, int w, int h, Facing facing, int frontDepth);
    static Rect hallRect(int x, int y, int w, int h, Facing facing, int hallCross,
                         int leftCross);

    static int countBedrooms(const std::vector<std::string>& rooms);
    static bool isBedroom(const std::string& type);
    static int findBedroomIndex(const std::vector<std::string>& rooms);
    static bool sideAHasRoom(const std::vector<std::string>& rooms, int depth);
    static bool roomCountNeedsMoreCapacity(const std::vector<std::string>& rooms, int depth);

    /// MUTATES rooms in place, as Java does.
    static void trimSideRoomCount(std::vector<std::string>& rooms, int depth);

    static std::vector<std::string> trimDwellingRooms(int w, int h,
                                                      const std::vector<std::string>& secondary);
    static int findOptionalRoomFromEnd(const std::vector<std::string>& rooms);
    /// Java mutates AND returns the same list; every call site passes a copy.
    static std::vector<std::string> trimRowRooms(std::vector<std::string> rooms, int capacity);
    static std::vector<std::string> trimToCapacity(const std::vector<std::string>& rooms,
                                                   int w, int h);

    /// Generic weighted recursive splitter, used by non-dwellings only.
    /// The dwelling hub is NEVER produced by this method.
    static void split(std::vector<Room>& out, int x, int y, int w, int h,
                      std::vector<std::string> rooms, JavaRandom& rng);

    static int crossAxis(int w, int h, Facing facing);
    static int depthAxis(int w, int h, Facing facing);
    static int clamp(int v, int lo, int hi);
    static double aspect(int w, int h);
    static double weightOf(const std::vector<std::string>& rooms);
    static double weightSum(const std::vector<std::string>& rooms);
    static int ceilDiv(int a, int b);
    static int minRooms(int w, int h);

    // -----------------------------------------------------------------
    // SELF TEST  (Java main, line 2614) + its footprint check (line 3245)
    // -----------------------------------------------------------------

    /// Exact footprint tiling check. Prints one line, returns 1 on failure.
    static int check(const std::string& label, int w, int h, const std::vector<Room>& rooms);

    /// The ported self-test. Returns the Java process exit code: 0 or 1.
    static int selfTest();

    /// java.util.AbstractCollection.toString over a room list.
    static std::string roomsToString(const std::vector<Room>& rooms);

    /// Java's %.1f — HALF_UP on the exact decimal value. See hazard 5.
    static std::string javaFormat1f(double v);
};

}  // namespace pzformat

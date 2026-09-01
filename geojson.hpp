// geojson.hpp — port of pzformat/GeoJson.java
//
// Minimal GeoJSON reader: polygons and linestrings with properties. Handles the
// Esri-flavoured GeoJSON that ArcGIS feature services emit, and the OSM/Overpass
// output the fetch script writes for non-US boxes.
//
// Contract details the oracle checks and a rewrite must not drift on:
//
//  * Features are kept in FILE ORDER. GisImport.buildings is documented as
//    "in import order" and downstream seeding indexes into it, so sorting here
//    would silently change generated output.
//  * props preserves JSON object order (Java LinkedHashMap).
//  * A feature with no rings is DROPPED, not kept empty.
//  * collectRings descends until it finds an array whose first element is an
//    array of >= 2 numbers. That one predicate is what makes Polygon,
//    MultiPolygon and LineString all work without branching on the type string,
//    and it means `type` is carried but never dispatched on.

#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "json.hpp"

namespace pzformat {

class GeoJson {
public:
    struct Point {
        double lon = 0.0;
        double lat = 0.0;
    };

    using Ring = std::vector<Point>;

    struct Feature {
        std::string type;  // Polygon, MultiPolygon, LineString, ...
        std::vector<Ring> rings;
        // Insertion-ordered, matching Java's LinkedHashMap.
        std::vector<std::pair<std::string, std::string>> props;

        /// Java: props.get(k) — returns empty and reports absence separately,
        /// since C++ has no null String. Callers that need "was it present"
        /// use hasProp.
        std::string prop(std::string_view k) const {
            for (const auto& kv : props)
                if (kv.first == k) return kv.second;
            return {};
        }

        bool hasProp(std::string_view k) const {
            for (const auto& kv : props)
                if (kv.first == k) return true;
            return false;
        }
    };

    std::vector<Feature> features;

    /// Mirrors GeoJson.read(Path).
    static GeoJson read(const std::string& file);

    /// Parse from text already in memory.
    static GeoJson parse(std::string_view text);

private:
    static void collectRings(const Json::ValuePtr& node, std::vector<Ring>& out);
    static bool isPairArray(const Json::ValuePtr& node);
};

}  // namespace pzformat

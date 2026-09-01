// json.hpp — port of pzformat/Json.java
//
// Tiny JSON parser. Dependency-free: C++20 standard library only (Charter §3).
//
// Ported behaviours that are NOT obvious and must not be "improved":
//
//  * Object key order is INSERTION order, matching Java's LinkedHashMap.
//    GeoJson.props relies on it, so a std::map or unordered_map would silently
//    reorder every property digest.
//  * asText() has two numeric branches. An integral double formats through
//    (long), a non-integral one through Java's Double.toString. These are
//    different algorithms and C++ has neither by default.
//  * The parser is deliberately trusting, exactly like the Java one: it does no
//    validation and will walk off the end of malformed input. Reproduced as-is;
//    adding error handling here would be a behaviour change the oracle cannot
//    see, and the callers already assume well-formed feature-service output.

#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pzformat {

/// Java's String.valueOf(double) / Double.toString.
///
/// Not the same as std::to_string (which is "%f", six decimals) and not the
/// same as std::to_chars shortest (which picks its own exponent threshold).
/// Java's contract: shortest decimal that round-trips, rendered plainly when
/// 1e-3 <= |d| < 1e7 and in scientific notation otherwise, always with at
/// least one digit on each side of the point.
std::string javaDoubleToString(double d);

/// Java's String.valueOf(long).
std::string javaLongToString(long long v);

class Json {
public:
    struct Value;
    using ValuePtr = std::shared_ptr<Value>;

    struct Value {
        // Insertion-ordered key/value pairs. A vector, not a map, because the
        // order is part of the contract (see header comment).
        std::vector<std::pair<std::string, ValuePtr>> object;
        std::vector<ValuePtr> array;

        std::string str;
        double num = 0.0;

        bool isObject = false;
        bool isArray  = false;
        bool isStr    = false;
        bool isNum    = false;
        bool isBool   = false;
        bool boolVal  = false;
        bool isNull   = false;

        /// Java: object == null ? null : object.get(key)
        ValuePtr get(std::string_view key) const {
            if (!isObject) return nullptr;
            for (const auto& kv : object)
                if (kv.first == key) return kv.second;
            return nullptr;
        }

        /// Java Json.Value.asText(), branch for branch.
        std::string asText() const {
            if (isStr) return str;
            if (isNum) {
                // Java: num == Math.floor(num) && !Double.isInfinite(num)
                if (num == std::floor(num) && !std::isinf(num))
                    return javaLongToString(static_cast<long long>(num));
                return javaDoubleToString(num);
            }
            if (isBool) return boolVal ? "true" : "false";
            return "";
        }
    };

    /// Parse UTF-8 text. Mirrors Json.parse(String).
    static ValuePtr parse(std::string_view text);

private:
    explicit Json(std::string_view s) : s_(s) {}

    std::string_view s_;
    std::size_t p_ = 0;

    void ws();
    ValuePtr value();
    ValuePtr object();
    ValuePtr array();
    ValuePtr boolean(bool b);
    std::string string();
    ValuePtr number();
};

}  // namespace pzformat

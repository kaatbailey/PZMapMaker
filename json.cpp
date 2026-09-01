#include "json.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace pzformat {

std::string javaLongToString(long long v) {
    // std::to_chars on an integer is exactly Java's decimal rendering,
    // including the leading '-'. No locale, no separators.
    std::array<char, 32> buf{};
    auto [end, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), v);
    return std::string(buf.data(), end);
}

std::string javaDoubleToString(double d) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d < 0 ? "-Infinity" : "Infinity";
    if (d == 0.0) return std::signbit(d) ? "-0.0" : "0.0";

    const bool neg = d < 0;
    const double a = std::fabs(d);

    // Shortest round-tripping digits, in scientific form, so the digit string
    // and decimal exponent can be pulled apart and re-rendered Java's way.
    std::array<char, 64> buf{};
    auto [end, ec] =
        std::to_chars(buf.data(), buf.data() + buf.size(), a, std::chars_format::scientific);
    std::string sci(buf.data(), end);

    const std::size_t epos = sci.find('e');
    std::string mant = sci.substr(0, epos);
    const int exp10 = std::atoi(sci.c_str() + epos + 1);

    // Digit string with the point removed: d1 d2 ... dn, value = d1.d2..dn * 10^exp10
    std::string digits;
    for (char c : mant)
        if (c != '.') digits.push_back(c);
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();

    std::string out;
    if (a >= 1e-3 && a < 1e7) {
        // Plain decimal, always with at least one digit either side of the point.
        if (exp10 >= 0) {
            const int intDigits = exp10 + 1;
            if (static_cast<int>(digits.size()) <= intDigits) {
                out = digits;
                out.append(intDigits - digits.size(), '0');
                out += ".0";
            } else {
                out = digits.substr(0, intDigits) + "." + digits.substr(intDigits);
            }
        } else {
            out = "0.";
            out.append(-exp10 - 1, '0');
            out += digits;
        }
    } else {
        // Java's "computerized scientific notation": d1.d2..dn E exp
        out = digits.substr(0, 1) + ".";
        out += digits.size() > 1 ? digits.substr(1) : "0";
        out += "E" + javaLongToString(exp10);
    }
    return neg ? "-" + out : out;
}

// Java's Character.isWhitespace, restricted to what can appear between JSON
// tokens. Java also treats 0x1C-0x1F as whitespace, which C's isspace does not;
// included so the two parsers agree on pathological input.
static bool javaIsWhitespace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r' ||
           (c >= 0x1C && c <= 0x1F);
}

/// Append one Unicode code point as UTF-8.
static void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

void Json::ws() {
    while (p_ < s_.size() && javaIsWhitespace(static_cast<unsigned char>(s_[p_]))) p_++;
}

Json::ValuePtr Json::parse(std::string_view text) {
    Json j(text);
    j.ws();
    return j.value();
}

Json::ValuePtr Json::value() {
    ws();
    const char c = s_[p_];
    switch (c) {
        case '{': return object();
        case '[': return array();
        case '"': {
            auto v = std::make_shared<Value>();
            v->isStr = true;
            v->str = string();
            return v;
        }
        case 't': p_ += 4; return boolean(true);
        case 'f': p_ += 5; return boolean(false);
        case 'n': {
            p_ += 4;
            auto v = std::make_shared<Value>();
            v->isNull = true;
            return v;
        }
        default: return number();
    }
}

Json::ValuePtr Json::boolean(bool b) {
    auto v = std::make_shared<Value>();
    v->isBool = true;
    v->boolVal = b;
    return v;
}

Json::ValuePtr Json::object() {
    auto v = std::make_shared<Value>();
    v->isObject = true;
    p_++;  // {
    ws();
    if (s_[p_] == '}') { p_++; return v; }
    while (true) {
        ws();
        std::string key = string();
        ws();
        p_++;  // :
        // Java: v.object.put(key, value()) — LinkedHashMap.put REPLACES an
        // existing key IN PLACE, keeping its original position. Duplicate keys
        // are not legal GeoJSON but the behaviour is reproduced anyway so the
        // two parsers cannot disagree on malformed input.
        auto val = value();
        bool replaced = false;
        for (auto& kv : v->object)
            if (kv.first == key) { kv.second = val; replaced = true; break; }
        if (!replaced) v->object.emplace_back(std::move(key), std::move(val));
        ws();
        if (s_[p_] == ',') { p_++; continue; }
        p_++;  // }
        return v;
    }
}

Json::ValuePtr Json::array() {
    auto v = std::make_shared<Value>();
    v->isArray = true;
    p_++;  // [
    ws();
    if (s_[p_] == ']') { p_++; return v; }
    while (true) {
        v->array.push_back(value());
        ws();
        if (s_[p_] == ',') { p_++; continue; }
        p_++;  // ]
        return v;
    }
}

std::string Json::string() {
    std::string sb;
    p_++;  // opening quote
    while (true) {
        const char c = s_[p_++];
        if (c == '"') return sb;
        if (c != '\\') { sb.push_back(c); continue; }
        const char e = s_[p_++];
        switch (e) {
            case 'n': sb.push_back('\n'); break;
            case 't': sb.push_back('\t'); break;
            case 'r': sb.push_back('\r'); break;
            case 'b': sb.push_back('\b'); break;
            case 'f': sb.push_back('\f'); break;
            case 'u': {
                // Java appends a raw UTF-16 code unit here. Storing UTF-8, so a
                // high surrogate must be paired with the low surrogate that
                // follows before encoding, or the character comes out mangled.
                uint32_t cp = 0;
                std::from_chars(s_.data() + p_, s_.data() + p_ + 4, cp, 16);
                p_ += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && p_ + 1 < s_.size() && s_[p_] == '\\' &&
                    s_[p_ + 1] == 'u') {
                    uint32_t lo = 0;
                    std::from_chars(s_.data() + p_ + 2, s_.data() + p_ + 6, lo, 16);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        p_ += 6;
                    }
                }
                appendUtf8(sb, cp);
                break;
            }
            default: sb.push_back(e); break;
        }
    }
}

Json::ValuePtr Json::number() {
    const std::size_t start = p_;
    static constexpr std::string_view kNumChars = "+-0123456789.eE";
    while (p_ < s_.size() && kNumChars.find(s_[p_]) != std::string_view::npos) p_++;
    auto v = std::make_shared<Value>();
    v->isNum = true;
    const std::string tok(s_.substr(start, p_ - start));
    // strtod matches Double.parseDouble for every finite literal JSON can carry.
    v->num = std::strtod(tok.c_str(), nullptr);
    return v;
}

}  // namespace pzformat

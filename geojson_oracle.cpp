// geojson_oracle.cpp — C++ side of the Json + GeoJson cross-language oracle.
//
//   geojson_oracle <input.geojson> <out.txt>
//
// Must produce a file byte-identical to GeoJsonOracle.java's output. See that
// file for why the digest is unsorted and why coordinates are emitted as bits.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include "geojson.hpp"

using namespace pzformat;

// Escape control characters so one record is always exactly one line with fixed
// tab positions. Must match GeoJsonOracle.java's esc() byte for byte. Operates
// on UTF-8 bytes; every byte of a multi-byte sequence is >= 0x80 so none is
// touched, which is what makes this agree with the Java char-wise version.
static std::string esc(const std::string& s) {
    std::string b;
    b.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
            case '\\': b += "\\\\"; break;
            case '\t': b += "\\t"; break;
            case '\n': b += "\\n"; break;
            case '\r': b += "\\r"; break;
            default:
                if (c < 0x20) {
                    char t[8];
                    std::snprintf(t, sizeof t, "\\x%02x", c);
                    b += t;
                } else {
                    b.push_back(static_cast<char>(c));
                }
        }
    }
    return b;
}

static std::string bitsHex(double d) {
    std::uint64_t bits;
    std::memcpy(&bits, &d, sizeof bits);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(bits));
    return buf;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: geojson_oracle <input.geojson> <out.txt>\n");
        return 2;
    }

    GeoJson g = GeoJson::read(argv[1]);

    std::ofstream out(argv[2], std::ios::binary);
    std::size_t lines = 0;
    auto emit = [&](const std::string& s) {
        out << s << '\n';
        lines++;
    };

    for (std::size_t i = 0; i < g.features.size(); i++) {
        const auto& f = g.features[i];
        std::size_t pts = 0;
        for (const auto& r : f.rings) pts += r.size();
        const std::string type = f.type.empty() ? "-" : f.type;
        emit("F\t" + std::to_string(i) + "\t" + esc(type) + "\t" + std::to_string(f.rings.size()) +
             "\t" + std::to_string(pts));

        for (const auto& kv : f.props)
            emit("P\t" + std::to_string(i) + "\t" + esc(kv.first) + "\t" + esc(kv.second));

        for (std::size_t r = 0; r < f.rings.size(); r++) {
            const auto& ring = f.rings[r];
            for (std::size_t j = 0; j < ring.size(); j++) {
                emit("C\t" + std::to_string(i) + "\t" + std::to_string(r) + "\t" +
                     std::to_string(j) + "\t" + bitsHex(ring[j].lon) + "\t" +
                     bitsHex(ring[j].lat));
            }
        }
    }

    std::printf("cpp  geojson: %zu features, %zu digest lines -> %s\n", g.features.size(), lines,
                argv[2]);
    return 0;
}

// Read -> write -> byte-compare against the original file.
//
// Mirrors RoundTrip.java, deliberately including its output shape, so the two
// trees can be run over the same directory and the numbers diffed line for
// line. Any divergence is a port bug with a cell name attached.
//
//   pz_roundtrip <dir> [limit]
//
// Parsing without error only proves we consumed every byte. Round-tripping
// proves we RETAINED every byte, which is a much stronger claim and the real
// test of whether the read model is complete.
#include "lotheader.hpp"
#include "lotpack.hpp"
#include "mapped_file.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace pzformat;
namespace fs = std::filesystem;

namespace {

constexpr Policy kPolicies[] = {
    Policy::SpanLevelsMinimal,
    Policy::BreakAtLevelMinimal,
    Policy::SpanLevelsFull,
    Policy::BreakAtLevelFull,
};

const char* name(Policy p) {
    switch (p) {
        case Policy::SpanLevelsMinimal:   return "SPAN_LEVELS_MINIMAL";
        case Policy::BreakAtLevelMinimal: return "BREAK_AT_LEVEL_MINIMAL";
        case Policy::SpanLevelsFull:      return "SPAN_LEVELS_FULL";
        case Policy::BreakAtLevelFull:    return "BREAK_AT_LEVEL_FULL";
    }
    return "?";
}

std::size_t firstDiff(std::span<const std::byte> a, std::span<const std::byte> b) {
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) if (a[i] != b[i]) return i;
    return n;
}

std::string hexAround(std::span<const std::byte> a, std::size_t at) {
    std::string s;
    const std::size_t from = at > 4 ? at - 4 : 0;
    const std::size_t to = std::min(a.size(), at + 12);
    char buf[8];
    for (std::size_t i = from; i < to; ++i) {
        if (i == at) s += '[';
        std::snprintf(buf, sizeof buf, "%02X", std::to_integer<unsigned>(a[i]));
        s += buf;
        if (i == at) s += ']';
        s += ' ';
    }
    if (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

bool sameBytes(std::span<const std::byte> a, std::span<const std::byte> b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}

// ------------------------------------------------------------ lotheader -----

void headerRoundTrip(const std::vector<fs::path>& headers) {
    std::cout << "=== .lotheader round trip ===\n";
    std::size_t identical = 0, differing = 0, errored = 0;
    std::vector<std::string> diffs;
    std::map<long long, int> lengthDelta;

    for (const auto& f : headers) {
        try {
            MappedFile mf(f);
            const auto original = mf.span();
            const LotHeader h = LotHeader::read(original);
            const auto rewritten = h.write();

            if (sameBytes(original, rewritten)) { ++identical; continue; }

            ++differing;
            lengthDelta[static_cast<long long>(rewritten.size())
                      - static_cast<long long>(original.size())] += 1;
            if (diffs.size() < 8) {
                const std::size_t at = firstDiff(original, rewritten);
                diffs.push_back(f.filename().string() + ": first diff at byte "
                    + std::to_string(at) + " (orig " + std::to_string(original.size())
                    + " B, rewritten " + std::to_string(rewritten.size()) + " B)"
                    + "\n         orig: " + hexAround(original, at)
                    + "\n         ours: " + hexAround(rewritten, at));
            }
        } catch (const std::exception& e) {
            ++errored;
            if (diffs.size() < 8) diffs.push_back(f.filename().string() + ": " + e.what());
        }
    }

    std::printf("  byte-identical : %zu / %zu  (%.2f%%)\n", identical, headers.size(),
                headers.empty() ? 0.0 : 100.0 * static_cast<double>(identical)
                                        / static_cast<double>(headers.size()));
    std::cout << "  differing      : " << differing << '\n';
    std::cout << "  errored        : " << errored << '\n';
    if (!lengthDelta.empty()) {
        std::cout << "  length deltas (rewritten - original): {";
        bool first = true;
        for (const auto& [d, n] : lengthDelta) {
            if (!first) std::cout << ", ";
            std::cout << d << '=' << n;
            first = false;
        }
        std::cout << "}\n";
    }
    for (const auto& d : diffs) std::cout << "     " << d << '\n';
    if (identical == headers.size() && !headers.empty()) {
        std::cout << "  => read model is COMPLETE: nothing dropped, nothing reordered\n";
    }
    std::cout << '\n';
}

// -------------------------------------------------------------- lotpack -----

void lotpackRoundTrip(const fs::path& dir, const std::vector<fs::path>& headers) {
    std::cout << "=== .lotpack chunk encoding policy ===\n";

    constexpr std::size_t nPolicies = std::size(kPolicies);
    long long chunkExact[nPolicies] = {};
    int fileExact[nPolicies] = {};
    long long chunksTotal = 0;
    int cells = 0, errored = 0;
    std::vector<std::string> notes;
    std::vector<std::string> firstMismatchShape;

    for (const auto& hf : headers) {
        std::string stem = hf.filename().string();
        const std::string ext = ".lotheader";
        if (stem.size() > ext.size()) stem.resize(stem.size() - ext.size());

        const fs::path pf = dir / ("world_" + stem + ".lotpack");
        if (!fs::exists(pf)) continue;

        try {
            MappedFile hm(hf);
            const LotHeader h = LotHeader::read(hm.span());

            MappedFile pm(pf);
            const LotPack lp = LotPack::read(pm.span(), h.levelRange());
            const auto original = lp.rawFile();
            ++cells;

            for (int ci = 0; ci < lp.chunkCount(); ++ci) {
                // Offset table is COLUMN-major: index = cx * chunksPerSide + cy.
                // Decomposing it row-major here compared each chunk's bytes
                // against a different chunk's encoding.
                const int cx = ci / lp.chunksPerSide();
                const int cy = ci % lp.chunksPerSide();
                const Chunk c = lp.chunk(cx, cy);
                const auto rawBody = lp.rawChunk(ci);
                ++chunksTotal;

                for (std::size_t p = 0; p < nPolicies; ++p) {
                    const auto enc = lp.encodeChunk(c, kPolicies[p]);
                    if (sameBytes(rawBody, enc)) {
                        ++chunkExact[p];
                    } else if (p == 0 && ci == 0 && firstMismatchShape.size() < 6) {
                        firstMismatchShape.push_back(stem + " chunk0: orig "
                            + std::to_string(rawBody.size()) + " B, ours "
                            + std::to_string(enc.size()) + " B, first diff @"
                            + std::to_string(firstDiff(rawBody, enc)));
                    }
                }
            }

            for (std::size_t p = 0; p < nPolicies; ++p) {
                if (sameBytes(original, lp.write(kPolicies[p]))) ++fileExact[p];
            }
        } catch (const std::exception& e) {
            ++errored;
            if (notes.size() < 6) notes.push_back(stem + ": " + e.what());
        }
    }

    std::cout << "  cells: " << cells << "   chunks: " << chunksTotal
              << "   errored: " << errored << '\n';
    std::cout << "\n  per-policy chunk body reproduction:\n";
    for (std::size_t p = 0; p < nPolicies; ++p) {
        std::printf("     %-24s %lld / %lld chunks  (%.2f%%)   whole files: %d/%d\n",
            name(kPolicies[p]), chunkExact[p], chunksTotal,
            chunksTotal == 0 ? 0.0
                : 100.0 * static_cast<double>(chunkExact[p]) / static_cast<double>(chunksTotal),
            fileExact[p], cells);
    }

    std::size_t best = 0;
    for (std::size_t p = 1; p < nPolicies; ++p) if (chunkExact[p] > chunkExact[best]) best = p;
    if (chunksTotal > 0 && chunkExact[best] == chunksTotal) {
        std::cout << "\n  => ENCODER POLICY CONFIRMED: " << name(kPolicies[best]) << '\n';
    } else {
        std::cout << "\n  no policy reproduces every chunk; best is "
                  << name(kPolicies[best]) << '\n';
        for (const auto& s : firstMismatchShape) std::cout << "     " << s << '\n';
    }
    for (const auto& n : notes) std::cout << "     " << n << '\n';
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: pz_roundtrip <dir> [limit]\n";
        return 2;
    }
    const fs::path dir = argv[1];
    const int limit = argc > 2 ? std::atoi(argv[2]) : 0;

    std::vector<fs::path> headers;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.is_regular_file() && e.path().extension() == ".lotheader") {
            headers.push_back(e.path());
        }
    }
    std::sort(headers.begin(), headers.end());
    if (limit > 0 && static_cast<int>(headers.size()) > limit) {
        headers.resize(static_cast<std::size_t>(limit));
    }
    std::cout << "cells to test: " << headers.size() << "\n\n";

    headerRoundTrip(headers);
    lotpackRoundTrip(dir, headers);
    return 0;
}

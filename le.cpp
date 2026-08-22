#include "le.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace pzformat {

namespace detail {

void throwShortRead(std::size_t n, std::size_t pos, std::size_t len) {
    throw ParseError("read of " + std::to_string(n) + " bytes at offset "
                     + std::to_string(pos) + " exceeds file length "
                     + std::to_string(len));
}

} // namespace detail

std::vector<std::byte> readAllBytes(const std::filesystem::path& file) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec) throw ParseError("cannot stat " + file.string() + ": " + ec.message());

    std::vector<std::byte> out(static_cast<std::size_t>(size));

    std::ifstream in(file, std::ios::binary);
    if (!in) throw ParseError("cannot open " + file.string());
    if (size != 0) {
        in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
        if (in.gcount() != static_cast<std::streamsize>(size)) {
            throw ParseError("short read on " + file.string() + ": got "
                             + std::to_string(in.gcount()) + " of "
                             + std::to_string(size));
        }
    }
    return out;
}

void LE::seek(std::size_t newPos) {
    if (newPos > b_.size()) {
        throw ParseError("seek to " + std::to_string(newPos)
                         + " exceeds file length " + std::to_string(b_.size()));
    }
    p_ = newPos;
}

std::vector<std::byte> LE::bytes(std::size_t n) {
    require(n);
    std::vector<std::byte> out(n);
    if (n != 0) std::memcpy(out.data(), b_.data() + p_, n);
    p_ += n;
    return out;
}

std::string LE::lenString() {
    const std::int32_t n = i32();
    if (n < 0 || n > (1 << 20)) {
        throw ParseError("implausible string length " + std::to_string(n)
                         + " at offset " + std::to_string(p_ - 4));
    }
    const auto raw = view(static_cast<std::size_t>(n));
    return std::string(reinterpret_cast<const char*>(raw.data()), raw.size());
}

std::string LE::cString() {
    std::string s;
    while (!eof()) {
        const std::uint8_t c = u8();
        if (c == 0 || c == '\n' || c == '\r') break;
        s.push_back(static_cast<char>(c));
    }
    return s;
}

std::string LE::hexDump(std::size_t offset, std::size_t n) const {
    std::string sb;
    const std::size_t end = std::min(offset + n, b_.size());
    char buf[16];

    for (std::size_t i = offset; i < end; i += 16) {
        std::snprintf(buf, sizeof buf, "%08X  ", static_cast<unsigned>(i));
        sb += buf;

        std::string ascii;
        for (std::size_t j = 0; j < 16; ++j) {
            if (i + j < end) {
                const auto v = std::to_integer<std::uint8_t>(b_[i + j]);
                std::snprintf(buf, sizeof buf, "%02X ", v);
                sb += buf;
                ascii.push_back(v >= 32 && v < 127 ? static_cast<char>(v) : '.');
            } else {
                sb += "   ";
            }
            if (j == 7) sb.push_back(' ');
        }
        sb += " |";
        sb += ascii;
        sb += "|\n";
    }
    return sb;
}

} // namespace pzformat

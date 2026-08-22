// Port of LEW.java.
//
// Little-endian writer. Mirrors LE's read conventions exactly, so that
// read(write(x)) == x by construction and write(read(bytes)) == bytes is a
// meaningful test of whether the read model retained everything.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pzformat {

class LEW {
public:
    LEW() = default;
    explicit LEW(std::size_t reserveBytes) { o_.reserve(reserveBytes); }

    std::size_t size() const noexcept { return o_.size(); }
    void reserve(std::size_t n) { o_.reserve(n); }
    void clear() noexcept { o_.clear(); }

    /// Java: toByteArray() — but non-copying. take() moves the buffer out.
    std::span<const std::byte> data() const noexcept { return o_; }
    std::vector<std::byte> take() noexcept { return std::move(o_); }

    LEW& u8(int v) {
        o_.push_back(static_cast<std::byte>(v & 0xFF));
        return *this;
    }

    LEW& i32(std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        o_.push_back(static_cast<std::byte>(u & 0xFFu));
        o_.push_back(static_cast<std::byte>((u >> 8) & 0xFFu));
        o_.push_back(static_cast<std::byte>((u >> 16) & 0xFFu));
        o_.push_back(static_cast<std::byte>((u >> 24) & 0xFFu));
        return *this;
    }

    LEW& i64(std::int64_t v) {
        const auto u = static_cast<std::uint64_t>(v);
        for (int i = 0; i < 8; ++i) {
            o_.push_back(static_cast<std::byte>((u >> (8 * i)) & 0xFFu));
        }
        return *this;
    }

    LEW& bytes(std::span<const std::byte> b) {
        o_.insert(o_.end(), b.begin(), b.end());
        return *this;
    }

    /// One byte per char, raw — the Java's ISO_8859_1 encode. See the note on
    /// LE::lenString about the Qt boundary.
    LEW& ascii(std::string_view s) {
        const auto* p = reinterpret_cast<const std::byte*>(s.data());
        o_.insert(o_.end(), p, p + s.size());
        return *this;
    }

    /// Length-prefixed string, as used by .pack entry and page names.
    LEW& lenString(std::string_view s) {
        i32(static_cast<std::int32_t>(s.size()));
        return ascii(s);
    }

    /// Newline-terminated string, as used by B42 tile and room names.
    LEW& nlString(std::string_view s) {
        ascii(s);
        return u8('\n');
    }

private:
    std::vector<std::byte> o_;
};

} // namespace pzformat

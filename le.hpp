// Port of LE.java.
//
// Little-endian reader. All Project Zomboid binary formats are little-endian.
//
// Tracks position so a failed parse can report *where* it went wrong, which is
// the whole point of the probe workflow.
//
// Difference from the Java: this reads over a std::span rather than owning a
// byte[]. The bytes may come from readAllBytes() or from MappedFile; LE does
// not care and does not copy. That is the change that lets the viewport stream
// chunks out of an mmap'ed .lotpack without a per-cell allocation.
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace pzformat {

/// Java: LE.ParseException
class ParseError : public std::runtime_error {
public:
    explicit ParseError(const std::string& msg) : std::runtime_error(msg) {}
};

/// Java: LE.of(Path) — reads the whole file into memory.
/// Prefer MappedFile for anything cell-sized or larger.
std::vector<std::byte> readAllBytes(const std::filesystem::path& file);

namespace detail {
[[noreturn]] void throwShortRead(std::size_t n, std::size_t pos, std::size_t len);

constexpr std::uint32_t bswap32(std::uint32_t v) noexcept {
    return (v >> 24) | ((v >> 8) & 0x0000FF00u) | ((v << 8) & 0x00FF0000u) | (v << 24);
}
constexpr std::uint64_t bswap64(std::uint64_t v) noexcept {
    return (static_cast<std::uint64_t>(bswap32(static_cast<std::uint32_t>(v))) << 32)
         | bswap32(static_cast<std::uint32_t>(v >> 32));
}
} // namespace detail

class LE {
public:
    LE() = default;
    explicit LE(std::span<const std::byte> data) noexcept : b_(data) {}

    std::size_t pos()       const noexcept { return p_; }
    std::size_t remaining() const noexcept { return b_.size() - p_; }
    std::size_t length()    const noexcept { return b_.size(); }
    bool        eof()       const noexcept { return p_ >= b_.size(); }

    /// Java's seek() does not validate; this does. A seek past the end can only
    /// ever be a bug, and failing here names the offset instead of failing at
    /// the next read with a position that has already lost its context.
    void seek(std::size_t newPos);

    std::uint8_t u8() {
        require(1);
        return std::to_integer<std::uint8_t>(b_[p_++]);
    }

    /// Signed, matching Java's int. .lotpack chunk offsets are read as signed
    /// i32 there; keep the same type so the same values overflow the same way.
    std::int32_t i32() {
        require(4);
        std::uint32_t v;
        std::memcpy(&v, b_.data() + p_, 4);
        if constexpr (std::endian::native == std::endian::big) v = detail::bswap32(v);
        p_ += 4;
        return static_cast<std::int32_t>(v);
    }

    std::int64_t i64() {
        require(8);
        std::uint64_t v;
        std::memcpy(&v, b_.data() + p_, 8);
        if constexpr (std::endian::native == std::endian::big) v = detail::bswap64(v);
        p_ += 8;
        return static_cast<std::int64_t>(v);
    }

    /// Peek an i32 without advancing.
    std::int32_t peekI32() {
        const std::size_t save = p_;
        const std::int32_t v = i32();
        p_ = save;
        return v;
    }

    /// Zero-copy. Not in the Java, which had no way to express it.
    /// The span is valid only as long as the underlying buffer is.
    std::span<const std::byte> view(std::size_t n) {
        require(n);
        const auto s = b_.subspan(p_, n);
        p_ += n;
        return s;
    }

    /// Java: bytes(int n) — copies.
    std::vector<std::byte> bytes(std::size_t n);

    /// Length-prefixed string: int32 char count, then that many single-byte
    /// chars. Used by .pack entry/page names.
    ///
    /// Bytes are kept raw, one byte per char, which is what Java's
    /// ISO_8859_1 decode did. Never convert these to UTF-8 on the way to Qt:
    /// use QString::fromLatin1 / QString::toLatin1 at the UI boundary or the
    /// round-trip stops being byte-identical for any name >= 0x80.
    std::string lenString();

    /// Null-terminated string, used by .lotheader's tile-name table.
    /// Also stops at \r or \n, which some writers emit as separators.
    /// Stops at end-of-buffer without throwing, as the Java did.
    std::string cString();

    /// Hex + ASCII dump of n bytes starting at offset, for eyeballing unknown
    /// regions. Byte-for-byte identical layout to the Java, so probe output
    /// from the two trees can be diffed directly.
    std::string hexDump(std::size_t offset, std::size_t n) const;

private:
    void require(std::size_t n) const {
        // p_ <= b_.size() is an invariant, so the subtraction cannot wrap and
        // this cannot overflow the way `p_ + n > size` can.
        if (n > b_.size() - p_) detail::throwShortRead(n, p_, b_.size());
    }

    std::span<const std::byte> b_{};
    std::size_t p_ = 0;
};

} // namespace pzformat

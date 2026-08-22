// Not a port — there is no Java counterpart, because byte[] could not express
// it. A read-only mmap of a file, handed to LE as a span.
//
// This is the reason LE takes a span instead of owning its bytes: the viewport
// can map a .lotpack once and decode chunks on demand from arbitrary offsets,
// with no per-cell allocation and no read of the parts of the cell that are
// off-screen.
//
// POSIX only. Windows needs CreateFileMapping/MapViewOfFile; UNVERIFIED, not
// written, and not needed until someone asks for a Windows build.
#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <utility>

namespace pzformat {

class MappedFile {
public:
    MappedFile() = default;
    explicit MappedFile(const std::filesystem::path& file);

    ~MappedFile();

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    MappedFile(MappedFile&& other) noexcept
        : data_(std::exchange(other.data_, nullptr)),
          size_(std::exchange(other.size_, 0)) {}

    MappedFile& operator=(MappedFile&& other) noexcept {
        if (this != &other) {
            close();
            data_ = std::exchange(other.data_, nullptr);
            size_ = std::exchange(other.size_, 0);
        }
        return *this;
    }

    std::span<const std::byte> span() const noexcept { return {data_, size_}; }
    std::size_t size() const noexcept { return size_; }
    bool valid() const noexcept { return data_ != nullptr; }

private:
    void close() noexcept;

    const std::byte* data_ = nullptr;
    std::size_t size_ = 0;
};

} // namespace pzformat

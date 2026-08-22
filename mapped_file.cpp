#include "mapped_file.hpp"

#include "le.hpp" // ParseError

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace pzformat {

MappedFile::MappedFile(const std::filesystem::path& file) {
    const int fd = ::open(file.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        throw ParseError("cannot open " + file.string() + ": " + std::strerror(errno));
    }

    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        const int e = errno;
        ::close(fd);
        throw ParseError("cannot stat " + file.string() + ": " + std::strerror(e));
    }

    size_ = static_cast<std::size_t>(st.st_size);
    if (size_ == 0) {
        // mmap of length 0 is an error; an empty file maps to an empty span.
        ::close(fd);
        data_ = nullptr;
        return;
    }

    void* p = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
    const int e = errno;
    ::close(fd); // the mapping keeps its own reference
    if (p == MAP_FAILED) {
        size_ = 0;
        throw ParseError("cannot mmap " + file.string() + ": " + std::strerror(e));
    }

    data_ = static_cast<const std::byte*>(p);
}

MappedFile::~MappedFile() { close(); }

void MappedFile::close() noexcept {
    if (data_ != nullptr && size_ != 0) {
        ::munmap(const_cast<void*>(static_cast<const void*>(data_)), size_);
    }
    data_ = nullptr;
    size_ = 0;
}

} // namespace pzformat

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <dirent.h>
#include <fcntl.h>
#include <forward_list>
#include <functional>
#include <getopt.h>
#include <limits>
#include <list>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include <fbl/macros.h>
#include <fbl/unique_fd.h>
#include <lib/cksum.h>
#include <lz4/lz4frame.h>
#include <zircon/boot/image.h>

namespace {

bool Aligned(uint32_t length) {
    return length % ZBI_ALIGNMENT == 0;
}

// It's not clear where this magic number comes from.
constexpr size_t kLZ4FMaxHeaderFrameSize = 128;

// iovec.iov_base is void* but we only use pointers to const.
template<typename T>
iovec Iovec(const T* buffer, size_t size = sizeof(T)) {
    assert(size > 0);
    return {const_cast<void*>(static_cast<const void*>(buffer)), size};
}

class AppendBuffer {
public:
    explicit AppendBuffer(size_t size) :
        buffer_(std::make_unique<uint8_t[]>(size)), ptr_(buffer_.get()) {
    }

    size_t size() const {
        return ptr_ - buffer_.get();
    }

    iovec get() {
        return Iovec(buffer_.get(), size());
    }

    std::unique_ptr<uint8_t[]> release() {
        ptr_ = nullptr;
        return std::move(buffer_);
    }

    template<typename T>
    void Append(const T* data, size_t bytes = sizeof(T)) {
        ptr_ = static_cast<uint8_t*>(memcpy(static_cast<void*>(ptr_),
                                            static_cast<const void*>(data),
                                            bytes)) + bytes;
    }

    void Pad(size_t bytes) {
        ptr_ = static_cast<uint8_t*>(memset(static_cast<void*>(ptr_), 0,
                                            bytes)) + bytes;
    }

private:
    std::unique_ptr<uint8_t[]> buffer_;
    uint8_t* ptr_ = nullptr;
};

class Item;

class OutputStream {
public:
    OutputStream() = delete;

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(OutputStream);

    explicit OutputStream(fbl::unique_fd fd) : fd_(std::move(fd)) {
    }

    ~OutputStream() {
        Flush();
    }

    // Queue the iovec for output.  The second argument can transfer
    // ownership of the memory that buffer.iov_base points into.  This
    // object may refer to buffer.iov_base until Flush() completes.
    void Write(const iovec& buffer,
               std::unique_ptr<uint8_t[]> owned = nullptr) {
        assert(buffer.iov_len > 0);
        if (buffer.iov_len + total_ > UINT32_MAX - sizeof(zbi_header_t) + 1) {
            fprintf(stderr, "output size exceeds format maximum\n");
            exit(1);
        }
        total_ += static_cast<uint32_t>(buffer.iov_len);
        *write_pos_++ = buffer;
        if (write_pos_ == iov_.end()) {
            Flush();
        } else if (owned) {
            owned_buffers_.push_front(std::move(owned));
        }
    }

    uint32_t WritePosition() const {
        return total_;
    }

    void Flush() {
        auto read_pos = iov_.begin();
        while (read_pos != write_pos_) {
            read_pos = WriteBuffers(read_pos);
        }
        write_pos_ = iov_.begin();
        owned_buffers_.clear();
    }

    // Emit a placeholder.  The return value will be passed to PatchHeader.
    uint32_t PlaceHeader() {
        uint32_t pos = WritePosition();
        static const zbi_header_t dummy = {};
        Write(Iovec(&dummy));
        return pos;
    }

    // Replace a placeholder with a real header.
    void PatchHeader(const zbi_header_t& header, uint32_t place) {
        assert(place < total_);
        assert(total_ - place >= sizeof(header));

        if (flushed_ <= place) {
            // We haven't actually written it yet, so just update it in
            // memory.  A placeholder always has its own iovec, so just
            // skip over earlier ones until we hit the right offset.
            auto it = iov_.begin();
            for (place -= flushed_; place > 0; place -= it++->iov_len) {
                assert(it != write_pos_);
                assert(place >= it->iov_len);
            }
            assert(it->iov_len == sizeof(header));
            auto buffer = std::make_unique<uint8_t[]>(sizeof(header));
            it->iov_base = memcpy(buffer.get(), &header, sizeof(header));
            owned_buffers_.push_front(std::move(buffer));
        } else {
            assert(flushed_ >= place + sizeof(header));
            // Overwrite the earlier part of the file with pwrite.  This
            // does not affect the current lseek position for the next writev.
            auto buf = reinterpret_cast<const uint8_t*>(&header);
            size_t len = sizeof(header);
            while (len > 0) {
                ssize_t wrote = pwrite(fd_.get(), buf, len, place);
                if (wrote < 0) {
                    perror("pwrite on output file");
                    exit(1);
                }
                len -= wrote;
                buf += wrote;
                place += wrote;
            }
        }
    }

private:
    using IovecArray = std::array<iovec, IOV_MAX>;
    IovecArray iov_;
    IovecArray::iterator write_pos_ = iov_.begin();
    // iov_[n].iov_base might point into these buffers.  They're just
    // stored here to own the buffers until iov_ is flushed.
    std::forward_list<std::unique_ptr<uint8_t[]>> owned_buffers_;
    fbl::unique_fd fd_;
    uint32_t flushed_ = 0;
    uint32_t total_ = 0;

    bool Buffering() const {
        return write_pos_ != iov_.begin();
    }

    IovecArray::iterator WriteBuffers(IovecArray::iterator read_pos) {
        assert(read_pos != write_pos_);
        ssize_t wrote = writev(fd_.get(), &(*read_pos), write_pos_ - read_pos);
        if (wrote < 0) {
            perror("writev to output file");
            exit(1);
        }
        flushed_ += wrote;
#ifndef NDEBUG
        off_t pos = lseek(fd_.get(), 0, SEEK_CUR);
#endif
        assert(static_cast<off_t>(flushed_) == pos ||
               (pos == -1 && errno == ESPIPE));
        // Skip all the buffers that were wholly written.
        while (wrote >= read_pos->iov_len) {
            wrote -= read_pos->iov_len;
            ++read_pos;
            if (wrote == 0) {
                break;
            }
            assert(read_pos != write_pos_);
        }
        if (wrote > 0) {
            // writev wrote only part of this buffer.  Do the rest next time.
            read_pos->iov_len -= wrote;
            read_pos->iov_base = static_cast<void*>(
                static_cast<uint8_t*>(read_pos->iov_base) + wrote);
        }
        return read_pos;
    }
};

class Checksummer {
public:
    void Write(const iovec& buffer) {
        crc_ = crc32(crc_, static_cast<const uint8_t*>(buffer.iov_base),
                     buffer.iov_len);
    }

    void FinalizeHeader(zbi_header_t* header) {
        header->crc32 = 0;
        uint32_t header_crc = crc32(
            0, reinterpret_cast<const uint8_t*>(header), sizeof(*header));
        header->crc32 = crc32_combine(header_crc, crc_, header->length);
    }

private:
    uint32_t crc_ = 0;
};

// This tells LZ4f_compressUpdate it can keep a pointer to data.
constexpr const LZ4F_compressOptions_t kCompressOpt  = { 1, {} };

class Compressor {
public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Compressor);
    Compressor() = default;

#define LZ4F_CALL(func, ...)                                            \
    [&](){                                                              \
        auto result = func(__VA_ARGS__);                                \
        if (LZ4F_isError(result)) {                                     \
            fprintf(stderr, "%s: %s\n", #func, LZ4F_getErrorName(result)); \
            exit(1);                                                    \
        }                                                               \
        return result;                                                  \
    }()

    void Init(OutputStream* out, const zbi_header_t& header) {
        header_ = header;
        assert(header_.flags & ZBI_FLAG_STORAGE_COMPRESSED);
        assert(header_.flags & ZBI_FLAG_CRC32);

        // Write a place-holder for the header, which we will go back
        // and fill in once we know the payload length and CRC.
        header_pos_ = out->PlaceHeader();

        prefs_.frameInfo.contentSize = header_.length;

        prefs_.frameInfo.blockSizeID = LZ4F_max64KB;
        prefs_.frameInfo.blockMode = LZ4F_blockIndependent;

        // LZ4 compression levels 1-3 are for "fast" compression, and 4-16
        // are for higher compression. The additional compression going from
        // 4 to 16 is not worth the extra time needed during compression.
        prefs_.compressionLevel = 4;

        LZ4F_CALL(LZ4F_createCompressionContext, &ctx_, LZ4F_VERSION);

        // Record the original uncompressed size in header_.extra.
        // WriteBuffer will accumulate the compressed size in header_.length.
        header_.extra = header_.length;
        header_.length = 0;

        // This might start writing compression format headers before it
        // receives any data.
        auto buffer = GetBuffer(kLZ4FMaxHeaderFrameSize);
        size_t size = LZ4F_CALL(LZ4F_compressBegin, ctx_,
                                buffer.data.get(), buffer.size, &prefs_);
        assert(size <= buffer.size);
        WriteBuffer(out, std::move(buffer), size);
    }

    ~Compressor() {
        LZ4F_CALL(LZ4F_freeCompressionContext, ctx_);
    }

    // NOTE: Input buffer may be referenced for the life of the Compressor!
    void Write(OutputStream* out, const iovec& input) {
        auto buffer = GetBuffer(LZ4F_compressBound(input.iov_len, &prefs_));
        size_t actual_size = LZ4F_CALL(LZ4F_compressUpdate,
                                       ctx_, buffer.data.get(), buffer.size,
                                       input.iov_base, input.iov_len,
                                       &kCompressOpt);
        WriteBuffer(out, std::move(buffer), actual_size);
    }

    uint32_t Finish(OutputStream* out) {
        // Write the closing chunk from the compressor.
        auto buffer = GetBuffer(LZ4F_compressBound(0, &prefs_));
        size_t actual_size = LZ4F_CALL(LZ4F_compressEnd,
                                       ctx_, buffer.data.get(), buffer.size,
                                       &kCompressOpt);

        WriteBuffer(out, std::move(buffer), actual_size);

        // Complete the checksum.
        crc_.FinalizeHeader(&header_);

        // Write the header back where its place was held.
        out->PatchHeader(header_, header_pos_);
        return header_.length;
    }

private:
    struct Buffer {
        // Move-only type: after moving, data is nullptr and size is 0.
        Buffer() = default;
        Buffer(std::unique_ptr<uint8_t[]> buffer, size_t max_size) :
            data(std::move(buffer)), size(max_size) {
        }
        Buffer(Buffer&& other) {
            *this = std::move(other);
        }
        Buffer& operator=(Buffer&& other) {
            data = std::move(other.data);
            size = other.size;
            other.size = 0;
            return *this;
        }
        std::unique_ptr<uint8_t[]> data;
        size_t size = 0;
    } unused_buffer_;
    zbi_header_t header_;
    Checksummer crc_;
    LZ4F_compressionContext_t ctx_;
    LZ4F_preferences_t prefs_{};
    uint32_t header_pos_ = 0;
    // IOV_MAX buffers might be live at once.
    static constexpr const size_t kMinBufferSize = (128 << 20) / IOV_MAX;

    Buffer GetBuffer(size_t max_size) {
        if (unused_buffer_.size >= max_size) {
            // We have an old buffer that will do fine.
            return std::move(unused_buffer_);
        } else {
            // Get a new buffer.
            max_size = std::max(max_size, kMinBufferSize);
            return {std::make_unique<uint8_t[]>(max_size), max_size};
        }
    }

    void WriteBuffer(OutputStream* out, Buffer buffer, size_t actual_size) {
        if (actual_size > 0) {
            header_.length += actual_size;
            const iovec iov{buffer.data.get(), actual_size};
            crc_.Write(iov);
            out->Write(iov, std::move(buffer.data));
            buffer.size = 0;
        } else {
            // The compressor often delivers zero bytes for an input chunk.
            // Stash the unused buffer for next time to cut down on new/delete.
            unused_buffer_ = std::move(buffer);
        }
    }
};

const size_t Compressor::kMinBufferSize;

constexpr const LZ4F_decompressOptions_t kDecompressOpt{};

std::unique_ptr<uint8_t[]> Decompress(const std::list<const iovec>& payload,
                                      uint32_t decompressed_length) {
    auto buffer = std::make_unique<uint8_t[]>(decompressed_length);

    LZ4F_decompressionContext_t ctx;
    LZ4F_CALL(LZ4F_createDecompressionContext, &ctx, LZ4F_VERSION);

    uint8_t* dst = buffer.get();
    size_t dst_size = decompressed_length;
    for (const auto& iov : payload) {
        auto src = static_cast<const uint8_t*>(iov.iov_base);
        size_t src_size = iov.iov_len;
        do {
            if (dst_size == 0) {
                fprintf(stderr, "decompression produced too much data\n");
                exit(1);
            }

            size_t nwritten = dst_size, nread = src_size;
            LZ4F_CALL(LZ4F_decompress, ctx, dst, &nwritten, src, &nread,
                      &kDecompressOpt);

            assert(nread <= src_size);
            src += nread;
            src_size -= nread;

            assert(nwritten <= dst_size);
            dst += nwritten;
            dst_size -= nwritten;
        } while (src_size > 0);
    }
    if (dst_size > 0) {
        fprintf(stderr,
                "decompression produced too little data by %zu bytes\n",
                dst_size);
        exit(1);
    }

    LZ4F_CALL(LZ4F_freeDecompressionContext, ctx);

    return buffer;
}

#undef LZ4F_CALL

class FileContents {
public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(FileContents);
    FileContents() = default;

    FileContents(FileContents&& other) {
        *this = std::move(other);
    }

    FileContents& operator=(FileContents&& other) {
        std::swap(mapped_, other.mapped_);
        std::swap(mapped_size_, other.mapped_size_);
        std::swap(exact_size_, other.exact_size_);
        return *this;
    }

    ~FileContents() {
        if (mapped_) {
            munmap(mapped_, mapped_size_);
        }
    }

    size_t exact_size() const { return exact_size_; }
    size_t mapped_size() const { return mapped_size_; }

    static FileContents Map(const fbl::unique_fd& fd,
                            const struct stat& st,
                            const char* filename) {
        // st_size is off_t, everything else is size_t.
        const size_t size = st.st_size;
        static_assert(std::numeric_limits<decltype(st.st_size)>::max() <=
                      std::numeric_limits<size_t>::max(), "size_t < off_t?");

        static size_t pagesize = []() -> size_t {
            size_t pagesize = sysconf(_SC_PAGE_SIZE);
            assert(pagesize >= ZBI_BOOTFS_PAGE_SIZE);
            assert(pagesize % ZBI_BOOTFS_PAGE_SIZE == 0);
            return pagesize;
        }();

        void* map = mmap(nullptr, size,
                         PROT_READ, MAP_FILE | MAP_PRIVATE, fd.get(), 0);
        if (map == MAP_FAILED) {
            fprintf(stderr, "mmap: %s: %s\n", filename, strerror(errno));
            exit(1);
        }
        assert(map);

        FileContents result;
        result.mapped_ = map;
        result.exact_size_ = size;
        result.mapped_size_ = (size + pagesize - 1) & -pagesize;
        return result;
    }

    const iovec View(size_t offset, size_t length) const {
        assert(length > 0);
        assert(offset < exact_size_);
        assert(exact_size_ - offset >= length);
        return Iovec(static_cast<const uint8_t*>(mapped_) + offset, length);
    }

    const iovec PageRoundedView(size_t offset, size_t length) const {
        assert(length > 0);
        assert(offset < mapped_size_);
        assert(mapped_size_ - offset >= length);
        return Iovec(static_cast<const uint8_t*>(mapped_) + offset, length);
    }

private:
    void* mapped_ = nullptr;
    size_t mapped_size_ = 0;
    size_t exact_size_ = 0;
};

class FileOpener {
public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(FileOpener);
    FileOpener() = default;

    void Init(const char* output_file, const char* depfile) {
        if (depfile) {
            depfile_ = fopen(depfile, "w");
            if (!depfile_) {
                perror(depfile);
                exit(1);
            }
            fprintf(depfile_, "%s:", output_file);
        }
    }

    fbl::unique_fd Open(const char* file, struct stat* st = nullptr) {
        fbl::unique_fd fd(open(file, O_RDONLY));
        if (!fd) {
            perror(file);
            exit(1);
        }
        if (st && fstat(fd.get(), st) < 0) {
            perror("fstat");
            exit(1);
        }
        if (depfile_) {
            fprintf(depfile_, " %s", file);
        }
        return fd;
    }

    fbl::unique_fd Open(const std::string& file, struct stat* st = nullptr) {
        return Open(file.c_str(), st);
    }

    ~FileOpener() {
        if (depfile_) {
            fputc('\n', depfile_);
            fclose(depfile_);
        }
    }

private:
    FILE* depfile_ = nullptr;
};

void RequireRegularFile(const struct stat& st, const char* file) {
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "%s: not a regular file\n", file);
        exit(1);
    }
}

class GroupFilter {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(GroupFilter);
    GroupFilter() = default;

    void SetFilter(const char* groups) {
        if (!strcmp(groups, "all")) {
            groups_.reset();
        } else {
            groups_ = std::make_unique<std::set<std::string>>();
            while (const char *p = strchr(groups, ',')) {
                groups_->emplace(groups, p - groups);
                groups = p + 1;
            }
            groups_->emplace(groups);
        }
    }

    bool AllowsAll() const {
        return !groups_;
    }

    bool Allows(const std::string& group) const {
        return AllowsAll() || groups_->find(group) != groups_->end();
    }

private:
    std::unique_ptr<std::set<std::string>> groups_;
};

// Base class for ManifestInputFileGenerator and DirectoryInputFileGenerator.
// These both deliver target name -> file contents mappings until they don't.
struct InputFileGenerator {
    struct value_type {
        std::string target;
        FileContents file;
    };
    virtual ~InputFileGenerator() = default;
    virtual bool Next(FileOpener*, const std::string& prefix, value_type*) = 0;
};

using InputFileGeneratorList = std::list<std::unique_ptr<InputFileGenerator>>;

class ManifestInputFileGenerator : public InputFileGenerator {
public:
    ManifestInputFileGenerator(FileContents file, std::string prefix,
                               const GroupFilter* filter) :
        file_(std::move(file)), prefix_(std::move(prefix)), filter_(filter) {
        read_ptr_ = static_cast<const char*>(
            file_.View(0, file_.exact_size()).iov_base);
        eof_ = read_ptr_ + file_.exact_size();
    }

    ~ManifestInputFileGenerator() override = default;

    bool Next(FileOpener* opener, const std::string& prefix,
              value_type* value) override {
        while (read_ptr_ != eof_) {
            auto eol = static_cast<const char*>(
                memchr(read_ptr_, '\n', eof_ - read_ptr_));
            if (!eol) {
                fprintf(stderr, "manifest file does not end with newline\n");
                exit(1);
            }
            auto line = read_ptr_;
            read_ptr_ = eol + 1;

            auto eq = static_cast<const char*>(memchr(line, '=', eol - line));
            if (!eq) {
                fprintf(stderr, "manifest entry has no '=' separator: %.*s\n",
                        static_cast<int>(eol - line), line);
                exit(1);
            }

            line = AllowEntry(line, eq, eol);
            if (line) {
                std::string target(line, eq - line);
                std::string source(eq + 1, eol - (eq + 1));
                struct stat st;
                auto fd = opener->Open(source, &st);
                RequireRegularFile(st, source.c_str());
                auto file = FileContents::Map(fd, st, source.c_str());
                *value = value_type{prefix + target, std::move(file)};
                return true;
            }
        }
        return false;
    }

private:
    FileContents file_;
    const std::string prefix_;
    const GroupFilter* filter_ = nullptr;
    const char* read_ptr_ = nullptr;
    const char* eof_ = nullptr;

    // Returns the beginning of the `target=source` portion of the entry
    // if the entry is allowed by the filter, otherwise nullptr.
    const char* AllowEntry(const char* start, const char* eq, const char* eol) {
        if (*start != '{') {
            // This entry doesn't specify a group.
            return filter_->AllowsAll() ? start : nullptr;
        }
        auto end_group = static_cast<const char*>(
            memchr(start + 1, '}', eq - start));
        if (!end_group) {
            fprintf(stderr,
                    "manifest entry has '{' but no '}': %.*s\n",
                    static_cast<int>(eol - start), start);
            exit(1);
        }
        std::string group(start, end_group - start);
        return filter_->Allows(group) ? end_group + 1 : nullptr;
    }
};

class DirectoryInputFileGenerator : public InputFileGenerator {
public:
    DirectoryInputFileGenerator(fbl::unique_fd fd, const char* dirname) :
        source_prefix_(dirname) {
        walk_pos_.emplace_front(MakeUniqueDir(std::move(fd)), 0);
    }

    ~DirectoryInputFileGenerator() override = default;

    bool Next(FileOpener* opener, const std::string& prefix,
              value_type* value) override {
        do {
            const dirent* d = readdir(walk_pos_.front().dir.get());
            if (!d) {
                Ascend();
                continue;
            }
            if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, "..")) {
                continue;
            }
            std::string target = prefix + walk_prefix_ + d->d_name;
            std::string source = source_prefix_ + walk_prefix_ + d->d_name;
            struct stat st;
            auto fd = opener->Open(source, &st);
            if (S_ISDIR(st.st_mode)) {
                Descend(std::move(fd), d->d_name);
            } else {
                RequireRegularFile(st, source.c_str());
                auto file = FileContents::Map(std::move(fd), st,
                                              source.c_str());
                *value = value_type{std::move(target), std::move(file)};
                return true;
            }
        } while (!walk_pos_.empty());
        return false;
    }

private:
    // std::unique_ptr for fdopendir/closedir.
    static void DeleteUniqueDir(DIR* dir) {
        closedir(dir);
    }
    using UniqueDir = std::unique_ptr<DIR, decltype(&DeleteUniqueDir)>;
    UniqueDir MakeUniqueDir(fbl::unique_fd fd) {
        DIR* dir = fdopendir(fd.release());
        if (!dir) {
            perror("fdopendir");
            exit(1);
        }
        return UniqueDir(dir, &DeleteUniqueDir);
    }

    // State of our depth-first directory tree walk.
    struct WalkState {
        WalkState(UniqueDir d, size_t len) :
            dir(std::move(d)), parent_prefix_len(len) {
        }
        UniqueDir dir;
        size_t parent_prefix_len;
    };

    const std::string source_prefix_;
    std::forward_list<WalkState> walk_pos_;
    std::string walk_prefix_;

    void Descend(fbl::unique_fd fd, const char* name) {
        size_t parent = walk_prefix_.size();
        walk_prefix_ += name;
        walk_prefix_ += "/";
        walk_pos_.emplace_front(MakeUniqueDir(std::move(fd)), parent);
    }

    void Ascend() {
        walk_prefix_.resize(walk_pos_.front().parent_prefix_len);
        walk_pos_.pop_front();
    }
};

void DescribeHeader(uint32_t pos, uint32_t length, const char* type) {
    printf("%08x: %08x %s\n", pos, length, type);
}

class Item {
public:
    // Only the static methods below can create an Item.
    Item() = delete;

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Item);

    uint32_t type() const {
        return header_.type;
    }

    uint32_t PayloadSize() const {
        return header_.length;
    }

    uint32_t TotalSize() const {
        return sizeof(header_) + ZBI_ALIGN(PayloadSize());
    }

    void Describe(uint32_t pos) const {
        switch (header_.type) {
        case ZBI_TYPE_STORAGE_BOOTFS:
            printf("%08x: %08x BOOTFS (size=%08x)\n",
                   pos, header_.length, header_.extra);
            break;
        case ZBI_TYPE_STORAGE_RAMDISK:
            printf("%08x: %08x RAMDISK (size=%08x)\n",
                   pos, header_.length, header_.extra);
            break;

#define ZBI_CASE(type)                                         \
        case ZBI_TYPE_##type:                                  \
            DescribeHeader(pos, header_.length, #type);        \
            break
        ZBI_CASE(ACPI_RSDP);
        ZBI_CASE(CMDLINE);
        ZBI_CASE(CRASHLOG);
        ZBI_CASE(DEBUG_UART);
        ZBI_CASE(DISCARD);
        ZBI_CASE(E820_TABLE);
        ZBI_CASE(EFI_MEMORY_MAP);
        ZBI_CASE(EFI_SYSTEM_TABLE);
        ZBI_CASE(FRAMEBUFFER);
        ZBI_CASE(KERNEL_ARM64);
        ZBI_CASE(KERNEL_X64);
        ZBI_CASE(NVRAM_DEPRECATED);
        ZBI_CASE(NVRAM);
        ZBI_CASE(PLATFORM_ID);
#undef ZBI_CASE

        default:
            printf("%08x: %08x UNKNOWN (type=%08x)\n",
                   pos, header_.length, header_.type);
            break;
        }

        if (header_.flags & ZBI_FLAG_CRC32) {
            printf("        :          MAGIC=%08x CRC=%08x\n",
                   header_.magic, header_.crc32);

            Checksummer crc;
            for (const auto& chunk : payload_) {
                crc.Write(chunk);
            }
            zbi_header_t check_header = header_;
            crc.FinalizeHeader(&check_header);

            if (check_header.crc32 != header_.crc32) {
                fprintf(stderr, "error: CRC %08x does not match header\n",
                        check_header.crc32);
            }
        } else {
            printf("        :          MAGIC=%08x NO CRC\n", header_.magic);
        }
    }

    bool AlreadyCompressed() const {
        return (header_.flags & ZBI_FLAG_STORAGE_COMPRESSED) && !compress_;
    }

    void Show() const {
        if (header_.length > 0) {
            if (AlreadyCompressed()) {
                CreateFromCompressed(*this)->Show();
                return;
            }
            switch (header_.type) {
            case ZBI_TYPE_CMDLINE:
                ShowCmdline();
                break;
            }
        }
    }

    // Streaming exhausts the item's payload.  The OutputStream will now
    // have pointers into buffers owned by this Item, so this Item must be
    // kept alive until out->Flush() runs (while *this is alive, to be safe).
    void Stream(OutputStream* out) {
        assert(Aligned(out->WritePosition()));
        uint32_t wrote = compress_ ? StreamCompressed(out) : StreamRaw(out);
        assert(out->WritePosition() % ZBI_ALIGNMENT == wrote % ZBI_ALIGNMENT);
        uint32_t aligned = ZBI_ALIGN(wrote);
        if (aligned > wrote) {
            static const uint8_t padding[ZBI_ALIGNMENT]{};
            out->Write(Iovec(padding, aligned - wrote));
        }
        assert(Aligned(out->WritePosition()));
    }

    void StreamPayload(OutputStream* out) {
        if (AlreadyCompressed()) {
            CreateFromCompressed(*this)->StreamRawPayload(out);
        } else {
            StreamRawPayload(out);
        }
    }

    // The buffer will be released when this Item is destroyed.  This item
    // and items earlier on the list can hold pointers into the buffer.
    void OwnBuffer(std::unique_ptr<uint8_t[]> buffer) {
        buffers_.push_front(std::move(buffer));
    }
    void OwnFile(FileContents file) {
        files_.push_front(std::move(file));
    }

    // Create from in-core data.
    static std::unique_ptr<Item> CreateFromBuffer(
        uint32_t type, std::unique_ptr<uint8_t[]> payload, size_t size) {
        auto item = MakeItem(NewHeader(type, size));
        item->payload_.emplace_front(Iovec(payload.get(), size));
        item->OwnBuffer(std::move(payload));
        Checksummer crc;
        crc.Write(Iovec(payload.get(), size));
        crc.FinalizeHeader(&item->header_);
        return item;
    }

    // Create from local scratch data.
    template<typename T>
    static std::unique_ptr<Item> Create(uint32_t type, const T& payload) {
        auto buffer = std::make_unique<uint8_t[]>(sizeof(payload));
        memcpy(buffer.get(), &payload, sizeof(payload));
        return CreateFromBuffer(type, std::move(buffer), sizeof(payload));
    }

    // Create from raw file contents.
    static std::unique_ptr<Item> CreateFromFile(
        FileContents file, uint32_t type, bool compress) {
        bool null_terminate = type == ZBI_TYPE_CMDLINE;
        size_t size = file.exact_size() + (null_terminate ? 1 : 0);
        auto item = MakeItem(NewHeader(type, size), compress);

        // If we need some zeros, see if they're already right there
        // in the last mapped page past the exact end of the file.
        if (size <= file.mapped_size()) {
            // Use the padding that's already there.
            item->payload_.emplace_front(file.PageRoundedView(0, size));
        } else {
            // No space, so we need a separate padding buffer.
            if (null_terminate) {
                item->payload_.emplace_front(Iovec("", 1));
            }
            item->payload_.emplace_front(file.View(0, file.exact_size()));
        }

        if (!compress) {
            // Compute the checksum now so the item is ready to write out.
            Checksummer crc;
            crc.Write(file.View(0, file.exact_size()));
            if (null_terminate) {
                crc.Write(Iovec("", 1));
            }
            crc.FinalizeHeader(&item->header_);
        }

        // The item now owns the file mapping that its payload points into.
        item->OwnFile(std::move(file));

        return item;
    }

    // Create from an existing fully-baked item in an input file.
    static std::unique_ptr<Item> CreateFromItem(const FileContents& file,
                                                uint32_t offset) {
        if (offset > file.exact_size() ||
            file.exact_size() - offset < sizeof(zbi_header_t)) {
            fprintf(stderr, "input file too short for next header\n");
            exit(1);
        }
        const zbi_header_t* header = static_cast<const zbi_header_t*>(
            file.View(offset, sizeof(zbi_header_t)).iov_base);
        offset += sizeof(zbi_header_t);
        if (file.exact_size() - offset < header->length) {
            fprintf(stderr, "input file too short for payload of %u bytes\n",
                    header->length);
            exit(1);
        }
        auto item = MakeItem(*header);
        item->payload_.emplace_front(file.View(offset, header->length));
        return item;
    }

    // Create by decompressing a fully-baked item that is compressed.
    static std::unique_ptr<Item> CreateFromCompressed(const Item& compressed) {
        assert(compressed.header_.flags & ZBI_FLAG_STORAGE_COMPRESSED);
        assert(!compressed.compress_);
        auto item = MakeItem(compressed.header_);
        item->header_.flags &= ~ZBI_FLAG_STORAGE_COMPRESSED;
        item->header_.length = item->header_.extra;
        auto buffer = Decompress(compressed.payload_, item->header_.length);
        item->payload_.emplace_front(
            Iovec(buffer.get(), item->header_.length));
        item->OwnBuffer(std::move(buffer));
        return item;
    }

    // Create a BOOTFS item.
    static std::unique_ptr<Item> CreateBootFS(FileOpener* opener,
                                              InputFileGeneratorList input,
                                              const std::string& prefix,
                                              uint32_t type, bool compress) {
        auto item = MakeItem(NewHeader(type, 0), compress);

        // Collect the names and exact sizes here and the contents in payload_.
        struct Entry {
            std::string name;
            uint32_t data_len = 0;
        };
        std::deque<Entry> entries;
        size_t dirsize = 0, bodysize = 0;
        while (!input.empty()) {
            auto generator = std::move(input.front());
            input.pop_front();
            InputFileGenerator::value_type next;
            while (generator->Next(opener, prefix, &next)) {
                // Accumulate the space needed for each zbi_bootfs_dirent_t.
                dirsize += ZBI_BOOTFS_DIRENT_SIZE(next.target.size() + 1);
                Entry entry;
                entry.name.swap(next.target);
                entry.data_len = static_cast<uint32_t>(next.file.exact_size());
                if (entry.data_len != next.file.exact_size()) {
                    fprintf(stderr, "input file size exceeds format maximum\n");
                    exit(1);
                }
                uint32_t size = ZBI_BOOTFS_PAGE_ALIGN(entry.data_len);
                bodysize += size;
                item->payload_.emplace_back(next.file.PageRoundedView(0, size));
                entries.push_back(std::move(entry));
                item->OwnFile(std::move(next.file));
            }
        }

        // Now we can calculate the final sizes.
        const zbi_bootfs_header_t header = {
            ZBI_BOOTFS_MAGIC,               // magic
            static_cast<uint32_t>(dirsize), // dirsize
            0,                              // reserved0
            0,                              // reserved1
        };
        size_t header_size = ZBI_BOOTFS_PAGE_ALIGN(sizeof(header) + dirsize);
        item->header_.length = static_cast<uint32_t>(header_size + bodysize);
        if (item->header_.length != header_size + bodysize) {
            fprintf(stderr, "BOOTFS image size exceeds format maximum\n");
            exit(1);
        }

        // Now fill a buffer with the BOOTFS header and directory entries.
        AppendBuffer buffer(header_size);
        buffer.Append(&header);
        uint32_t data_off = static_cast<uint32_t>(header_size);
        for (const auto& file : item->payload_) {
            const auto& entry = entries.front();
            const zbi_bootfs_dirent_t entry_hdr = {
                static_cast<uint32_t>(entry.name.size() + 1), // name_len
                entry.data_len,                               // data_len
                data_off,                                     // data_off
            };
            data_off += static_cast<uint32_t>(file.iov_len);
            buffer.Append(&entry_hdr);
            buffer.Append(entry.name.c_str(), entry_hdr.name_len);
            buffer.Pad(
                ZBI_BOOTFS_DIRENT_SIZE(entry_hdr.name_len) -
                offsetof(zbi_bootfs_dirent_t, name[entry_hdr.name_len]));
            entries.pop_front();
        }
        assert(data_off == item->header_.length);
        // Zero fill to the end of the page.
        buffer.Pad(header_size - buffer.size());

        if (!compress) {
            // Checksum the BOOTFS image right now: header and then payload.
            Checksummer crc;
            crc.Write(buffer.get());
            for (const auto& file : item->payload_) {
                crc.Write(file);
            }
            crc.FinalizeHeader(&item->header_);
        }

        // Put the header at the front of the payload.
        item->payload_.emplace_front(buffer.get());
        item->OwnBuffer(buffer.release());

        return item;
    }

private:
    zbi_header_t header_;
    std::list<const iovec> payload_;
    // The payload_ items might point into these buffers.  They're just
    // stored here to own the buffers until the payload is exhausted.
    std::forward_list<FileContents> files_;
    std::forward_list<std::unique_ptr<uint8_t[]>> buffers_;
    const bool compress_;

    static constexpr zbi_header_t NewHeader(uint32_t type, uint32_t size) {
        return {
            type,                                   // type
            size,                                   // length
            0,                                      // extra
            ZBI_FLAG_VERSION | ZBI_FLAG_CRC32,      // flags
            0,                                      // reserved0
            0,                                      // reserved1
            ZBI_ITEM_MAGIC,                         // magic
            0,                                      // crc32
        };
    }

    Item(const zbi_header_t& header, bool compress) :
        header_(header), compress_(compress) {
        if (compress_) {
            // We'll compress and checksum on the way out.
            header_.flags |= ZBI_FLAG_STORAGE_COMPRESSED;
        }
    }

    static std::unique_ptr<Item> MakeItem(const zbi_header_t& header,
                                          bool compress = false) {
        return std::unique_ptr<Item>(new Item(header, compress));
    }

    void StreamRawPayload(OutputStream* out) {
        do {
            out->Write(payload_.front());
            payload_.pop_front();
        } while (!payload_.empty());
    }

    uint32_t StreamRaw(OutputStream* out) {
        // The header is already fully baked.
        out->Write(Iovec(&header_, sizeof(header_)));
        // The payload goes out as is.
        StreamRawPayload(out);
        return sizeof(header_) + header_.length;
    }

    uint32_t StreamCompressed(OutputStream* out) {
        // Compress and checksum the payload.
        Compressor compressor;
        compressor.Init(out, header_);
        do {
            // The compressor streams the header and compressed payload out.
            compressor.Write(out, payload_.front());
            payload_.pop_front();
        } while (!payload_.empty());
        // This writes the final header as well as the last of the payload.
        return compressor.Finish(out);
    }

    void ShowCmdline() const {
        std::string cmdline = std::accumulate(
            payload_.begin(), payload_.end(), std::string(),
            [](std::string cmdline, const iovec& iov) {
                return cmdline.append(
                    static_cast<const char*>(iov.iov_base),
                    iov.iov_len);
            });
        size_t start = 0;
        while (start < cmdline.size()) {
            size_t word_end = cmdline.find_first_of(" \t\r\n", start);
            if (word_end == std::string::npos) {
                if (cmdline[start] != '\0') {
                    printf("        : %s\n", cmdline.c_str() + start);
                }
                break;
            }
            if (word_end > start) {
                printf("        : %.*s\n",
                       static_cast<int>(word_end - start),
                       cmdline.c_str() + start);
            }
            start = word_end + 1;
        }
    }
};

using ItemList = std::vector<std::unique_ptr<Item>>;

bool ImportFile(const FileContents& file, const char* filename,
                ItemList* items) {
    if (file.exact_size() <= (sizeof(zbi_header_t) * 2)) {
        return false;
    }
    const zbi_header_t* header = static_cast<const zbi_header_t*>(
        file.View(0, sizeof(zbi_header_t)).iov_base);
    if (!(header->type == ZBI_TYPE_CONTAINER &&
          header->extra == ZBI_CONTAINER_MAGIC &&
          header->magic == ZBI_ITEM_MAGIC)) {
        return false;
    }
    size_t file_size = file.exact_size() - sizeof(zbi_header_t);
    if (file_size != header->length) {
        fprintf(stderr, "%s: header size doesn't match file size\n", filename);
        exit(1);
    }
    if (!Aligned(header->length)) {
        fprintf(stderr, "ZBI item misaligned\n");
        exit(1);
    }
    uint32_t pos = sizeof(zbi_header_t);
    do {
        auto item = Item::CreateFromItem(file, pos);
        pos += item->TotalSize();
        items->push_back(std::move(item));
    } while (pos < file.exact_size());
    return true;
}

const uint32_t kImageArchUndefined = ZBI_TYPE_DISCARD;

// Returns nullptr if complete, else an explanatory string.
const char* IncompleteImage(const ItemList& items, const uint32_t image_arch) {
    if (!ZBI_IS_KERNEL_BOOTITEM(items.front()->type())) {
        return "first item not KERNEL";
    }

    if (items.front()->type() != image_arch &&
        image_arch != kImageArchUndefined) {
        return "kernel arch mismatch";
    }

    auto count =
        std::count_if(items.begin(), items.end(),
                      [](const std::unique_ptr<Item>& item) {
                          return item->type() == ZBI_TYPE_STORAGE_BOOTFS;
                      });
    if (count == 0) {
        return "no /boot BOOTFS item";
    }
    if (count > 1) {
        return "multiple BOOTFS items";
    }
    return nullptr;
}

constexpr const char kOptString[] = "-B:cd:X:R:g:hto:p:T:uv";
constexpr const option kLongOpts[] = {
    {"complete", required_argument, nullptr, 'B'},
    {"compressed", no_argument, nullptr, 'c'},
    {"depfile", required_argument, nullptr, 'd'},
    {"extract-item", required_argument, nullptr, 'X'},
    {"extract-raw", required_argument, nullptr, 'R'},
    {"groups", required_argument, nullptr, 'g'},
    {"help", no_argument, nullptr, 'h'},
    {"list", no_argument, nullptr, 't'},
    {"output", required_argument, nullptr, 'o'},
    {"prefix", required_argument, nullptr, 'p'},
    {"target", required_argument, nullptr, 'T'},
    {"uncompressed", no_argument, nullptr, 'u'},
    {"verbose", no_argument, nullptr, 'v'},
    {nullptr, no_argument, nullptr, 0},
};

void usage(const char* progname) {
    fprintf(stderr, "\
Usage: %s {--output=FILE | -o FILE} [--depfile=FILE | -d FILE] ...\n\
       %s {--list | -t} ...\n\
       %s {--extract-item=N | -X N} ...\n\
       %s {--extract-raw=N | -R N} ...\n\
\n\
Remaining arguments are interpersed switches and input files:\n\
    --help, -h                     print this message\n\
    --output=FILE, -o FILE         output file name\n\
    --depfile=FILE, -d FILE        makefile dependency output file name\n\
    --list, -t                     list input ZBI item headers\n\
    --verbose, -v                  show item contents\n\
    --complete=ARCH, -B ARCH       verify result is a complete boot image\n\
    --groups=GROUPS, -g GROUPS     comma-separated list of manifest groups\n\
    --compressed, -c               compress BOOTFS/RAMDISK images (default)\n\
    --uncompressed, -u             do not compress BOOTFS/RAMDISK images\n\
    --target=boot, -T boot         BOOTFS to be unpacked at /boot (default)\n\
    --target=cmdline, -T cmdline   input files are kernel command line text\n\
    --target=ramdisk, -T ramdisk   input files are raw RAMDISK images\n\
    --target=zbi, -T zbi           input files must be ZBI files\n\
    --prefix=PREFIX, -p PREFIX     prepend PREFIX/ to target file names\n\
    FILE                           input or manifest file\n\
    DIRECTORY                      directory tree goes into BOOTFS at PREFIX/\n\
\n\
Each `--target` or `-T` switch affects subsequent FILE arguments.\n\
With `--target=boot` (or no switch), files with ZBI_TYPE_CONTAINER headers\n\
are incomplete boot files; other files are taken to be manifest files.\n\
Each DIRECTORY is listed recursively and handled just like a manifest file\n\
using the path relative to DIRECTORY as the target name (before any PREFIX).\n\
Each `--group`, `--prefix`, `-g`, or `-p` switch affects each file from a\n\
manifest or directory in subsequent FILE or DIRECTORY arguments.\n\
All the BOOTFS files from all manifest entries and directories go into a\n\
single BOOTFS item and only the last `--compressed` or `--uncompressed`\n\
switch affects that, but `--target=ramdisk` input files are affected by\n\
the most recently preceding `--compressed` or `--uncompressed` switch.\n\
\n\
With `--extract-item` or `-X`, skip the first N items and then write out the\n\
next item alone into a fresh ZBI file.\n\
With `--extract-raw` or `-R`, write decompressed payload with no header.\n\
",
            progname, progname, progname, progname);
}

}  // anonymous namespace

int main(int argc, char** argv) {
    FileOpener opener;
    GroupFilter filter;
    const char* outfile = nullptr;
    const char* depfile = nullptr;
    uint32_t complete_arch = kImageArchUndefined;
    uint32_t target = ZBI_TYPE_STORAGE_BOOTFS;
    bool compressed = true;
    bool list_contents = false;
    int extract_item = -1, extract_raw = -1;
    bool verbose = false;
    ItemList items;
    InputFileGeneratorList bootfs_input;
    std::string prefix;

    int opt;
    while ((opt = getopt_long(argc, argv,
                              kOptString, kLongOpts, nullptr)) != -1) {
        // A non-option argument (1) is an input, handled below.
        // All other cases continue the loop and don't break the switch.
        switch (opt) {
        case 1:
            break;

        case 'o':
            if (outfile) {
                fprintf(stderr, "only one output file\n");
                exit(1);
            }
            if (!items.empty()) {
                fprintf(stderr, "--output or -o must precede inputs\n");
                exit(1);
            }
            outfile = optarg;
            continue;

        case 'd':
            if (depfile) {
                fprintf(stderr, "only one depfile\n");
                exit(1);
            }
            if (!outfile) {
                fprintf(stderr,
                        "--output -or -o must precede --depfile or -d\n");
                exit(1);
            }
            if (!items.empty()) {
                fprintf(stderr, "--depfile or -d must precede inputs\n");
                exit(1);
            }
            opener.Init(outfile, depfile);
            continue;

        case 'T':
            if (!strcmp(optarg, "boot")) {
                target = ZBI_TYPE_STORAGE_BOOTFS;
            } else if (!strcmp(optarg, "cmdline")) {
                target = ZBI_TYPE_CMDLINE;
            } else if (!strcmp(optarg, "ramdisk")) {
                target = ZBI_TYPE_STORAGE_RAMDISK;
            } else if (!strcmp(optarg, "zbi")) {
                target = ZBI_TYPE_CONTAINER;
            } else {
                fprintf(stderr, "\
--target requires boot, system, ramdisk, or zbi\n");
                exit(1);
            }
            continue;

        case 'p':
            // A nonempty prefix should have no leading slashes and
            // exactly one trailing slash.
            prefix = optarg;
            while (!prefix.empty() && prefix.front() == '/') {
                prefix.erase(0, 1);
            }
            if (!prefix.empty() && prefix.back() == '/') {
                prefix.pop_back();
            }
            if (prefix.empty() && optarg[0] != '\0') {
                fprintf(stderr, "\
--prefix cannot be /; use --prefix= (empty) instead\n");
                exit(1);
            }
            if (!prefix.empty()) {
                prefix.push_back('/');
            }
            continue;

        case 'g':
            filter.SetFilter(optarg);
            continue;

        case 't':
            list_contents = true;
            continue;

        case 'v':
            verbose = true;
            continue;

        case 'B':
            if (!strcmp(optarg, "x64")) {
                complete_arch = ZBI_TYPE_KERNEL_X64;
            } else if (!strcmp(optarg, "arm64")) {
                complete_arch = ZBI_TYPE_KERNEL_ARM64;
            } else {
                fprintf(stderr, "--complete architecture argument must be one"
                        " of: x64, arm64\n");
                exit(1);
            }
            continue;
        case 'c':
            compressed = true;
            continue;

        case 'u':
            compressed = false;
            continue;

        case 'X':
            if (extract_item != -1 || extract_raw != -1) {
                fprintf(stderr,
                        "only one --extract-item, --extract-raw, -X, or -R\n");
                exit(1);
            }
            extract_item = atoi(optarg);
            if (extract_item < 0) {
                fprintf(stderr, "item number must be nonnegative\n");
                exit(1);
            }
            continue;

        case 'R':
            if (extract_item != -1 || extract_raw != -1) {
                fprintf(stderr,
                        "only one --extract-item, --extract-raw, -X, or -R\n");
                exit(1);
            }
            extract_raw = atoi(optarg);
            if (extract_raw < 0) {
                fprintf(stderr, "item number must be nonnegative\n");
                exit(1);
            }
            continue;

        case 'h':
        default:
            usage(argv[0]);
            exit(opt == 'h' ? 0 : 1);
        }
        assert(opt == 1);

        struct stat st;
        auto fd = opener.Open(optarg, &st);

        // A directory populates the BOOTFS according to --prefix.
        if (target == ZBI_TYPE_STORAGE_BOOTFS && S_ISDIR(st.st_mode)) {
            bootfs_input.emplace_back(
                new DirectoryInputFileGenerator(std::move(fd), optarg));
            continue;
        }

        // Anything else must be a regular file.
        RequireRegularFile(st, optarg);
        auto file = FileContents::Map(std::move(fd), st, optarg);

        if (target == ZBI_TYPE_STORAGE_RAMDISK) {
            // Under --target=ramdisk, any input file is a raw image.
            items.push_back(Item::CreateFromFile(std::move(file),
                                                 ZBI_TYPE_STORAGE_RAMDISK,
                                                 compressed));
        } else if (target == ZBI_TYPE_CMDLINE) {
            // Under --target=cmdline, any input file is a cmdline fragment.
            items.push_back(Item::CreateFromFile(std::move(file),
                                                 ZBI_TYPE_CMDLINE,
                                                 false));
        } else if (ImportFile(file, optarg, &items)) {
            // It's another file in ZBI format.  The last item will own
            // the file buffer, so it lives until all earlier items are
            // exhausted.
            items.back()->OwnFile(std::move(file));
        } else if (target == ZBI_TYPE_CONTAINER) {
            fprintf(stderr, "%s: not a Zircon Boot container\n", optarg);
            exit(1);
        } else {
            // It must be a manifest file.
            bootfs_input.emplace_back(
                new ManifestInputFileGenerator(std::move(file),
                                               prefix, &filter));
        }
    }

    if (list_contents) {
        if (outfile || depfile) {
            fprintf(stderr, "no output file or depfile with --list or -t\n");
            exit(1);
        }
    } else {
        if (!outfile && extract_item == -1 && extract_raw == -1) {
            fprintf(stderr, "no output file\n");
            exit(1);
        }
    }

    if (!bootfs_input.empty()) {
        // Pack up the BOOTFS.
        items.emplace_back(
            Item::CreateBootFS(&opener, std::move(bootfs_input),
                               std::move(prefix), target, compressed));
    }

    if (items.empty()) {
        fprintf(stderr, "no inputs\n");
        exit(1);
    }

    if (!list_contents && complete_arch != kImageArchUndefined) {
        // The only hard requirement is that the kernel be first.
        // But it seems most orderly to put the BOOTFS second,
        // other storage in the middle, and CMDLINE last.
        std::stable_sort(
            items.begin(), items.end(),
            [](const std::unique_ptr<Item>& a,
               const std::unique_ptr<Item>& b) {
                auto item_rank = [](uint32_t type) {
                    return (ZBI_IS_KERNEL_BOOTITEM(type) ? 0 :
                            type == ZBI_TYPE_STORAGE_BOOTFS ? 1 :
                            type == ZBI_TYPE_CMDLINE ? 9 :
                            5);
                };
                return item_rank(a->type()) < item_rank(b->type());
            });
    }

    if (complete_arch != kImageArchUndefined) {
        const char* incomplete = IncompleteImage(items, complete_arch);
        if (incomplete) {
            fprintf(stderr, "incomplete image: %s\n", incomplete);
            exit(1);
        }
    }

    if (list_contents || verbose) {
        const char* incomplete = IncompleteImage(items, complete_arch);
        if (incomplete) {
            printf("INCOMPLETE: %s\n", incomplete);
        } else {
            puts("COMPLETE: bootable image");
        }
        // Contents start after the ZBI_TYPE_CONTAINER header.
        uint32_t pos = sizeof(zbi_header_t);
        for (const auto& item : items) {
            item->Describe(pos);
            pos += item->TotalSize();
            if (verbose) {
                item->Show();
            }
        }
    }

    auto extract_index = std::max(extract_item, extract_raw);
    if (extract_index != -1 &&
        static_cast<size_t>(extract_index) >= items.size()) {
        fprintf(stderr, "cannot extract item %d of %zu\n",
                std::max(extract_item, extract_raw), items.size());
        exit(1);
    }

    fbl::unique_fd outfd;
    if (outfile) {
        outfd.reset(open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0666));
        if (!outfd) {
            perror(outfile);
            exit(1);
        }
    } else if (extract_index != -1) {
        outfd.reset(STDOUT_FILENO);
    }

    if (outfd) {
        OutputStream out(std::move(outfd));
        if (extract_raw == -1) {
            uint32_t header_start = out.PlaceHeader();
            uint32_t payload_start = out.WritePosition();
            assert(Aligned(payload_start));
            if (extract_item == -1) {
                for (const auto& item : items) {
                    // The OutputStream stores pointers into Item buffers in
                    // its write queue until it goes out of scope below.
                    // The ItemList keeps all the items alive past then.
                    item->Stream(&out);
                }
            } else {
                items[extract_item]->Stream(&out);
            }
            const zbi_header_t header = {
                ZBI_TYPE_CONTAINER,                               // type
                out.WritePosition() - payload_start,              // length
                ZBI_CONTAINER_MAGIC,                              // extra
                ZBI_FLAG_VERSION,                                 // flags
                0,                                                // reserved0
                0,                                                // reserved1
                ZBI_ITEM_MAGIC,                                   // magic
                ZBI_ITEM_NO_CRC32,                                // crc32
            };
            assert(Aligned(header.length));
            out.PatchHeader(header, header_start);
        } else {
            items[extract_raw]->StreamPayload(&out);
        }
    }

    return 0;
}

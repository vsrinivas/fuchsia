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

#include <fbl/macros.h>
#include <fbl/unique_fd.h>
#include <lib/cksum.h>
#include <lz4/lz4frame.h>
#include <zircon/boot/bootdata.h>

namespace {

bool Aligned(uint32_t length) {
    return BOOTDATA_ALIGN(length) == length;
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
        if (buffer.iov_len + total_ > UINT32_MAX - sizeof(bootdata_t) + 1) {
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

    // Take ownership of the Item after it has called this->Write
    // repeatedly, using pointers into buffers owned by the Item.
    void OwnItem(std::unique_ptr<Item> item) {
        if (Buffering()) {
            // Keep the Item alive as long as we might be buffering pointers
            // into memory it owns.
            owned_items_.push_front(std::move(item));
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
        owned_items_.clear();
    }

    // Emit a placeholder.  The return value will be passed to PatchHeader.
    uint32_t PlaceHeader() {
        uint32_t pos = WritePosition();
        static const bootdata_t dummy = {};
        Write(Iovec(&dummy));
        return pos;
    }

    // Replace a placeholder with a real header.
    void PatchHeader(const bootdata_t& header, uint32_t place) {
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
    std::forward_list<std::unique_ptr<Item>> owned_items_;
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
        assert(static_cast<off_t>(flushed_) == lseek(fd_.get(), 0, SEEK_CUR));
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

    void FinalizeHeader(bootdata_t* header) {
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

    void Init(OutputStream* out, const bootdata_t& header) {
        header_ = header;
        assert(header_.flags & BOOTDATA_BOOTFS_FLAG_COMPRESSED);
        assert(header_.flags & BOOTDATA_FLAG_CRC32);

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
    bootdata_t header_;
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

#undef LZ4F_CALL
};

const size_t Compressor::kMinBufferSize;

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
            assert(pagesize >= BOOTFS_PAGE_SIZE);
            assert(pagesize % BOOTFS_PAGE_SIZE == 0);
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
    virtual bool Next(FileOpener*, const GroupFilter&, value_type*) = 0;
};

class ManifestInputFileGenerator : public InputFileGenerator {
public:
    explicit ManifestInputFileGenerator(FileContents file) :
        file_(std::move(file)) {
        read_ptr_ = static_cast<const char*>(
            file_.View(0, file_.exact_size()).iov_base);
        eof_ = read_ptr_ + file_.exact_size();
    }

    ~ManifestInputFileGenerator() override = default;

    bool Next(FileOpener* opener, const GroupFilter& filter,
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

            line = AllowEntry(filter, line, eq, eol);
            if (line) {
                std::string target(line, eq - line);
                std::string source(eq + 1, eol - (eq + 1));
                struct stat st;
                auto fd = opener->Open(source, &st);
                RequireRegularFile(st, source.c_str());
                auto file = FileContents::Map(fd, st, source.c_str());
                *value = value_type{std::move(target), std::move(file)};
                return true;
            }
        }
        return false;
    }

private:
    FileContents file_;
    const char* read_ptr_ = nullptr;
    const char* eof_ = nullptr;

    // Returns the beginning of the `target=source` portion of the entry
    // if the entry is allowed by the filter, otherwise nullptr.
    static const char* AllowEntry(const GroupFilter& filter, const char* start,
                                  const char* eq, const char* eol) {
        if (*start != '{') {
            // This entry doesn't specify a group.
            return filter.AllowsAll() ? start : nullptr;
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
        return filter.Allows(group) ? end_group + 1 : nullptr;
    }
};

class DirectoryInputFileGenerator : public InputFileGenerator {
public:
    DirectoryInputFileGenerator(fbl::unique_fd fd, const char* dirname) :
        source_prefix_(dirname) {
        walk_pos_.emplace_front(MakeUniqueDir(std::move(fd)), 0);
    }

    ~DirectoryInputFileGenerator() override = default;

    bool Next(FileOpener* opener, const GroupFilter&,
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
            std::string target = walk_prefix_ + d->d_name;
            std::string source = source_prefix_ + target;
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
        return sizeof(header_) + BOOTDATA_ALIGN(PayloadSize());
    }

    void Describe(uint32_t pos) const {
        switch (header_.type) {
        case BOOTDATA_BOOTFS_BOOT:
            printf("%08x: %08x BOOTFS @/boot (size=%08x)\n",
                   pos, header_.length, header_.extra);
            break;
        case BOOTDATA_BOOTFS_SYSTEM:
            printf("%08x: %08x BOOTFS @/system (size=%08x)\n",
                   pos, header_.length, header_.extra);
            break;
        case BOOTDATA_RAMDISK:
            printf("%08x: %08x RAMDISK (size=%08x)\n",
                   pos, header_.length, header_.extra);
            break;

#define BOOTDATA_CASE(type)                                         \
        case BOOTDATA_##type:                                       \
            DescribeHeader(pos, header_.length, #type);             \
            break
        BOOTDATA_CASE(KERNEL);
        BOOTDATA_CASE(CMDLINE);
        BOOTDATA_CASE(ACPI_RSDP);
        BOOTDATA_CASE(FRAMEBUFFER);
        BOOTDATA_CASE(DEBUG_UART);
        BOOTDATA_CASE(PLATFORM_ID);
        BOOTDATA_CASE(LASTLOG_NVRAM);
        BOOTDATA_CASE(LASTLOG_NVRAM2);
        BOOTDATA_CASE(E820_TABLE);
        BOOTDATA_CASE(EFI_MEMORY_MAP);
        BOOTDATA_CASE(EFI_SYSTEM_TABLE);
        BOOTDATA_CASE(LAST_CRASHLOG);
        BOOTDATA_CASE(IGNORE);
#undef BOOTDATA_CASE

        default:
            printf("%08x: %08x UNKNOWN (type=%08x)\n",
                   pos, header_.length, header_.type);
            break;
        }

        if (header_.flags & BOOTDATA_FLAG_CRC32) {
            printf("        :          MAGIC=%08x CRC=%08x\n",
                   header_.magic, header_.crc32);

            Checksummer crc;
            for (const auto& chunk : payload_) {
                crc.Write(chunk);
            }
            bootdata_t check_header = header_;
            crc.FinalizeHeader(&check_header);

            if (check_header.crc32 != header_.crc32) {
                fprintf(stderr, "error: CRC %08x does not match header\n",
                        check_header.crc32);
            }
        } else {
            printf("        :          MAGIC=%08x NO CRC\n", header_.magic);
        }
    }

    // Streaming exhausts the item's payload.  The OutputStream will now
    // have pointers into buffers owned by this Item, so this Item must be
    // kept alive until out->Flush() runs (e.g. via OutputStream::OwnItem).
    void Stream(OutputStream* out) {
        assert(Aligned(out->WritePosition()));
        uint32_t wrote = compress_ ? StreamCompressed(out) : StreamRaw(out);
        assert(out->WritePosition() % BOOTDATA_ALIGN(1) ==
               wrote % BOOTDATA_ALIGN(1));
        uint32_t aligned = BOOTDATA_ALIGN(wrote);
        if (aligned > wrote) {
            static const uint8_t padding[BOOTDATA_ALIGN(1)]{};
            out->Write(Iovec(padding, aligned - wrote));
        }
        assert(Aligned(out->WritePosition()));
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
        FileContents file, uint32_t type, bool compress, bool null_terminate) {
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
        }

        // The item now owns the file mapping that its payload points into.
        item->OwnFile(std::move(file));

        return item;
    }

    // Create from an existing fully-baked item in an input file.
    static std::unique_ptr<Item> CreateFromItem(const FileContents& file,
                                                uint32_t offset) {
        if (offset > file.exact_size() ||
            file.exact_size() - offset < sizeof(bootdata_t)) {
            fprintf(stderr, "input file too short for next header\n");
            exit(1);
        }
        const bootdata_t* header = static_cast<const bootdata_t*>(
            file.View(offset, sizeof(bootdata_t)).iov_base);
        offset += sizeof(bootdata_t);
        if (file.exact_size() - offset < header->length) {
            fprintf(stderr, "input file too short for payload of %u bytes\n",
                    header->length);
            exit(1);
        }
        auto item = MakeItem(*header);
        item->payload_.emplace_front(file.View(offset, header->length));
        return item;
    }

    // Create a BOOTFS item.
    static std::unique_ptr<Item> CreateBootFS(FileOpener* opener,
                                              const GroupFilter& filter,
                                              InputFileGenerator* files,
                                              uint32_t type, bool compress) {
        auto item = MakeItem(NewHeader(type, 0), compress);

        // Collect the names and exact sizes here and the contents in payload_.
        struct Entry {
            std::string name;
            uint32_t data_len = 0;
        };
        std::deque<Entry> entries;
        size_t dirsize = 0, bodysize = 0;
        InputFileGenerator::value_type next;
        while (files->Next(opener, filter, &next)) {
            // Accumulate the space needed for each bootfs_entry_t.
            dirsize += (sizeof(bootfs_entry_t) +
                        BOOTFS_ALIGN(next.target.size() + 1));
            Entry entry;
            entry.name.swap(next.target);
            entry.data_len = static_cast<uint32_t>(next.file.exact_size());
            if (entry.data_len != next.file.exact_size()) {
                fprintf(stderr, "input file size exceeds format maximum\n");
                exit(1);
            }
            uint32_t size = BOOTFS_PAGE_ALIGN(entry.data_len);
            bodysize += size;
            item->payload_.emplace_back(next.file.PageRoundedView(0, size));
            entries.push_back(std::move(entry));
            item->OwnFile(std::move(next.file));
        }

        // Now we can calculate the final sizes.
        const bootfs_header_t header = {
            BOOTFS_MAGIC,                   // magic
            static_cast<uint32_t>(dirsize), // dirsize
            0,                              // reserved0
            0,                              // reserved1
        };
        size_t header_size = BOOTFS_PAGE_ALIGN(sizeof(header) + dirsize);
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
            const bootfs_entry_t entry_hdr = {
                static_cast<uint32_t>(entry.name.size() + 1), // name_len
                entry.data_len,                               // data_len
                data_off,                                     // data_off
            };
            data_off += static_cast<uint32_t>(file.iov_len);
            buffer.Append(&entry_hdr);
            buffer.Append(entry.name.c_str(), entry_hdr.name_len);
            buffer.Pad(BOOTFS_ALIGN(entry_hdr.name_len) - entry_hdr.name_len);
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

    // Create a BOOTFS item from a directory tree.
    static std::unique_ptr<Item> ImportDirectory(
        FileOpener* opener, const char* dirname,
        uint32_t type, bool compress) {
        DirectoryInputFileGenerator files(opener->Open(dirname), dirname);
        return CreateBootFS(opener, GroupFilter(), &files, type, compress);
    }

    // Create a BOOTFS item from a manifest file.
    static std::unique_ptr<Item> ImportManifest(
        FileOpener* opener, FileContents file, const GroupFilter& filter,
        uint32_t type, bool compress) {
        ManifestInputFileGenerator files(std::move(file));
        return CreateBootFS(opener, filter, &files, type, compress);
    }

private:
    bootdata_t header_;
    std::list<const iovec> payload_;
    // The payload_ items might point into these buffers.  They're just
    // stored here to own the buffers until the payload is exhausted.
    std::forward_list<FileContents> files_;
    std::forward_list<std::unique_ptr<uint8_t[]>> buffers_;
    const bool compress_;

    static constexpr bootdata_t NewHeader(uint32_t type, uint32_t size) {
        return {
            type,                                   // type
            size,                                   // length
            0,                                      // extra
            BOOTDATA_FLAG_V2 | BOOTDATA_FLAG_CRC32, // flags
            0,                                      // reserved0
            0,                                      // reserved1
            BOOTITEM_MAGIC,                         // magic
            0,                                      // crc32
        };
    }

    Item(const bootdata_t& header, bool compress) :
        header_(header), compress_(compress) {
        if (compress_) {
            // We'll compress and checksum on the way out.
            header_.flags |= BOOTDATA_BOOTFS_FLAG_COMPRESSED;
        }
    }

    static std::unique_ptr<Item> MakeItem(const bootdata_t& header,
                                          bool compress = false) {
        return std::unique_ptr<Item>(new Item(header, compress));
    }

    uint32_t StreamRaw(OutputStream* out) {
        // The header is already fully baked.
        out->Write(Iovec(&header_, sizeof(header_)));
        // The payload goes out as is.
        do {
            out->Write(payload_.front());
            payload_.pop_front();
        } while (!payload_.empty());
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
};

bool ImportFile(const FileContents& file, const char* filename,
                std::list<std::unique_ptr<Item>>* items) {
    if (file.exact_size() <= (sizeof(bootdata_t) * 2)) {
        return false;
    }
    const bootdata_t* header = static_cast<const bootdata_t*>(
        file.View(0, sizeof(bootdata_t)).iov_base);
    if (!(header->type == BOOTDATA_CONTAINER &&
          header->extra == BOOTDATA_MAGIC &&
          header->magic == BOOTITEM_MAGIC)) {
        return false;
    }
    size_t file_size = file.exact_size() - sizeof(bootdata_t);
    if (file_size != header->length) {
        fprintf(stderr, "%s: header size doesn't match file size\n", filename);
        exit(1);
    }
    if (!Aligned(header->length)) {
        fprintf(stderr, "bootdata misaligned\n");
        exit(1);
    }
    uint32_t pos = sizeof(bootdata_t);
    do {
        auto item = Item::CreateFromItem(file, pos);
        pos += item->TotalSize();
        items->push_back(std::move(item));
    } while (pos < file.exact_size());
    return true;
}

// Returns nullptr if complete, else an explanatory string.
const char* IncompleteImage(const std::list<std::unique_ptr<Item>>& items) {
    if (items.front()->type() != BOOTDATA_KERNEL) {
        return "first item not KERNEL";
    }
    if (std::none_of(items.begin(), items.end(),
                     [](const std::unique_ptr<Item>& item) {
                         return item->type() == BOOTDATA_BOOTFS_BOOT;
                     })) {
        return "no /boot BOOTFS item";
    }
    return nullptr;
}

constexpr const char kOptString[] = "-ho:d:T:g:tBcuC:";
constexpr const option kLongOpts[] = {
    {"help", no_argument, nullptr, 'h'},
    {"output", required_argument, nullptr, 'o'},
    {"depfile", required_argument, nullptr, 'd'},
    {"target", required_argument, nullptr, 'T'},
    {"groups", required_argument, nullptr, 'g'},
    {"list", no_argument, nullptr, 't'},
    {"complete", no_argument, nullptr, 'B'},
    {"compressed", no_argument, nullptr, 'c'},
    {"uncompressed", no_argument, nullptr, 'u'},
    {"cmdline", required_argument, nullptr, 'C'},
    {nullptr, no_argument, nullptr, 0},
};

void usage(const char* progname) {
    fprintf(stderr, "\
Usage: %s {--output=FILE | -o FILE} [--depfile=FILE | -d FILE] ...\n\
       %s {--list | -t} ...\n\
\n\
Remaining arguments are interpersed switches and input files:\n\
    --help, -h                     print this message\n\
    --output=FILE, -o FILE         output file name\n\
    --depfile=FILE, -d FILE        makefile dependency output file name\n\
    --list, -t                     list input BOOTDATA item headers\n\
    --complete, -B                 verify result is a complete boot image\n\
    --groups=GROUPS, -g GROUPS     comma-separated list of manifest groups\n\
    --compressed, -c               compress BOOTFS/RAMDISK images (default)\n\
    --uncompressed, -u             do not compress BOOTFS/RAMDISK images\n\
    --target=boot, -T boot         BOOTFS to be unpacked at /boot (default)\n\
    --target=system, -T system     BOOTFS to be unpacked at /system\n\
    --target=ramdisk, -T ramdisk   input files are raw RAMDISK images\n\
    --target=zbi, -T zbi           input files must be BOOTDATA files\n\
    @DIRECTORY                     populate BOOTFS from DIRECTORY\n\
    FILE                           read BOOTDATA file or BOOTFS manifest\n\
\n\
Each manifest or directory populates a distinct BOOTFS item, tagged for\n\
unpacking based on the most recent `--target` switch.  Files with\n\
BOOTDATA_CONTAINER headers are incomplete boot files; others are manifests.\n\
",
            progname, progname);
}

}  // anonymous namespace

int main(int argc, char** argv) {
    FileOpener opener;
    GroupFilter filter;
    const char* outfile = nullptr;
    const char* depfile = nullptr;
    uint32_t target = BOOTDATA_BOOTFS_BOOT;
    bool compressed = true;
    bool list_contents = false;
    bool verify_complete = false;
    std::list<std::unique_ptr<Item>> items;

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

        case 'R':
            target = BOOTDATA_RAMDISK;
            continue;

        case 'T':
            if (!strcmp(optarg, "boot")) {
                target = BOOTDATA_BOOTFS_BOOT;
            } else if (!strcmp(optarg, "system")) {
                target = BOOTDATA_BOOTFS_SYSTEM;
            } else if (!strcmp(optarg, "ramdisk")) {
                target = BOOTDATA_RAMDISK;
            } else if (!strcmp(optarg, "zbi")) {
                target = BOOTDATA_CONTAINER;
            } else {
                fprintf(stderr, "\
--target requires boot, system, ramdisk, or zbi\n");
                exit(1);
            }
            continue;

        case 'g':
            filter.SetFilter(optarg);
            continue;

        case 't':
            list_contents = true;
            continue;

        case 'B':
            verify_complete = true;
            continue;

        case 'c':
            compressed = true;
            continue;

        case 'u':
            compressed = false;
            continue;

        case 'C': {
            struct stat st;
            auto fd = opener.Open(optarg, &st);
            RequireRegularFile(st, optarg);
            auto file = FileContents::Map(std::move(fd), st, optarg);
            items.push_back(
                Item::CreateFromFile(std::move(file), BOOTDATA_CMDLINE,
                                     false, true));
            continue;
        }

        case 'h':
        default:
            usage(argv[0]);
            exit(opt == 'h' ? 0 : 1);
        }
        assert(opt == 1);

        if (optarg[0] == '@') {
            if (target == BOOTDATA_RAMDISK) {
                fprintf(stderr,
                        "%s: can't import directory to --target=ramdisk\n",
                        &optarg[1]);
                exit(1);
            }
            items.push_back(Item::ImportDirectory(&opener, &optarg[1],
                                                  target, compressed));
        } else {
            struct stat st;
            auto fd = opener.Open(optarg, &st);
            RequireRegularFile(st, optarg);
            auto file = FileContents::Map(std::move(fd), st, optarg);
            if (target == BOOTDATA_RAMDISK) {
                // Under --target=ramdisk, any input file is a raw image.
                items.push_back(Item::CreateFromFile(
                                    std::move(file), BOOTDATA_RAMDISK,
                                    compressed, false));
            } else if (ImportFile(file, optarg, &items)) {
                // It's another file in BOOTDATA format.  The last
                // item will own the file buffer, so it lives until
                // all earlier items are exhausted.
                items.back()->OwnFile(std::move(file));
            } else if (target == BOOTDATA_CONTAINER) {
                fprintf(stderr, "%s: not a Zircon Boot container\n", optarg);
                exit(1);
            } else {
                // It must be a manifest file.
                items.push_back(Item::ImportManifest(
                                    &opener, std::move(file),
                                    filter, target, compressed));
            }
        }
    }

    if (items.empty()) {
        fprintf(stderr, "no inputs\n");
        exit(1);
    }

    if (verify_complete) {
        const char* incomplete = IncompleteImage(items);
        if (incomplete) {
            fprintf(stderr, "incomplete image: %s\n", incomplete);
            exit(1);
        }
    }

    if (list_contents) {
        if (outfile || depfile) {
            fprintf(stderr, "no output file or depfile with --list or -t\n");
            exit(1);
        }
        const char* incomplete = IncompleteImage(items);
        if (incomplete) {
            printf("INCOMPLETE: %s\n", incomplete);
        } else {
            puts("COMPLETE: bootable image");
        }
        // Contents start after the BOOTDATA_CONTAINER header.
        uint32_t pos = sizeof(bootdata_t);
        do {
            const auto& item = items.front();
            item->Describe(pos);
            pos += item->TotalSize();
            items.pop_front();
        } while (!items.empty());
    } else {
        if (!outfile) {
            fprintf(stderr, "no output file\n");
            exit(1);
        }
        fbl::unique_fd fd(open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0666));
        if (!fd) {
            perror(outfile);
            exit(1);
        }
        OutputStream out(std::move(fd));
        uint32_t header_start = out.PlaceHeader();
        uint32_t payload_start = out.WritePosition();
        assert(Aligned(payload_start));
        do {
            // Pop an item off and stream it out.  Then transfer the
            // item's ownership to the OutputStream while it stores
            // pointers into Item buffers in its write queue.
            auto item = std::move(items.front());
            items.pop_front();
            item->Stream(&out);
            out.OwnItem(std::move(item));
        } while (!items.empty());
        const bootdata_t header = {
            BOOTDATA_CONTAINER,                               // type
            out.WritePosition() - payload_start,              // length
            BOOTDATA_MAGIC,                                   // extra
            BOOTDATA_FLAG_V2,                                 // flags
            0,                                                // reserved0
            0,                                                // reserved1
            BOOTITEM_MAGIC,                                   // magic
            BOOTITEM_NO_CRC32,                                // crc32
        };
        assert(Aligned(header.length));
        out.PatchHeader(header, header_start);
    }

    return 0;
}

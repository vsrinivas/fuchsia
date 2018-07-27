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
#include <fnmatch.h>
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

const char* const kCmdlineWS = " \t\r\n";

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
        buffer_(std::make_unique<std::byte[]>(size)), ptr_(buffer_.get()) {
    }

    size_t size() const {
        return ptr_ - buffer_.get();
    }

    iovec get() {
        return Iovec(buffer_.get(), size());
    }

    std::unique_ptr<std::byte[]> release() {
        ptr_ = nullptr;
        return std::move(buffer_);
    }

    template<typename T>
    void Append(const T* data, size_t bytes = sizeof(T)) {
        ptr_ = static_cast<std::byte*>(memcpy(static_cast<void*>(ptr_),
                                              static_cast<const void*>(data),
                                              bytes)) + bytes;
    }

    void Pad(size_t bytes) {
        ptr_ = static_cast<std::byte*>(memset(static_cast<void*>(ptr_), 0,
                                              bytes)) + bytes;
    }

private:
    std::unique_ptr<std::byte[]> buffer_;
    std::byte* ptr_ = nullptr;
};

class Item;
using ItemPtr = std::unique_ptr<Item>;

class OutputStream {
public:
    OutputStream() = delete;

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(OutputStream);
    OutputStream(OutputStream&&) = default;

    explicit OutputStream(fbl::unique_fd fd) : fd_(std::move(fd)) {
    }

    ~OutputStream() {
        Flush();
    }

    // Queue the iovec for output.  The second argument can transfer
    // ownership of the memory that buffer.iov_base points into.  This
    // object may refer to buffer.iov_base until Flush() completes.
    void Write(const iovec& buffer,
               std::unique_ptr<std::byte[]> owned = nullptr) {
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
            auto buffer = std::make_unique<std::byte[]>(sizeof(header));
            it->iov_base = memcpy(buffer.get(), &header, sizeof(header));
            owned_buffers_.push_front(std::move(buffer));
        } else {
            assert(flushed_ >= place + sizeof(header));
            // Overwrite the earlier part of the file with pwrite.  This
            // does not affect the current lseek position for the next writev.
            auto buf = reinterpret_cast<const std::byte*>(&header);
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
    std::forward_list<std::unique_ptr<std::byte[]>> owned_buffers_;
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
                static_cast<std::byte*>(read_pos->iov_base) + wrote);
        }
        return read_pos;
    }
};

class FileWriter {
public:
    FileWriter(const char* outfile, std::string prefix) :
        prefix_(std::move(prefix)), outfile_(outfile) {
    }

    unsigned int NextFileNumber() const {
        return files_ + 1;
    }

    OutputStream RawFile(const char* name) {
        ++files_;
        if (outfile_) {
            if (files_ > 1) {
                fprintf(stderr,
                        "--output (-o) cannot write second file %s\n", name);
                exit(1);
            } else {
                return CreateFile(outfile_);
            }
        } else {
            auto file = prefix_ + name;
            return CreateFile(file.c_str());
        }
    }

private:
    std::string prefix_;
    const char* outfile_ = nullptr;
    unsigned int files_ = 0;

    OutputStream CreateFile(const char* outfile) {
        // Remove the file in case it exists.  This makes it safe to
        // to do e.g. `zbi -o boot.zbi boot.zbi --entry=bin/foo=mybuild/foo`
        // to modify a file "in-place" because the input `boot.zbi` will
        // already have been opened before the new `boot.zbi` is created.
        remove(outfile);

        fbl::unique_fd fd(open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0666));
        if (!fd && errno == ENOENT) {
            MakeDirs(outfile);
            fd.reset(open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0666));
        }
        if (!fd) {
            fprintf(stderr, "cannot create %s: %s\n",
                    outfile, strerror(errno));
            exit(1);
        }

        return OutputStream(std::move(fd));
    }

    static void MakeDirs(const std::string& name) {
        auto lastslash = name.rfind('/');
        if (lastslash == std::string::npos) {
            return;
        }
        auto dir = name.substr(0, lastslash);
        if (mkdir(dir.c_str(), 0777) == 0) {
            return;
        }
        if (errno == ENOENT) {
            MakeDirs(dir);
            if (mkdir(dir.c_str(), 0777) == 0) {
                return;
            }
        }
        if (errno != EEXIST) {
            fprintf(stderr, "mkdir: %s: %s\n",
                    dir.c_str(), strerror(errno));
            exit(1);
        }
    }
};

class NameMatcher {
public:
    NameMatcher(const char* const* patterns, int count) :
        begin_(patterns), end_(&patterns[count]) {
        assert(count >= 0);
        assert(!patterns[count]);
    }
    NameMatcher(char** argv, int argi, int argc) :
        NameMatcher(&argv[argi], argc - argi) {
    }

    unsigned int names_checked() const { return names_checked_; }
    unsigned int names_matched() const { return names_matched_; }

    bool MatchesAll(void) const { return begin_ == end_; }

    // Not const because it keeps stats.
    bool Matches(const char* name, bool casefold = false) {
        ++names_checked_;
        if (MatchesAll() || PatternMatch(name, casefold)) {
            ++names_matched_;
            return true;
        } else {
            return false;
        }
    }

    void Summary(const char* verbed, const char* items, bool verbose) {
        if (!MatchesAll()) {
            if (names_checked() == 0) {
                fprintf(stderr, "no %s\n", items);
                exit(1);
            } else if (names_matched() == 0) {
                fprintf(stderr, "no matching %s\n", items);
                exit(1);
            } else if (verbose) {
                printf("%s %u of %u %s\n",
                       verbed, names_matched(), names_checked(), items);
            }
        }
    }

private:
    const char* const* const begin_ = nullptr;
    const char* const* const end_ = nullptr;
    unsigned int names_checked_ = 0;
    unsigned int names_matched_ = 0;

    bool PatternMatch(const char* name, bool casefold) const {
        bool excludes = false, included = false;
        for (auto next = begin_; next != end_; ++next) {
            auto ptn = *next;
            if (ptn[0] == '!' || ptn[0] == '^') {
                excludes = true;
            } else {
                included = (included || fnmatch(
                                ptn, name, casefold ? FNM_CASEFOLD : 0) == 0);
            }
        }
        if (included && excludes) {
            for (auto next = begin_; next != end_; ++next) {
                auto ptn = *next;
                if (ptn[0] == '!' || ptn[0] == '^') {
                    ++ptn;
                    if (fnmatch(ptn, name, casefold ? FNM_CASEFOLD : 0) == 0) {
                        return false;
                    }
                }
            }
        }
        return false;
    }
};

class Checksummer {
public:
    void Write(const iovec& buffer) {
        crc_ = crc32(crc_, static_cast<const uint8_t*>(buffer.iov_base),
                     buffer.iov_len);
    }

    void Write(const std::list<const iovec>& list) {
        for (const auto& buffer : list) {
            Write(buffer);
        }
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
        Buffer(std::unique_ptr<std::byte[]> buffer, size_t max_size) :
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
        std::unique_ptr<std::byte[]> data;
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
            return {std::make_unique<std::byte[]>(max_size), max_size};
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

std::unique_ptr<std::byte[]> Decompress(const std::list<const iovec>& payload,
                                        uint32_t decompressed_length) {
    auto buffer = std::make_unique<std::byte[]>(decompressed_length);

    LZ4F_decompressionContext_t ctx;
    LZ4F_CALL(LZ4F_createDecompressionContext, &ctx, LZ4F_VERSION);

    std::byte* dst = buffer.get();
    size_t dst_size = decompressed_length;
    for (const auto& iov : payload) {
        auto src = static_cast<const std::byte*>(iov.iov_base);
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

    // Get unowned file contents from a BOOTFS image.
    // The entry has been validated against the payload size.
    FileContents(const zbi_bootfs_dirent_t& entry,
                 const std::byte* bootfs_payload) :
        mapped_(const_cast<void*>(static_cast<const void*>(bootfs_payload +
                                                           entry.data_off))),
        mapped_size_(ZBI_BOOTFS_PAGE_ALIGN(entry.data_len)),
        exact_size_(entry.data_len),
        owned_(false) {
    }

    // Get unowned file contents from a string.
    // This object won't support PageRoundedView.
    FileContents(const char* buffer, bool null_terminate) :
        mapped_(const_cast<char*>(buffer)), mapped_size_(strlen(buffer) + 1),
        exact_size_(mapped_size_ - (null_terminate ? 0 : 1)), owned_(false) {
    }

    FileContents(FileContents&& other) {
        *this = std::move(other);
    }

    FileContents& operator=(FileContents&& other) {
        std::swap(mapped_, other.mapped_);
        std::swap(mapped_size_, other.mapped_size_);
        std::swap(exact_size_, other.exact_size_);
        std::swap(owned_, other.owned_);
        return *this;
    }

    ~FileContents() {
        if (owned_ && mapped_) {
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
        return Iovec(static_cast<const std::byte*>(mapped_) + offset, length);
    }

    const iovec PageRoundedView(size_t offset, size_t length) const {
        assert(length > 0);
        assert(offset < mapped_size_);
        assert(mapped_size_ - offset >= length);
        return Iovec(static_cast<const std::byte*>(mapped_) + offset, length);
    }

private:
    void* mapped_ = nullptr;
    size_t mapped_size_ = 0;
    size_t exact_size_ = 0;
    bool owned_ = true;
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
            not_ = groups[0] == '!';
            if (not_) {
                ++groups;
            }
            groups_ = std::make_unique<std::set<std::string>>();
            while (const char *p = strchr(groups, ',')) {
                groups_->emplace(groups, p - groups);
                groups = p + 1;
            }
            groups_->emplace(groups);
        }
    }

    bool AllowsUnspecified() const {
        return !groups_ || not_;
    }

    bool Allows(const std::string& group) const {
        return !groups_ || (groups_->find(group) == groups_->end()) == not_;
    }

private:
    std::unique_ptr<std::set<std::string>> groups_;
    bool not_ = false;
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

using InputFileGeneratorList =
    std::deque<std::unique_ptr<InputFileGenerator>>;

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
            auto line = read_ptr_;
            if (eol) {
                read_ptr_ = eol + 1;
            } else {
                read_ptr_ = eol = eof_;
            }
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
            return filter_->AllowsUnspecified() ? start : nullptr;
        }
        auto end_group = static_cast<const char*>(
            memchr(start + 1, '}', eq - start));
        if (!end_group) {
            fprintf(stderr,
                    "manifest entry has '{' but no '}': %.*s\n",
                    static_cast<int>(eol - start), start);
            exit(1);
        }
        std::string group(start + 1, end_group - 1 - start);
        return filter_->Allows(group) ? end_group + 1 : nullptr;
    }
};

class DirectoryInputFileGenerator : public InputFileGenerator {
public:
    DirectoryInputFileGenerator(fbl::unique_fd fd, std::string prefix) :
        source_prefix_(std::move(prefix)) {
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

class Item {
public:
    // Only the static methods below can create an Item.
    Item() = delete;

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Item);

    static const char* TypeName(uint32_t zbi_type) {
        return ItemTypeInfo(zbi_type).name;
    }

    static bool ParseTypeName(const char* name, uint32_t* abi_type) {
        for (const auto& t : kItemTypes_) {
            if (!strcasecmp(t.name, name)) {
                *abi_type = t.type;
                return true;
            }
        }
        int i = 0;
        return sscanf(name, "%x%n", abi_type, &i) == 1 && name[i] == '\0';
    }

    static std::string ExtractedFileName(unsigned int n, uint32_t zbi_type,
                                         bool raw) {
        std::string name;
        char buf[32];
        const auto info = ItemTypeInfo(zbi_type);
        if (info.name) {
            snprintf(buf, sizeof(buf), "%03u.", n);
            name = buf;
            name += info.name;
            for (auto& c : name) {
                c = std::tolower(c);
            }
        } else {
            snprintf(buf, sizeof(buf), "%03u.%08x", n, zbi_type);
            name = buf;
        }
        name += (raw && info.extension) ? info.extension : ".zbi";
        return name;
    }

    static void PrintTypeUsage(FILE* out) {
        fprintf(out, "\
TYPE can be hexadecimal or a name string (case-insensitive).\n\
Extracted items use the file names shown below:\n\
    --type               --extract-item             --extract-raw\n\
");
        for (const auto& t : kItemTypes_) {
            const auto zbi_name = ExtractedFileName(1, t.type, false);
            const auto raw_name = ExtractedFileName(1, t.type, true);
            fprintf(out, "    %-20s %-26s %s\n",
                    t.name, zbi_name.c_str(), raw_name.c_str());
        }
    }

    static bool TypeIsStorage(uint32_t zbi_type) {
        return (zbi_type == ZBI_TYPE_STORAGE_BOOTFS ||
                zbi_type == ZBI_TYPE_STORAGE_RAMDISK);
    }

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
        const char* type_name = TypeName(type());
        if (!type_name) {
            printf("%08x: %08x UNKNOWN (type=%08x)\n",
                   pos, header_.length, header_.type);
        } else if (TypeIsStorage(type())) {
            printf("%08x: %08x %s (size=%08x)\n",
                   pos, header_.length, type_name, header_.extra);
        } else {
            printf("%08x: %08x %s\n",
                   pos, header_.length, type_name);
        }
        if (header_.flags & ZBI_FLAG_CRC32) {
            auto print_crc = [](const zbi_header_t& header) {
                printf("        :          MAGIC=%08x CRC=%08x\n",
                       header.magic, header.crc32);
            };

            Checksummer crc;
            crc.Write(payload_);
            zbi_header_t check_header = header_;
            crc.FinalizeHeader(&check_header);

            if (compress_) {
                // We won't compute it until StreamCompressed, so
                // write out the computation we just did to check.
                print_crc(check_header);
            } else {
                print_crc(header_);
                if (check_header.crc32 != header_.crc32) {
                    fprintf(stderr, "error: CRC %08x does not match header\n",
                            check_header.crc32);
                }
            }
        } else {
            printf("        :          MAGIC=%08x NO CRC\n", header_.magic);
        }
    }

    bool AlreadyCompressed() const {
        return (header_.flags & ZBI_FLAG_STORAGE_COMPRESSED) && !compress_;
    }

    int Show() {
        if (header_.length > 0) {
            if (AlreadyCompressed()) {
                return CreateFromCompressed(*this)->Show();
            }
            switch (header_.type) {
            case ZBI_TYPE_STORAGE_BOOTFS:
                return ShowBootFS();
            case ZBI_TYPE_CMDLINE:
                return ShowCmdline();
            }
        }
        return 0;
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
            static const std::byte padding[ZBI_ALIGNMENT]{};
            out->Write(Iovec(padding, aligned - wrote));
        }
        assert(Aligned(out->WritePosition()));
    }

    // The buffer will be released when this Item is destroyed.  This item
    // and items earlier on the list can hold pointers into the buffer.
    void OwnBuffer(std::unique_ptr<std::byte[]> buffer) {
        buffers_.push_front(std::move(buffer));
    }
    void OwnFile(FileContents file) {
        files_.push_front(std::move(file));
    }

    // Consume another Item while keeping its owned buffers and files alive.
    void TakeOwned(ItemPtr other) {
        if (other) {
            buffers_.splice_after(buffers_.before_begin(), other->buffers_);
            files_.splice_after(files_.before_begin(), other->files_);
        }
    }

    // Create from in-core data.
    static ItemPtr CreateFromBuffer(
        uint32_t type, std::unique_ptr<std::byte[]> payload, size_t size) {
        auto item = MakeItem(NewHeader(type, size));
        item->payload_.emplace_front(Iovec(payload.get(), size));
        item->OwnBuffer(std::move(payload));
        Checksummer crc;
        crc.Write(item->payload_);
        crc.FinalizeHeader(&item->header_);
        return item;
    }

    // Create from local scratch data.
    template<typename T>
    static ItemPtr Create(uint32_t type, const T& payload) {
        auto buffer = std::make_unique<std::byte[]>(sizeof(payload));
        memcpy(buffer.get(), &payload, sizeof(payload));
        return CreateFromBuffer(type, std::move(buffer), sizeof(payload));
    }

    // Create from raw file contents.
    static ItemPtr CreateFromFile(
        FileContents file, uint32_t type, bool compress) {
        bool null_terminate = type == ZBI_TYPE_CMDLINE;
        compress = compress && TypeIsStorage(type);

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
    static ItemPtr CreateFromItem(const FileContents& file,
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
    static ItemPtr CreateFromCompressed(const Item& compressed) {
        assert(compressed.AlreadyCompressed());
        auto item = MakeItem(compressed.header_);
        item->header_.flags &= ~ZBI_FLAG_STORAGE_COMPRESSED;
        item->header_.length = item->header_.extra;
        auto buffer = Decompress(compressed.payload_, item->header_.length);
        item->payload_.emplace_front(
            Iovec(buffer.get(), item->header_.length));
        item->OwnBuffer(std::move(buffer));
        return item;
    }

    // Same, but consumes the compressed item while keeping its
    // owned buffers alive in the new uncompressed item.
    static ItemPtr CreateFromCompressed(ItemPtr compressed) {
        auto uncompressed = CreateFromCompressed(*compressed);
        uncompressed->TakeOwned(std::move(compressed));
        return uncompressed;
    }

    // Create a BOOTFS item.
    template<typename Filter>
    static ItemPtr CreateBootFS(FileOpener* opener,
                                const InputFileGeneratorList& input,
                                const Filter& include_file,
                                bool sort,
                                const std::string& prefix,
                                bool compress) {
        auto item = MakeItem(NewHeader(ZBI_TYPE_STORAGE_BOOTFS, 0), compress);

        // Collect the names and exact sizes here and the contents in payload_.
        struct Entry {
            std::string name;
            uint32_t data_len = 0;
        };
        std::deque<Entry> entries;
        size_t dirsize = 0, bodysize = 0;
        for (const auto& generator : input) {
            InputFileGenerator::value_type next;
            while (generator->Next(opener, prefix, &next)) {
                if (!include_file(next.target.c_str())) {
                    continue;
                }
                // Accumulate the space needed for each zbi_bootfs_dirent_t.
                dirsize += ZBI_BOOTFS_DIRENT_SIZE(next.target.size() + 1);
                Entry entry;
                entry.name.swap(next.target);
                entry.data_len = static_cast<uint32_t>(next.file.exact_size());
                if (entry.data_len != next.file.exact_size()) {
                    fprintf(stderr,
                            "input file size exceeds format maximum\n");
                    exit(1);
                }
                uint32_t size = ZBI_BOOTFS_PAGE_ALIGN(entry.data_len);
                bodysize += size;
                item->payload_.emplace_back(
                    next.file.PageRoundedView(0, size));
                entries.push_back(std::move(entry));
                item->OwnFile(std::move(next.file));
            }
        }

        if (sort) {
            std::sort(entries.begin(), entries.end(),
                      [](const Entry& a, const Entry& b) {
                          return a.name < b.name;
                      });
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
            crc.Write(item->payload_);
            crc.FinalizeHeader(&item->header_);
        }

        // Put the header at the front of the payload.
        item->payload_.emplace_front(buffer.get());
        item->OwnBuffer(buffer.release());

        return item;
    }

    // The generator consumes the Item.  The FileContents it generates
    // point into the Item's storage, so the generator must be kept
    // alive as long as any of those FileContents is alive.
    static auto ReadBootFS(ItemPtr item) {
        return std::unique_ptr<InputFileGenerator>(
            new BootFSInputFileGenerator(std::move(item)));
    }

    void ExtractItem(FileWriter* writer, NameMatcher* matcher) {
        std::string namestr = ExtractedFileName(writer->NextFileNumber(),
                                                type(), false);
        auto name = namestr.c_str();
        if (matcher->Matches(name, true)) {
            WriteZBI(writer, name, (Item*const[]){this});
        }
    }

    void ExtractRaw(FileWriter* writer, NameMatcher* matcher) {
        std::string namestr = ExtractedFileName(writer->NextFileNumber(),
                                                type(), true);
        auto name = namestr.c_str();
        if (matcher->Matches(name, true)) {
            if (type() == ZBI_TYPE_CMDLINE) {
                // Drop a trailing NUL.
                iovec iov = payload_.back();
                auto str = static_cast<const char*>(iov.iov_base);
                if (str[iov.iov_len - 1] == '\0') {
                    payload_.pop_back();
                    --iov.iov_len;
                    payload_.push_back(iov);
                }
            }
            if (AlreadyCompressed()) {
                auto uncompressed = CreateFromCompressed(*this);
                // The uncompressed item must outlive the OutputStream.
                auto out = writer->RawFile(name);
                uncompressed->StreamRawPayload(&out);
            } else {
                auto out = writer->RawFile(name);
                StreamRawPayload(&out);
            }
        }
    }

    template<typename ItemList>
    static void WriteZBI(FileWriter* writer, const char* name,
                         const ItemList& items) {
        auto out = writer->RawFile(name);

        uint32_t header_start = out.PlaceHeader();
        uint32_t payload_start = out.WritePosition();
        assert(Aligned(payload_start));

        for (const auto& item : items) {
            // The OutputStream stores pointers into Item buffers in its write
            // queue until it goes out of scope below.  The ItemList keeps all
            // the items alive past then.
            item->Stream(&out);
        }

        const zbi_header_t header =
            ZBI_CONTAINER_HEADER(out.WritePosition() - payload_start);
        assert(Aligned(header.length));
        out.PatchHeader(header, header_start);
    }

    void AppendPayload(std::string* buffer) const {
        if (AlreadyCompressed()) {
            CreateFromCompressed(*this)->AppendPayload(buffer);
        } else {
            for (const auto& iov : payload_) {
                buffer->append(static_cast<const char*>(iov.iov_base),
                               iov.iov_len);
            }
        }
    }

private:
    zbi_header_t header_;
    std::list<const iovec> payload_;
    // The payload_ items might point into these buffers.  They're just
    // stored here to own the buffers until the payload is exhausted.
    std::forward_list<FileContents> files_;
    std::forward_list<std::unique_ptr<std::byte[]>> buffers_;
    const bool compress_;

    struct ItemTypeInfo {
        uint32_t type;
        const char* name;
        const char* extension;
    };
    static constexpr const ItemTypeInfo kItemTypes_[] = {
#define kITemTypes_Element(type, name, extension) {type, name, extension},
    ZBI_ALL_TYPES(kITemTypes_Element)
#undef kitemtypes_element
};;

    static constexpr ItemTypeInfo ItemTypeInfo(uint32_t zbi_type) {
        for (const auto& t : kItemTypes_) {
            if (t.type == zbi_type) {
                return t;
            }
        }
        return {};
    }

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

    static ItemPtr MakeItem(const zbi_header_t& header,
                            bool compress = false) {
        return ItemPtr(new Item(header, compress));
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

    int ShowCmdline() const {
        std::string cmdline = std::accumulate(
            payload_.begin(), payload_.end(), std::string(),
            [](std::string cmdline, const iovec& iov) {
                return cmdline.append(
                    static_cast<const char*>(iov.iov_base),
                    iov.iov_len);
            });
        size_t start = 0;
        while (start < cmdline.size()) {
            size_t word_end = cmdline.find_first_of(kCmdlineWS, start);
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
        return 0;
    }

    const std::byte* payload_data() {
        if (payload_.size() > 1) {
            AppendBuffer buffer(PayloadSize());
            for (const auto& iov : payload_) {
                buffer.Append(iov.iov_base, iov.iov_len);
            }
            payload_.clear();
            payload_.push_front(buffer.get());
            OwnBuffer(buffer.release());
        }
        assert(payload_.size() == 1);
        return static_cast<const std::byte*>(payload_.front().iov_base);
    }

    class BootFSDirectoryIterator {
    public:
        operator bool() const {
            return left_ > 0;
        }

        const zbi_bootfs_dirent_t& operator*() const {
            auto entry = reinterpret_cast<const zbi_bootfs_dirent_t*>(next_);
            assert(left_ >= sizeof(*entry));
            return *entry;
        }

        const zbi_bootfs_dirent_t* operator->() const {
            return &**this;
        }

        BootFSDirectoryIterator& operator++() {
            assert(left_ > 0);
            if (left_ < sizeof(zbi_bootfs_dirent_t)) {
                fprintf(stderr, "BOOTFS directory truncated\n");
                left_ = 0;
            } else {
                size_t size = ZBI_BOOTFS_DIRENT_SIZE((*this)->name_len);
                if (size > left_) {
                    fprintf(stderr,
                            "BOOTFS directory truncated or bad name_len\n");
                    left_ = 0;
                } else {
                    next_ += size;
                    left_ -= size;
                }
            }
            return *this;
        }

        // The iterator itself is a container enough to use range-based for.
        const BootFSDirectoryIterator& begin() {
            return *this;
        }

        BootFSDirectoryIterator end() {
            return BootFSDirectoryIterator();
        }

        static int Create(Item* item, BootFSDirectoryIterator* it) {
            zbi_bootfs_header_t superblock;
            const uint32_t length = item->header_.length;
            if (length < sizeof(superblock)) {
                fprintf(stderr, "payload too short for BOOTFS header\n");
                return 1;
            }
            memcpy(&superblock, item->payload_data(), sizeof(superblock));
            if (superblock.magic != ZBI_BOOTFS_MAGIC) {
                fprintf(stderr, "BOOTFS header magic %#x should be %#x\n",
                        superblock.magic, ZBI_BOOTFS_MAGIC);
                return 1;
            }
            if (superblock.dirsize > length - sizeof(superblock)) {
                fprintf(stderr,
                        "BOOTFS header dirsize %u > payload size %zu\n",
                        superblock.dirsize, length - sizeof(superblock));
                return 1;
            }
            it->next_ = item->payload_data() + sizeof(superblock);
            it->left_ = superblock.dirsize;
            return 0;
        }

    private:
        const std::byte* next_ = nullptr;
        uint32_t left_ = 0;
    };

    bool CheckBootFSDirent(const zbi_bootfs_dirent_t& entry,
                           bool always_print) const {
        const char* align_check =
            entry.data_off % ZBI_BOOTFS_PAGE_SIZE == 0 ? "" :
            "[ERROR: misaligned offset] ";
        const char* size_check =
            (entry.data_off < header_.length &&
             header_.length - entry.data_off >= entry.data_len) ? "" :
            "[ERROR: offset+size too large] ";
        bool ok = align_check[0] == '\0' && size_check[0] == '\0';
        if (always_print || !ok) {
            fprintf(always_print ? stdout : stderr,
                    "        : %08x %08x %s%s%.*s\n",
                    entry.data_off, entry.data_len,
                    align_check, size_check,
                    static_cast<int>(entry.name_len), entry.name);
        }
        return ok;
    }

    int ShowBootFS() {
        assert(!AlreadyCompressed());
        BootFSDirectoryIterator dir;
        int status = BootFSDirectoryIterator::Create(this, &dir);
        for (const auto& entry : dir) {
            if (!CheckBootFSDirent(entry, true)) {
                status = 1;
            }
        }
        return status;
    }

    class BootFSInputFileGenerator : public InputFileGenerator {
    public:
        explicit BootFSInputFileGenerator(ItemPtr item) :
            item_(std::move(item)) {
            if (item_->AlreadyCompressed()) {
                item_ = CreateFromCompressed(std::move(item_));
            }
            int status = BootFSDirectoryIterator::Create(item_.get(), &dir_);
            if (status != 0) {
                exit(status);
            }
        }

        ~BootFSInputFileGenerator() override = default;

        // Copying from an existing BOOTFS ignores the --prefix setting.
        bool Next(FileOpener*, const std::string&,
                  value_type* value) override {
            if (!dir_) {
                return false;
            }
            if (!item_->CheckBootFSDirent(*dir_, false)) {
                exit(1);
            }
            value->target = dir_->name;
            value->file = FileContents(*dir_, item_->payload_data());
            ++dir_;
            return true;
        }

    private:
        ItemPtr item_;
        BootFSDirectoryIterator dir_;
    };
};

constexpr decltype(Item::kItemTypes_) Item::kItemTypes_;

using ItemList = std::vector<ItemPtr>;

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
                      [](const ItemPtr& item) {
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

constexpr const char kOptString[] = "-B:cd:e:FxXRg:hto:p:sT:uv";
constexpr const option kLongOpts[] = {
    {"complete", required_argument, nullptr, 'B'},
    {"compressed", no_argument, nullptr, 'c'},
    {"depfile", required_argument, nullptr, 'd'},
    {"entry", required_argument, nullptr, 'e'},
    {"files", no_argument, nullptr, 'F'},
    {"extract", no_argument, nullptr, 'x'},
    {"extract-items", no_argument, nullptr, 'X'},
    {"extract-raw", no_argument, nullptr, 'R'},
    {"groups", required_argument, nullptr, 'g'},
    {"help", no_argument, nullptr, 'h'},
    {"list", no_argument, nullptr, 't'},
    {"output", required_argument, nullptr, 'o'},
    {"prefix", required_argument, nullptr, 'p'},
    {"sort", no_argument, nullptr, 's'},
    {"type", required_argument, nullptr, 'T'},
    {"uncompressed", no_argument, nullptr, 'u'},
    {"verbose", no_argument, nullptr, 'v'},
    {nullptr, no_argument, nullptr, 0},
};

constexpr const char kUsageFormatString[] = "\
Usage: %s [OUTPUT...] INPUT... [-- PATTERN...]\n\
\n\
Diagnostic switches:\n\
    --help, -h                     print this message\n\
    --list, -t                     list input ZBI item headers; no --output\n\
    --verbose, -v                  show contents (e.g. BOOTFS file names)\n\
    --extract, -x                  extract BOOTFS files\n\
    --extract-items, -X            extract items as pseudo-files (see below)\n\
    --extract-raw, -R              extract original payloads, not ZBI format\n\
\n\
Output file switches must come before input arguments:\n\
    --output=FILE, -o FILE         output file name\n\
    --depfile=FILE, -d FILE        makefile dependency output file name\n\
\n\
The `--output` FILE is always removed and created fresh after all input\n\
files have been opened.  So it is safe to use the same file name as an input\n\
file and the `--output` FILE, to append more items.\n\
\n\
Input control switches apply to subsequent input arguments:\n\
    --files, -F                    read BOOTFS manifest files (default)\n\
    --groups=GROUPS, -g GROUPS     comma-separated list of manifest groups\n\
    --prefix=PREFIX, -p PREFIX     prepend PREFIX/ to target file names\n\
    --type=TYPE, -T TYPE           input files are TYPE items (see below)\n\
    --compressed, -c               compress RAMDISK images (default)\n\
    --uncompressed, -u             do not compress RAMDISK images\n\
\n\
Input arguments:\n\
    --entry=TEXT, -e  TEXT         like an input file containing only TEXT\n\
    FILE                           input or manifest file\n\
    DIRECTORY                      directory tree copied to BOOTFS PREFIX/\n\
\n\
With `--files` or `-F` (the default state), files with ZBI_TYPE_CONTAINER\n\
headers are incomplete boot files and other files are BOOTFS manifest files.\n\
Each DIRECTORY is listed recursively and handled just like a manifest file\n\
using the path relative to DIRECTORY as the target name (before any PREFIX).\n\
Each `--group`, `--prefix`, `-g`, or `-p` switch affects each file from a\n\
manifest or directory in subsequent FILE or DIRECTORY arguments.\n\
If GROUPS starts with `!` then only manifest entries that match none of\n\
the listed groups are used.\n\
\n\
With `--type` or `-T`, input files are treated as TYPE instead of manifest\n\
files, and directories are not permitted.  See below for the TYPE strings.\n\
\n\
Format control switches (last switch affects all output):\n\
    --complete=ARCH, -B ARCH       verify result is a complete boot image\n\
    --compressed, -c               compress BOOTFS images (default)\n\
    --uncompressed, -u             do not compress BOOTFS images\n\
    --sort, -s                     sort BOOTFS entries by name\n\
\n\
In all cases there is only a single BOOTFS item (if any) written out.\n\
The BOOTFS image contains all files from BOOTFS items in ZBI input files,\n\
manifest files, directories, and `--entry` switches (in input order unless\n\
`--sort` was specified).\n\
\n\
Each argument after -- is shell filename PATTERN (* matches even /)\n\
to filter the files that will be packed into BOOTFS, extracted, or listed.\n\
For a PATTERN that starts with ! or ^ matching names are excluded after\n\
including matches for all positive PATTERN arguments.\n\
\n\
When extracting a single file, `--output` or `-o` can be used.\n\
Otherwise multiple files are created with their BOOTFS file names\n\
relative to PREFIX (default empty, so in the current directory).\n\
\n\
With `--extract-items` or `-X`, instead of BOOTFS files the names are\n\
synthesized as shown below, numbered in the order items appear in the input\n\
starting with 001.  Output files are ZBI files that can be input later.\n\
\n\
With `--extract-raw` or `-R`, each file is written with just the\n\
uncompressed payload of the item and no ZBI headers.\n\
\n\
";

void usage(const char* progname) {
    fprintf(stderr, kUsageFormatString, progname);
    Item::PrintTypeUsage(stderr);
}

}  // anonymous namespace

int main(int argc, char** argv) {
    FileOpener opener;
    GroupFilter filter;
    const char* outfile = nullptr;
    const char* depfile = nullptr;
    uint32_t complete_arch = kImageArchUndefined;
    bool input_manifest = true;
    uint32_t input_type = ZBI_TYPE_DISCARD;
    bool compressed = true;
    bool extract = false;
    bool extract_items = false;
    bool extract_raw = false;
    bool list_contents = false;
    bool sort = false;
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
            depfile = optarg;
            opener.Init(outfile, depfile);
            continue;

        case 'F':
            input_manifest = true;
            continue;

        case 'T':
            if (Item::ParseTypeName(optarg, &input_type)) {
                input_manifest = false;
            } else {
                fprintf(stderr, "unrecognized type: %s\n", optarg);
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

        case 's':
            sort = true;
            continue;

        case 'x':
            extract = true;
            continue;

        case 'X':
            extract = true;
            extract_items = true;
            continue;

        case 'R':
            extract = true;
            extract_items = true;
            extract_raw = true;
            continue;

        case 'e':
            if (input_manifest) {
                bootfs_input.emplace_back(
                    new ManifestInputFileGenerator(FileContents(optarg, false),
                                                   prefix, &filter));
            } else if (input_type == ZBI_TYPE_CONTAINER) {
                fprintf(stderr,
                        "cannot use --entry (-e) with --target=CONTAINER\n");
                exit(1);
            } else {
                items.push_back(
                    Item::CreateFromFile(
                        FileContents(optarg, input_type == ZBI_TYPE_CMDLINE),
                        input_type, compressed));
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

        // A directory populates the BOOTFS.
        if (input_manifest && S_ISDIR(st.st_mode)) {
            // Calculate the prefix for opening files within the directory.
            // This won't be part of the BOOTFS file name.
            std::string dir_prefix(optarg);
            if (dir_prefix.back() != '/') {
                dir_prefix.push_back('/');
            }
            bootfs_input.emplace_back(
                new DirectoryInputFileGenerator(std::move(fd),
                                                std::move(dir_prefix)));
            continue;
        }

        // Anything else must be a regular file.
        RequireRegularFile(st, optarg);
        auto file = FileContents::Map(std::move(fd), st, optarg);

        if (input_manifest || input_type == ZBI_TYPE_CONTAINER) {
            if (ImportFile(file, optarg, &items)) {
                // It's another file in ZBI format.  The last item will own
                // the file buffer, so it lives until all earlier items are
                // exhausted.
                items.back()->OwnFile(std::move(file));
            } else if (input_manifest) {
                // It must be a manifest file.
                bootfs_input.emplace_back(
                    new ManifestInputFileGenerator(std::move(file),
                                                   prefix, &filter));
            } else {
                fprintf(stderr, "%s: not a Zircon Boot container\n", optarg);
                exit(1);
            }
        } else {
            items.push_back(Item::CreateFromFile(std::move(file),
                                                 input_type, compressed));
        }
    }

    // Remaining arguments (after --) are patterns for matching file names.
    NameMatcher name_matcher(argv, optind, argc);

    if (list_contents) {
        if (outfile || depfile) {
            fprintf(stderr, "\
--output (-o) and --depfile (-d) are incompatible with --list (-t)\n");
            exit(1);
        }
    } else {
        if (!outfile && !extract) {
            fprintf(stderr, "no output file\n");
            exit(1);
        }
    }

    // Don't merge incoming items when only listing or extracting.
    const bool merge = !list_contents && !extract;

    auto is_bootfs = [](const ItemPtr& item) {
        return item->type() == ZBI_TYPE_STORAGE_BOOTFS;
    };

    // If there are multiple BOOTFS input items, or any BOOTFS items when
    // we're also creating a fresh BOOTFS, merge them all into the new one.
    const bool merge_bootfs =
        ((!extract_items && !name_matcher.MatchesAll()) ||
         ((merge || !bootfs_input.empty()) &&
          ((bootfs_input.empty() ? 0 : 1) +
           std::count_if(items.begin(), items.end(), is_bootfs)) > 1));

    if (merge_bootfs) {
        for (auto& item : items) {
            if (is_bootfs(item)) {
                // Null out the list entry.
                ItemPtr old;
                item.swap(old);
                // The generator consumes the old item.
                bootfs_input.push_back(Item::ReadBootFS(std::move(old)));
            }
        }
    }

    ItemPtr keepalive;
    if (merge) {
        // Merge multiple CMDLINE input items with spaces in between.
        std::string cmdline;
        for (auto& item : items) {
            if (item && item->type() == ZBI_TYPE_CMDLINE) {
                // Null out the list entry.
                ItemPtr old;
                item.swap(old);
                cmdline.append({' '});
                old->AppendPayload(&cmdline);
                // Trim leading whitespace.
                cmdline.erase(0, cmdline.find_first_not_of(kCmdlineWS));
                // Trim trailing NULs and whitespace.
                while (!cmdline.empty() && cmdline.back() == '\0') {
                    cmdline.pop_back();
                }
                cmdline.erase(cmdline.find_last_not_of(kCmdlineWS) + 1);
                // Keep alive all the owned files from the old item,
                // since it might have owned files used by other items.
                old->TakeOwned(std::move(keepalive));
                keepalive.swap(old);
            }
        }
        if (!cmdline.empty()) {
            size_t size = cmdline.size() + 1;
            auto buffer = std::make_unique<std::byte[]>(size);
            memcpy(buffer.get(), cmdline.c_str(), size);
            items.push_back(Item::CreateFromBuffer(ZBI_TYPE_CMDLINE,
                                                   std::move(buffer), size));
        }
    }

    // Compact out the null entries.
    items.erase(std::remove(items.begin(), items.end(), nullptr), items.end());

    if (!bootfs_input.empty()) {
        // Pack up the BOOTFS.
        items.push_back(
            Item::CreateBootFS(&opener, bootfs_input, [&](const char* name) {
                    return extract_items || name_matcher.Matches(name);
                }, sort, prefix, compressed));
    }

    if (items.empty()) {
        fprintf(stderr, "no inputs\n");
        exit(1);
    }

    items.back()->TakeOwned(std::move(keepalive));

    if (!list_contents && complete_arch != kImageArchUndefined) {
        // The only hard requirement is that the kernel be first.
        // But it seems most orderly to put the BOOTFS second,
        // other storage in the middle, and CMDLINE last.
        std::stable_sort(
            items.begin(), items.end(),
            [](const ItemPtr& a, const ItemPtr& b) {
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

    // Now we're ready to start writing output!
    FileWriter writer(outfile, std::move(prefix));

    if (list_contents || verbose || extract) {
        if (list_contents || verbose) {
            const char* incomplete = IncompleteImage(items, complete_arch);
            if (incomplete) {
                printf("INCOMPLETE: %s\n", incomplete);
            } else {
                puts("COMPLETE: bootable image");
            }
        }

        // Contents start after the ZBI_TYPE_CONTAINER header.
        uint32_t pos = sizeof(zbi_header_t);
        int status = 0;
        for (auto& item : items) {
            if (list_contents || verbose) {
                item->Describe(pos);
            }
            if (verbose) {
                status |= item->Show();
            }
            pos += item->TotalSize();
            if (extract_items) {
                if (extract_raw) {
                    item->ExtractRaw(&writer, &name_matcher);
                } else {
                    item->ExtractItem(&writer, &name_matcher);
                }
            } else if (extract && is_bootfs(item)) {
                auto generator = Item::ReadBootFS(std::move(item));
                InputFileGenerator::value_type next;
                while (generator->Next(&opener, prefix, &next)) {
                    if (name_matcher.Matches(next.target.c_str())) {
                        writer.RawFile(next.target.c_str())
                            .Write(next.file.View(0, next.file.exact_size()));
                    }
                }
            }
        }
        if (status) {
            exit(status);
        }
    } else {
        Item::WriteZBI(&writer, "boot.zbi", items);
    }

    name_matcher.Summary(extract ? "extracted" : "matched",
                         extract_items ? "boot items" : "BOOTFS files",
                         verbose);

    return 0;
}

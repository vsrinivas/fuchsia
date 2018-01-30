// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

#include <blobfs/format.h>
#include <blobfs/fsck.h>
#include <blobfs/host.h>
#include <fbl/unique_fd.h>
#include <fs-management/mount.h>
#include <fvm/fvm.h>
#include <minfs/bcache.h>
#include <minfs/format.h>
#include <minfs/fsck.h>
#include <fbl/vector.h>

#define TRACE 0

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

// File system names
static constexpr char kMinfsName[] = "minfs";
static constexpr char kBlobfsName[] = "blobfs";

// Guid type names
static constexpr char kDefaultTypeName[] = "default";
static constexpr char kDataTypeName[] = "data";
static constexpr char kSystemTypeName[] = "system";
static constexpr char kBlobTypeName[] = "blob";

// Guid type values
static constexpr uint8_t kDefaultType[] = GUID_EMPTY_VALUE;
static constexpr uint8_t kDataType[] = GUID_DATA_VALUE;
static constexpr uint8_t kSystemType[] = GUID_SYSTEM_VALUE;
static constexpr uint8_t kBlobType[] = GUID_BLOB_VALUE;

typedef struct {
    size_t vslice_start;
    uint32_t slice_count;
    uint32_t block_offset;
    uint32_t block_count;
    bool zero_fill;
} vslice_info_t;

// Format defines an interface for file systems to implement in order to be placed into an FVM or
// sparse container
class Format {
public:
    // Detect the type of partition starting at |offset| bytes
    static zx_status_t Detect(int fd, off_t offset, disk_format_t* out);
    // Read file at |path| and generate appropriate Format
    static zx_status_t Create(const char* path, const char* type, fbl::unique_ptr<Format>* out);
    // Fun fsck on partition contained between bytes |start| and |end|
    static zx_status_t Check(fbl::unique_fd fd, off_t start, off_t end,
                             const fbl::Vector<size_t>& extent_lengths, disk_format_t part);
    virtual ~Format() {}
    // Update the file system's superblock (e.g. set FVM flag), and any other information required
    // for the partition to be placed in FVM.
    virtual zx_status_t MakeFvmReady(size_t slice_size, uint32_t vpart_index) = 0;
    // Get FVM data for each extent
    virtual zx_status_t GetVsliceRange(unsigned extent_index, vslice_info_t* vslice_info) const = 0;
    // Get total number of slices required for this partition
    virtual zx_status_t GetSliceCount(uint32_t* slices_out) const = 0;
    // Fill the in-memory data block with data from the specified block on disk
    virtual zx_status_t FillBlock(size_t block_offset) = 0;
    // Empty the data block (i.e. fill with all 0's)
    virtual zx_status_t EmptyBlock() = 0;

    void Guid(uint8_t* guid) const {
        memcpy(guid, guid_, sizeof(guid_));
    }

    void Type(uint8_t* type) const {
        memcpy(type, type_, sizeof(type_));
    }

    virtual void* Data() = 0;
    virtual void Name(char* name) const = 0;
    virtual uint32_t BlockSize() const = 0;
    virtual uint32_t BlocksPerSlice() const = 0;

    uint32_t VpartIndex() const {
        CheckFvmReady();
        return vpart_index_;
    }

protected:
    bool fvm_ready_;
    uint32_t vpart_index_;
    uint8_t guid_[FVM_GUID_LEN];
    uint8_t type_[GPT_GUID_LEN];

    void CheckFvmReady() const {
        if (!fvm_ready_) {
            fprintf(stderr, "Error: File system has not been converted to an FVM-ready format\n");
            exit(-1);
        }
    }

    void GenerateGuid() {
        srand(time(0));
        for (unsigned i = 0; i < FVM_GUID_LEN; i++) {
            guid_[i] = static_cast<uint8_t>(rand());
        }
    }
};

class MinfsFormat final : public Format {
public:
    MinfsFormat(fbl::unique_fd fd, const char* type);
    zx_status_t MakeFvmReady(size_t slice_size, uint32_t vpart_index) final;
    zx_status_t GetVsliceRange(unsigned extent_index, vslice_info_t* vslice_info) const final;
    zx_status_t GetSliceCount(uint32_t* slices_out) const final;
    zx_status_t FillBlock(size_t block_offset) final;
    zx_status_t EmptyBlock() final;
    void* Data() final;
    void Name(char* name) const final;
    uint32_t BlockSize() const final;
    uint32_t BlocksPerSlice() const final;
    uint8_t datablk[minfs::kMinfsBlockSize];

private:
    fbl::unique_ptr<minfs::Bcache> bc_;

    // Input superblock
    union {
        char blk_[minfs::kMinfsBlockSize];
        minfs::minfs_info_t info_;
    };

    // Output superblock
    union {
        char fvm_blk_[minfs::kMinfsBlockSize];
        minfs::minfs_info_t fvm_info_;
    };
};

class BlobfsFormat final : public Format {
public:
    BlobfsFormat(fbl::unique_fd fd, const char* type);
    ~BlobfsFormat();
    zx_status_t MakeFvmReady(size_t slice_size, uint32_t vpart_index) final;
    zx_status_t GetVsliceRange(unsigned extent_index, vslice_info_t* vslice_info) const final;
    zx_status_t GetSliceCount(uint32_t* slices_out) const final;
    zx_status_t FillBlock(size_t block_offset) final;
    zx_status_t EmptyBlock() final;
    void* Data() final;
    void Name(char* name) const final;
    uint32_t BlockSize() const final;
    uint32_t BlocksPerSlice() const final;
    uint8_t datablk[blobfs::kBlobfsBlockSize];

private:
    fbl::unique_fd fd_;
    uint64_t blocks_;

    // Input superblock
    union {
        char blk_[blobfs::kBlobfsBlockSize];
        blobfs::blobfs_info_t info_;
    };

    // Output superblock
    union {
        char fvm_blk_[blobfs::kBlobfsBlockSize];
        blobfs::blobfs_info_t fvm_info_;
    };
};

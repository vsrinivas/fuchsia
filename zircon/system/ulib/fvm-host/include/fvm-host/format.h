// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FVM_HOST_FORMAT_H_
#define FVM_HOST_FORMAT_H_

#include <fcntl.h>
#include <lib/zx/status.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

#include <memory>
#include <optional>

#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <fs-management/mount.h>

#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/host.h"
#include "src/storage/fvm/format.h"
#include "src/storage/fvm/fvm_sparse.h"
#include "src/storage/fvm/snapshot_metadata.h"
#include "src/storage/minfs/bcache.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/fsck.h"

#define TRACE 0

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
  do {                  \
  } while (0)
#endif

// File system names
static constexpr char kMinfsName[] = "minfs";
static constexpr char kBlobfsName[] = "blobfs";

// Guid type names
static constexpr char kDefaultTypeName[] = "default";
static constexpr char kDataTypeName[] = "data";
static constexpr char kDataUnsafeTypeName[] = "data-unsafe";
static constexpr char kSystemTypeName[] = "system";
static constexpr char kBlobTypeName[] = "blob";

// Guid type values
static constexpr uint8_t kDefaultType[] = GUID_EMPTY_VALUE;
static constexpr uint8_t kDataType[] = GUID_DATA_VALUE;
static constexpr uint8_t kSystemType[] = GUID_SYSTEM_VALUE;
static constexpr uint8_t kBlobType[] = GUID_BLOB_VALUE;

// An ExtentInfo is a mapping between a range of vslices in a vpartition, and a range of blocks in
// the underlying filesystem. It describes how to fill a target vslice range with blocks from the
// filesystem.
struct ExtentInfo {
  // Address of the first vslice in the extent.
  size_t vslice_start = 0;
  // Virtual length of the extent
  uint32_t vslice_count = 0;
  // Block offset of the first block to fill the extent from.
  uint32_t block_offset = 0;
  // Number of blocks to fill the extent with.
  uint32_t block_count = 0;
  // If |block_count| * block_size < |vslice_count| * slice_size, then |zero_fill| controls whether
  // the remaining bytes are explicitly zeroed. If not set, their value is undefined.
  bool zero_fill = false;

  // Returns the number of pslices needed to store the extent.
  uint32_t PslicesNeeded() const { return vslice_count; }
};

// Reservation is a request that may or may not be approved.
// Request for reservation may fail the AddPartition or
// request may be rejected silently. Only way to verify is
// to check both return value and "reserved" field.
struct fvm_reserve_t {
  // How many bytes/inodes needs to be reserved. Serves as input to
  // AddPartition.
  std::optional<uint64_t> request;

  // How many bytes/inodes were reserved. Serves as output of
  // AddPartition.
  // Depending on filesystems, more than request may be reserved.
  uint64_t reserved;
};

class FvmReservation {
 public:
  FvmReservation() {}
  FvmReservation(std::optional<uint64_t> inode_count, std::optional<uint64_t> data,
                 std::optional<uint64_t> total_bytes) {
    nodes_.request = inode_count;
    data_.request = data;
    total_bytes_.request = total_bytes;
  }

  // Returns true if all parts of the request are approved.
  bool Approved() const;

  void Dump(FILE* stream) const;

  fvm_reserve_t inodes() const { return nodes_; }

  fvm_reserve_t total_bytes() const { return total_bytes_; }

  fvm_reserve_t data() const { return data_; }

  void set_inodes_reserved(uint64_t reserved) { nodes_.reserved = reserved; }

  void set_data_reserved(uint64_t reserved) { data_.reserved = reserved; }

  void set_total_bytes_reserved(uint64_t reserved) { total_bytes_.reserved = reserved; }

 private:
  // Reserve number of files/directory that can be created.
  fvm_reserve_t nodes_ = {};

  // Raw bytes for "data" that needs to be reserved.
  fvm_reserve_t data_ = {};

  // Byte limit on the reservation. Zero value implies limitless. If set,
  // over-committing will fail. Return value contains total bytes reserved.
  fvm_reserve_t total_bytes_ = {};
};

// Format defines an interface for file systems to implement in order to be placed into an FVM or
// sparse container
class Format {
 public:
  // Detect the type of partition starting at |offset| bytes
  static zx_status_t Detect(int fd, off_t offset, disk_format_t* out);
  // Read file at |path| and generate appropriate Format
  static zx_status_t Create(const char* path, const char* type, std::unique_ptr<Format>* out);
  // Run fsck on partition contained between bytes |start| and |end|. extent_lengths is lengths
  // of each extent (in bytes).
  static zx_status_t Check(fbl::unique_fd fd, off_t start, off_t end,
                           const fbl::Vector<size_t>& extent_lengths, disk_format_t part);

  // Copies into |out_size| the number of bytes used by data in fs contained in a partition
  // between bytes |start| and |end|. extent_lengths is lengths of each extent (in bytes).
  static zx_status_t UsedDataSize(const fbl::unique_fd& fd, off_t start, off_t end,
                                  const fbl::Vector<size_t>& extent_lengths, disk_format_t part,
                                  uint64_t* out_size);

  // Copies into |out_inodes| the number of allocated inodes in fs contained in a partition
  // between bytes |start| and |end|. extent_lengths is lengths of each extent (in bytes).
  static zx_status_t UsedInodes(const fbl::unique_fd& fd, off_t start, off_t end,
                                const fbl::Vector<size_t>& extent_lengths, disk_format_t part,
                                uint64_t* out_inodes);

  // Copies into |out_size| the number of bytes used by data and bytes reserved for superblock,
  // bitmaps, inodes and journal on fs contained in a partition between bytes |start| and |end|.
  // extent_lengths is lengths of each extent (in bytes).
  static zx_status_t UsedSize(const fbl::unique_fd& fd, off_t start, off_t end,
                              const fbl::Vector<size_t>& extent_lengths, disk_format_t part,
                              uint64_t* out_size);
  virtual ~Format() {}
  // Update the file system's superblock (e.g. set FVM flag), and any other information required
  // for the partition to be placed in FVM.
  virtual zx_status_t MakeFvmReady(size_t slice_size, uint32_t vpart_index,
                                   FvmReservation* reserve) = 0;

  // Get the extent at |index| in the partition.
  // Once ZX_ERR_OUT_OF_RANGE is returned, any higher values of index will return the same.
  virtual zx::status<ExtentInfo> GetExtent(unsigned index) const = 0;

  // Get total number of slices required for this partition
  virtual zx_status_t GetSliceCount(uint32_t* slices_out) const = 0;
  // Fill the in-memory data block with data from the specified block on disk
  virtual zx_status_t FillBlock(size_t block_offset) = 0;
  // Empty the data block (i.e. fill with all 0's)
  virtual zx_status_t EmptyBlock() = 0;

  void GetPartitionInfo(fvm::PartitionDescriptor* partition) const {
    memcpy(partition->type, type_, sizeof(type_));
    strncpy(reinterpret_cast<char*>(partition->name), Name(), fvm::kMaxVPartitionNameLength);
    partition->flags = flags_;
  }

  void Guid(uint8_t* guid) const { memcpy(guid, guid_, sizeof(guid_)); }

  virtual void* Data() = 0;
  virtual uint32_t BlockSize() const = 0;
  virtual uint32_t BlocksPerSlice() const = 0;

  uint32_t VpartIndex() const {
    CheckFvmReady();
    return vpart_index_;
  }

 protected:
  bool fvm_ready_;
  uint32_t vpart_index_;
  uint8_t guid_[fvm::kGuidSize];
  uint8_t type_[GPT_GUID_LEN];
  uint32_t flags_;

  Format();

  void CheckFvmReady() const {
    if (!fvm_ready_) {
      fprintf(stderr, "Error: File system has not been converted to an FVM-ready format\n");
      exit(-1);
    }
  }

  void GenerateGuid() {
    srand(static_cast<unsigned int>(time(0)));
    for (unsigned i = 0; i < fvm::kGuidSize; i++) {
      guid_[i] = static_cast<uint8_t>(rand());
    }
  }

 private:
  virtual const char* Name() const = 0;
};

class MinfsFormat final : public Format {
 public:
  MinfsFormat(fbl::unique_fd fd, const char* type);
  zx_status_t MakeFvmReady(size_t slice_size, uint32_t vpart_index, FvmReservation* reserve) final;
  zx::status<ExtentInfo> GetExtent(unsigned index) const final;
  zx_status_t GetSliceCount(uint32_t* slices_out) const final;
  zx_status_t FillBlock(size_t block_offset) final;
  zx_status_t EmptyBlock() final;
  void* Data() final;
  uint32_t BlockSize() const final;
  uint32_t BlocksPerSlice() const final;
  uint8_t datablk[minfs::kMinfsBlockSize];

 private:
  const char* Name() const final;

  std::unique_ptr<minfs::Bcache> bc_;

  // Input superblock
  union {
    char blk_[minfs::kMinfsBlockSize];
    minfs::Superblock info_;
  };

  // Output superblock
  union {
    char fvm_blk_[minfs::kMinfsBlockSize];
    minfs::Superblock fvm_info_;
  };
};

class BlobfsFormat final : public Format {
 public:
  BlobfsFormat(fbl::unique_fd fd, const char* type);
  ~BlobfsFormat();
  zx_status_t MakeFvmReady(size_t slice_size, uint32_t vpart_index, FvmReservation* reserve) final;
  zx::status<ExtentInfo> GetExtent(unsigned index) const final;
  zx_status_t GetSliceCount(uint32_t* slices_out) const final;
  zx_status_t FillBlock(size_t block_offset) final;
  zx_status_t EmptyBlock() final;
  void* Data() final;
  uint32_t BlockSize() const final;
  uint32_t BlocksPerSlice() const final;
  uint8_t datablk[blobfs::kBlobfsBlockSize];

 private:
  const char* Name() const final;

  fbl::unique_fd fd_;
  uint64_t blocks_;

  // Input superblock
  union {
    char blk_[blobfs::kBlobfsBlockSize];
    blobfs::Superblock info_;
  };

  // Output superblock
  union {
    char fvm_blk_[blobfs::kBlobfsBlockSize];
    blobfs::Superblock fvm_info_;
  };

  uint32_t BlocksToSlices(uint32_t block_count) const;
  uint32_t SlicesToBlocks(uint32_t slice_count) const;
  zx_status_t ComputeSlices(uint64_t inode_count, uint64_t data_blocks,
                            uint64_t journal_block_count);
};

class InternalSnapshotMetaFormat final : public Format {
 public:
  InternalSnapshotMetaFormat(size_t reserved_slices, size_t slice_size,
                             const std::vector<fvm::PartitionSnapshotState>& partitions,
                             const std::vector<fvm::SnapshotExtentType>& extents);
  ~InternalSnapshotMetaFormat() override;
  zx_status_t MakeFvmReady(size_t slice_size, uint32_t vpart_index, FvmReservation* reserve) final {
    return ZX_OK;
  }
  zx::status<ExtentInfo> GetExtent(unsigned index) const final;
  zx_status_t GetSliceCount(uint32_t* slices_out) const final;
  zx_status_t FillBlock(size_t block_offset) final;
  zx_status_t EmptyBlock() final;
  void* Data() final;
  uint32_t BlockSize() const final;
  uint32_t BlocksPerSlice() const final;

 private:
  const char* Name() const final;

  size_t reserved_slices_ = 0;
  size_t slice_size_ = 0;
  // When FillBlock(0) is called, we read from meta_. Otherwise we read from zero_buf_.
  bool reading_from_meta_ = false;
  std::unique_ptr<uint8_t[]> zero_buf_;
  fvm::SnapshotMetadata meta_;
};

#endif  // FVM_HOST_FORMAT_H_

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FVM_HOST_FORMAT_H_
#define SRC_STORAGE_FVM_HOST_FORMAT_H_

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

class FvmReservation;

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

#endif  // SRC_STORAGE_FVM_HOST_FORMAT_H_

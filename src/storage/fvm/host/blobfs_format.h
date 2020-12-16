// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FVM_HOST_BLOBFS_FORMAT_H_
#define SRC_STORAGE_FVM_HOST_BLOBFS_FORMAT_H_

#include <lib/zx/status.h>

#include <memory>

#include <fbl/unique_fd.h>

#include "src/storage/blobfs/host.h"
#include "src/storage/fvm/host/format.h"

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

#endif  // SRC_STORAGE_FVM_HOST_BLOBFS_FORMAT_H_

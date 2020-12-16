// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FVM_HOST_MINFS_FORMAT_H_
#define SRC_STORAGE_FVM_HOST_MINFS_FORMAT_H_

#include <memory>

#include <fbl/unique_fd.h>

#include "src/storage/blobfs/format.h"
#include "src/storage/fvm/host/format.h"

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

#endif  // SRC_STORAGE_FVM_HOST_MINFS_FORMAT_H_

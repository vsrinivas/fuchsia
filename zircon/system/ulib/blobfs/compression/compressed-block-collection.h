// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_COMPRESSED_BLOCK_COLLECTION_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_COMPRESSED_BLOCK_COLLECTION_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/macros.h>

namespace blobfs {

// Interface for reading contiguous blocks of data from a compressed blob. Offsets are relative to
// the start of the data blocks of the blob (i.e. the merkle blocks are skipped).
class CompressedBlockCollection {
 public:
  CompressedBlockCollection() = default;
  virtual ~CompressedBlockCollection() = default;
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(CompressedBlockCollection);

  // Load exactly |block_offset| through |block_offset + num_blocks - 1| blocks into |buf|. The
  // value of data in |buf| is expected to be valid if and only if the return value is |ZX_OK|.
  virtual zx_status_t Read(uint8_t* buf, uint32_t data_block_offset, uint32_t num_blocks) = 0;
};

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_COMPRESSED_BLOCK_COLLECTION_H_

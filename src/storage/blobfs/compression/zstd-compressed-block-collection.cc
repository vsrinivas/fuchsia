// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zstd-compressed-block-collection.h"

#include <lib/fzl/vmo-mapper.h>
#include <lib/trace/event.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/device/block.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <cstring>
#include <limits>
#include <memory>

#include <blobfs/node-finder.h>
#include <fs/trace.h>
#include <storage/buffer/owned_vmoid.h>

#include "allocator/allocator.h"
#include "iterator/allocated-extent-iterator.h"
#include "iterator/block-iterator.h"
#include "zstd-seekable.h"

namespace blobfs {

ZSTDCompressedBlockCollectionImpl::ZSTDCompressedBlockCollectionImpl(
    storage::OwnedVmoid* vmoid, uint32_t num_vmo_blocks, SpaceManager* space_manager,
    fs::LegacyTransactionHandler* txn_handler, NodeFinder* node_finder, uint32_t node_index,
    uint32_t num_merkle_blocks)
    : vmoid_(vmoid),
      num_vmo_blocks_(num_vmo_blocks),
      space_manager_(space_manager),
      txn_handler_(txn_handler),
      node_finder_(node_finder),
      node_index_(node_index),
      num_merkle_blocks_(num_merkle_blocks) {}

zx_status_t ZSTDCompressedBlockCollectionImpl::Read(uint32_t data_block_offset,
                                                    uint32_t num_blocks) {
  TRACE_DURATION("blobfs", "ZSTDCompressedBlockCollectionImpl::Read", "node index", node_index_,
                 "data block offset", data_block_offset, "number of blocks", num_blocks);

  fs::ReadTxn txn(txn_handler_);

  uint64_t blob_block_offset64 =
      static_cast<uint64_t>(num_merkle_blocks_) + static_cast<uint64_t>(data_block_offset);
  if (blob_block_offset64 > std::numeric_limits<uint32_t>::max()) {
    FS_TRACE_ERROR("[blobfs][zstd] Block offset overflow\n");
    return ZX_ERR_OUT_OF_RANGE;
  }
  uint32_t blob_block_offset = static_cast<uint32_t>(blob_block_offset64);

  // Iterate to blocks and enqueue reads into VMO which backs |vmoid_|.
  {
    TRACE_DURATION("blobfs", "ZSTDCompressedBlockCollectionImpl::Read::Iterate", "blocks",
                   data_block_offset + num_blocks);
    BlockIterator iter(std::make_unique<AllocatedExtentIterator>(node_finder_, node_index_));
    zx_status_t status = IterateToBlock(&iter, blob_block_offset);
    if (status != ZX_OK) {
      FS_TRACE_ERROR("[blobfs][zstd] Failed to iterate to block at offset %u: %s\n",
                     blob_block_offset, zx_status_get_string(status));
    }

    // Lookup offset to BlobFS on block device; device offsets in |StreamBlocks| are relative to
    // this offset, but |txn| needs absolute block device offsets.
    uint64_t dev_data_start = DataStartBlock(space_manager_->Info());

    status = StreamBlocks(
        &iter, num_blocks,
        [&](uint64_t current_blob_block_offset, uint64_t dev_block_offset, uint32_t n_blocks) {
          // Sanity check offsets. Note that this does not catch attempting to read past the end of
          // the blob. This code assumes that |StreamBlocks| will return non-|ZX_OK| in that case.
          if (current_blob_block_offset < blob_block_offset ||
              current_blob_block_offset - blob_block_offset > num_blocks ||
              current_blob_block_offset - blob_block_offset + n_blocks > num_vmo_blocks_) {
            FS_TRACE_ERROR("[blobfs][zstd] Attempt to enqueue read at out-of-bounds VMO offset\n");
            return ZX_ERR_OUT_OF_RANGE;
          }
          txn.Enqueue(vmoid_->get(), current_blob_block_offset - blob_block_offset,
                      dev_data_start + dev_block_offset, n_blocks);
          return ZX_OK;
        });
    if (status != ZX_OK) {
      return status;
    }
  }

  // Read blocks into VMO which backs |vmoid_|.
  {
    TRACE_DURATION("blobfs", "ZSTDCompressedBlockCollectionImpl::Read::Transact", "blocks",
                   num_blocks);
    txn.Transact();
  }

  return ZX_OK;
}

}  // namespace blobfs

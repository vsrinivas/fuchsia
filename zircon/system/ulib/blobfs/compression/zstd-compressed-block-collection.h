// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_ZSTD_COMPRESSED_BLOCK_COLLECTION_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_ZSTD_COMPRESSED_BLOCK_COLLECTION_H_

#include <lib/fzl/vmo-mapper.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>

#include <blobfs/format.h>
#include <blobfs/node-finder.h>
#include <fs/transaction/block_transaction.h>
#include <storage/buffer/vmoid_registry.h>
#include <zstd/zstd_seekable.h>

#include "allocator/allocator.h"
#include "compression/compressed-block-collection.h"
#include "iterator/block-iterator-provider.h"
#include "iterator/block-iterator.h"

namespace blobfs {

class ZSTDCompressedBlockCollection : public CompressedBlockCollection {
 public:
  // Construct a ZSTD-backed block collection with the following assumptions:
  //
  // - |vmoid| is the ID registered with the block device for the VMO mapped by |mapped_vmo|;
  // - |space_manager_| is bound to the filesystem's superblock metadata;
  // - |txn_handler| is bound to the block device that registered |vmoid|;
  // - |node_finder| tracks this blob by |node_index|;
  // - |num_merkle_blocks| is the number of merkle blocks in this blob.
  // - All raw pointer input parameters will remain valid for the lifetime of this object.
  //
  // The owner of this |ZSTDCompressedBlockCollection| is responsible for ensuring that the above
  // assumptions hold.
  ZSTDCompressedBlockCollection(vmoid_t vmoid, fzl::VmoMapper* mapped_vmo,
                                SpaceManager* space_manager, fs::TransactionHandler* txn_handler,
                                NodeFinder* node_finder, uint32_t node_index,
                                uint32_t num_merkle_blocks);

  // CompressedBlockCollection implementation.
  zx_status_t Read(uint8_t* buf, uint32_t data_block_offset, uint32_t num_blocks) final;

 private:
  uint32_t NumVMOBlocks() const;

  vmoid_t vmoid_;
  fzl::VmoMapper* mapped_vmo_;
  SpaceManager* space_manager_;
  fs::TransactionHandler* txn_handler_;
  NodeFinder* node_finder_;
  uint32_t node_index_;
  uint32_t num_merkle_blocks_;
};

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_ZSTD_COMPRESSED_BLOCK_COLLECTION_H_

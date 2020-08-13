// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_COMPRESSION_ZSTD_COMPRESSED_BLOCK_COLLECTION_H_
#define SRC_STORAGE_BLOBFS_COMPRESSION_ZSTD_COMPRESSED_BLOCK_COLLECTION_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>

#include <blobfs/format.h>
#include <blobfs/node-finder.h>
#include <fs/transaction/legacy_transaction_handler.h>
#include <storage/buffer/owned_vmoid.h>
#include <storage/buffer/vmoid_registry.h>
#include <zstd/zstd_seekable.h>

#include "allocator/allocator.h"
#include "iterator/block-iterator-provider.h"
#include "iterator/block-iterator.h"

namespace blobfs {

// Interface for reading contiguous blocks of data from a compressed blob. Offsets are relative to
// the start of the data blocks of the blob (i.e. the merkle blocks are skipped). Each
// implementation defines its own contract for where data that is read will be stored and how long
// it is guaranteed to be valid. This style of contract allows implementations and their clients to
// minimize copying.
//
// This interface is separated from the concrete implementation below to make testing easier.
class ZSTDCompressedBlockCollection {
 public:
  ZSTDCompressedBlockCollection() = default;
  virtual ~ZSTDCompressedBlockCollection() = default;
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ZSTDCompressedBlockCollection);

  // Load exactly |block_offset| through |block_offset + num_blocks - 1| blocks into memory. Each
  // implementation defines its own contract for where data that is read will be stored and how long
  // it is guaranteed to be valid. The value of data in |buf| is expected to be valid if and only if
  // the return value is |ZX_OK|.
  virtual zx_status_t Read(uint32_t data_block_offset, uint32_t num_blocks) = 0;
};

// ZSTDCompressedBlockCollectionImpl is a |ZSTDCompressedBlockCollection| encoded using the ZSTD
// Seekable Format. Reads are copied to the beginning of the VMO referred to by |vmoid_| and remain
// valid until the next |ZSTDCompressedBlockCollectionImpl.Read()| that uses the same VMO.
//
// https://github.com/facebook/zstd/blob/dev/contrib/seekable_format/zstd_seekable_compression_format.md.
class ZSTDCompressedBlockCollectionImpl : public ZSTDCompressedBlockCollection {
 public:
  // Construct a ZSTD-backed block collection with the following assumptions:
  //
  // - |vmoid| is the ID registered with the block device;
  // - |space_manager| is bound to the filesystem's superblock metadata;
  // - |txn_handler| is bound to the block device that registered |vmoid|;
  // - |node_finder| tracks this blob by |node_index|;
  // - |num_merkle_blocks| is the number of merkle blocks in this blob.
  // - All raw pointer input parameters will remain valid for the lifetime of this object.
  //
  // The owner of this |ZSTDCompressedBlockCollectionImpl| is responsible for ensuring that the
  // above assumptions hold.
  ZSTDCompressedBlockCollectionImpl(storage::OwnedVmoid* vmoid, uint32_t num_vmo_blocks,
                                    SpaceManager* space_manager,
                                    fs::LegacyTransactionHandler* txn_handler,
                                    NodeFinder* node_finder, uint32_t node_index,
                                    uint32_t num_merkle_blocks);

  // ZSTDCompressedBlockCollection implementation. Reads are copied to the beginning of the VMO
  // referred to by |vmoid_|.
  zx_status_t Read(uint32_t data_block_offset, uint32_t num_blocks) final;

 private:
  storage::OwnedVmoid* vmoid_;
  uint32_t num_vmo_blocks_;
  SpaceManager* space_manager_;
  fs::LegacyTransactionHandler* txn_handler_;
  NodeFinder* node_finder_;
  uint32_t node_index_;
  uint32_t num_merkle_blocks_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_COMPRESSION_ZSTD_COMPRESSED_BLOCK_COLLECTION_H_

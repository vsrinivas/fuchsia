// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zstd-seekable-blob-collection.h"

#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <blobfs/format.h>
#include <blobfs/node-finder.h>
#include <fs/trace.h>
#include <storage/buffer/owned_vmoid.h>
#include <storage/buffer/vmoid_registry.h>

#include "allocator/allocator.h"
#include "zstd-compressed-block-collection.h"
#include "zstd-seekable-blob.h"

namespace blobfs {

zx_status_t ZSTDSeekableBlobCollection::Create(storage::VmoidRegistry* vmoid_registry,
                                               SpaceManager* space_manager,
                                               fs::LegacyTransactionHandler* txn_handler,
                                               NodeFinder* node_finder,
                                               std::unique_ptr<ZSTDSeekableBlobCollection>* out) {
  // |space_manager|, |txn_handler|, |node_finder| passed through on |Read()|.
  std::unique_ptr<ZSTDSeekableBlobCollection> cbc(
      new ZSTDSeekableBlobCollection(vmoid_registry, space_manager, txn_handler, node_finder));

  // Map shared transfer buffer.
  zx_status_t status =
      cbc->mapped_vmo_.CreateAndMap(kCompressedTransferBufferBytes, "zstd-seekable-compressed");
  if (status != ZX_OK) {
    FS_TRACE_ERROR("[blobfs][compressed] Failed to map transfer VMO: %s\n",
                   zx_status_get_string(status));
    return status;
  }

  // Attach shared transfer buffer to block device.
  status = cbc->vmoid_.AttachVmo(cbc->mapped_vmo_.vmo());
  if (status != ZX_OK) {
    FS_TRACE_ERROR("[blobfs][compressed] Failed to register transfer VMO: %s\n",
                   zx_status_get_string(status));
    return status;
  }

  *out = std::move(cbc);
  return ZX_OK;
}

ZSTDSeekableBlobCollection::ZSTDSeekableBlobCollection(storage::VmoidRegistry* vmoid_registry,
                                                       SpaceManager* space_manager,
                                                       fs::LegacyTransactionHandler* txn_handler,
                                                       NodeFinder* node_finder)
    : space_manager_(space_manager),
      txn_handler_(txn_handler),
      node_finder_(node_finder),
      vmoid_(vmoid_registry) {}

zx_status_t ZSTDSeekableBlobCollection::Read(uint32_t node_index, uint8_t* buf,
                                             uint64_t data_byte_offset, uint64_t num_bytes) {
  InodePtr node = node_finder_->GetNode(node_index);
  if (!node) {
    FS_TRACE_ERROR("[blobfs][compressed] Invalid node index: %u\n", node_index);
    return ZX_ERR_INVALID_ARGS;
  }

  // Currently, only ZSTD seekable is supported.
  if ((node->header.flags & kBlobFlagZSTDSeekableCompressed) == 0) {
    FS_TRACE_ERROR("[blobfs][compressed] Blob is not zstd-seekable compressed\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Create -> Read -> Destroy appropriate
  // RandomAccessCompressedBlob(ZSTDCompressedBlockCollectionImpl) composition.
  uint32_t num_merkle_blocks = ComputeNumMerkleTreeBlocks(*node);
  auto blocks = std::make_unique<ZSTDCompressedBlockCollectionImpl>(
      &vmoid_, kCompressedTransferBufferBlocks, space_manager_, txn_handler_, node_finder_,
      node_index, num_merkle_blocks);
  std::unique_ptr<ZSTDSeekableBlob> blob;
  zx_status_t status = ZSTDSeekableBlob::Create(&mapped_vmo_, std::move(blocks), &blob);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("[blobfs][compressed] Failed to construct ZSTDSeekableBlob: %s\n",
                   zx_status_get_string(status));
    return status;
  }
  status = blob->Read(buf, data_byte_offset, num_bytes);
  if (status != ZX_OK) {
    FS_TRACE_ERROR(
        "[blobfs][compressed] Failed to Read from blob: node_index=%u, data_byte_offset=%lu, "
        "num_bytes=%lu: %s\n",
        node_index, data_byte_offset, num_bytes, zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

}  // namespace blobfs

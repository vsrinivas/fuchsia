// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "corrupt_blob.h"

#include <blobfs/format.h>
#include <fs/trace.h>

#include "fs_block_client.h"

using block_client::BlockDevice;

zx_status_t CorruptBlob(std::unique_ptr<BlockDevice> device, BlobCorruptOptions* options) {
  unsigned char block[blobfs::kBlobfsBlockSize];
  std::unique_ptr<FsBlockClient> block_client;

  zx_status_t status = FsBlockClient::Create(std::move(device), &block_client);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs-corrupt: Could not initialize block client\n");
    return status;
  }

  // Read and verify the superblock.
  status = block_client->ReadBlock(blobfs::kSuperblockOffset, block);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs-corrupt: Could not read superblock\n");
    return status;
  }

  blobfs::Superblock superblock = *reinterpret_cast<blobfs::Superblock*>(block);
  status = blobfs::CheckSuperblock(&superblock, block_client->BlockCount());
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs-corrupt: Bad superblock, bailing out\n");
    return status;
  }

  if ((superblock.flags & blobfs::kBlobFlagClean) == 0) {
    FS_TRACE_ERROR(
        "blobfs-corrupt: Superblock indicates filesystem was not unmounted cleanly, bailing out\n");
    return ZX_ERR_BAD_STATE;
  }

  // Find the blob we are interested in.
  for (uint64_t inode_block = blobfs::NodeMapStartBlock(superblock);
       inode_block < blobfs::NodeMapStartBlock(superblock) + blobfs::NodeMapBlocks(superblock);
       inode_block++) {
    status = block_client->ReadBlock(inode_block, block);
    if (status != ZX_OK) {
      FS_TRACE_ERROR("blobfs-corrupt: Could not read inode block %lu\n", inode_block);
      return status;
    }

    for (uint32_t inode_in_block = 0; inode_in_block < blobfs::kBlobfsInodesPerBlock;
         inode_in_block++) {
      blobfs::Inode* inode =
          reinterpret_cast<blobfs::Inode*>(block + inode_in_block * sizeof(blobfs::Inode));

      // Skip unused inodes and extent containers.
      if (!inode->header.IsAllocated() || inode->header.IsExtentContainer()) {
        continue;
      }

      // Skip inodes that don't have the merkle we are looking for.
      if (blobfs::Digest(inode->merkle_root_hash) != options->merkle) {
        continue;
      }

      // Determine the location of the first data block (which may be the merkle tree or data block
      // depending on how large the blob is).
      if (inode->extent_count == 0) {
        FS_TRACE_ERROR("blobfs-corrupt: blob to corrupt is the empty blob!\n");
        return ZX_ERR_INVALID_ARGS;
      }
      auto extent = inode->extents[0];
      uint64_t data_block = DataStartBlock(superblock) + extent.Start();

      if (extent.Length() == 0) {
        FS_TRACE_ERROR("blobfs-corrupt: blob extent has 0 blocks?\n");
        return ZX_ERR_BAD_STATE;
      }

      // Read the first data block, flip the first byte, and re-write the block.
      status = block_client->ReadBlock(data_block, block);
      if (status != ZX_OK) {
        FS_TRACE_ERROR("blobfs-corrupt: Could not read data block %lu\n", extent.Start());
        return status;
      }

      block[0] ^= 0xFF;

      status = block_client->WriteBlock(data_block, block);
      if (status != ZX_OK) {
        FS_TRACE_ERROR("blobfs-corrupt: Could not write corrupted data block: %d\n", status);
      }
      return status;
    }
  }

  FS_TRACE_ERROR("blobfs-corrupt: requested blob not found\n");
  return ZX_ERR_NOT_FOUND;
}

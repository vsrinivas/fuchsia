// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cksum.h>

#include <iterator>
#include <utility>

#include <blobfs/common.h>
#include <blobfs/format.h>
#include <blobfs/mkfs.h>
#include <fbl/ref_ptr.h>
#include <fs/journal/initializer.h>
#include <fs/trace.h>
#include <fvm/client.h>
#include <safemath/checked_math.h>
#include <storage/buffer/owned_vmoid.h>

namespace blobfs {
namespace {

// Attempts to format the device as an FVM-based filesystem.
//
// If |device| does not speak FVM protocols, returns ZX_OK.
// If the volume cannot be modified (either removing old slices, or extending
// the volume to contain new slices), an error from the FVM is returned.
zx_status_t TryFormattingFVM(BlockDevice* device, Superblock* superblock) {
  fuchsia_hardware_block_volume_VolumeInfo fvm_info = {};
  zx_status_t status = device->VolumeQuery(&fvm_info);
  if (status != ZX_OK) {
    // If the device does not speak the FVM protocol, that's acceptable.
    return ZX_OK;
  }

  superblock->slice_size = fvm_info.slice_size;
  superblock->flags |= kBlobFlagFVM;

  if (superblock->slice_size % kBlobfsBlockSize) {
    FS_TRACE_ERROR("blobfs mkfs: Slice size not multiple of blobfs block\n");
    return ZX_ERR_IO_INVALID;
  }

  status = fvm::ResetAllSlices(device);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs mkfs: Failed to reset slices\n");
    return status;
  }

  const size_t blocks_per_slice = superblock->slice_size / kBlobfsBlockSize;
  // Converts blocks to slices, rounding up to the nearest slice size.
  auto BlocksToSlices = [blocks_per_slice](uint64_t blocks) {
    return fbl::round_up(blocks, blocks_per_slice) / blocks_per_slice;
  };
  uint64_t data_blocks = fbl::round_up(kMinimumDataBlocks, blocks_per_slice);

  uint64_t offset = kFVMBlockMapStart / blocks_per_slice;
  uint64_t length = BlocksToSlices(BlocksRequiredForBits(data_blocks));
  superblock->abm_slices = static_cast<uint32_t>(length);
  status = device->VolumeExtend(offset, superblock->abm_slices);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs mkfs: Failed to allocate block map\n");
    return status;
  }

  offset = kFVMNodeMapStart / blocks_per_slice;
  superblock->ino_slices = 1;
  status = device->VolumeExtend(offset, superblock->ino_slices);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs mkfs: Failed to allocate node map\n");
    return status;
  }

  // Allocate the minimum number of journal blocks in FVM.
  offset = kFVMJournalStart / blocks_per_slice;
  length = fbl::round_up(kDefaultJournalBlocks, blocks_per_slice) / blocks_per_slice;
  superblock->journal_slices = static_cast<uint32_t>(length);
  status = device->VolumeExtend(offset, superblock->journal_slices);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs mkfs: Failed to allocate journal blocks\n");
    return status;
  }

  // Allocate the minimum number of data blocks in the FVM.
  offset = kFVMDataStart / blocks_per_slice;
  length = fbl::round_up(kMinimumDataBlocks, blocks_per_slice) / blocks_per_slice;
  superblock->dat_slices = static_cast<uint32_t>(length);
  status = device->VolumeExtend(offset, superblock->dat_slices);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs mkfs: Failed to allocate data blocks\n");
    return status;
  }

  superblock->inode_count =
      static_cast<uint32_t>(superblock->ino_slices * superblock->slice_size / kBlobfsInodeSize);

  superblock->data_block_count =
      static_cast<uint32_t>(superblock->dat_slices * superblock->slice_size / kBlobfsBlockSize);
  superblock->journal_block_count =
      static_cast<uint32_t>(superblock->journal_slices * superblock->slice_size / kBlobfsBlockSize);
  return ZX_OK;
}

// Take the contents of the filesystem, generated in-memory, and transfer
// them to the underlying device.
zx_status_t WriteFilesystemToDisk(BlockDevice* device, const Superblock& superblock,
                                  const RawBitmap& block_bitmap,
                                  const fuchsia_hardware_block_BlockInfo& block_info) {
  uint64_t blockmap_blocks = BlockMapBlocks(superblock);
  uint64_t nodemap_blocks = NodeMapBlocks(superblock);

  // All in-memory structures have been created successfully. Dump everything to disk.
  uint64_t superblock_blocks = SuperblockBlocks(superblock);
  uint64_t journal_blocks = JournalBlocks(superblock);
  uint64_t total_blocks = superblock_blocks + blockmap_blocks + nodemap_blocks + journal_blocks;

  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(kBlobfsBlockSize * total_blocks, 0, &vmo);
  if (status != ZX_OK) {
    return status;
  }

  storage::OwnedVmoid vmoid;
  status = device->BlockAttachVmo(vmo, &vmoid.GetReference(device));
  if (status != ZX_OK) {
    return status;
  }

  // Write the root block.
  status = vmo.write(&superblock, 0, kBlobfsBlockSize);
  if (status != ZX_OK) {
    return status;
  }

  // Write allocation bitmap.
  for (uint64_t n = 0; n < blockmap_blocks; n++) {
    uint64_t offset = kBlobfsBlockSize * (superblock_blocks + n);
    uint64_t length = kBlobfsBlockSize;
    status = vmo.write(GetRawBitmapData(block_bitmap, n), offset, length);
    if (status != ZX_OK) {
      return status;
    }
  }

  // Write node map.
  uint8_t block[kBlobfsBlockSize];
  memset(block, 0, sizeof(block));
  for (uint64_t n = 0; n < nodemap_blocks; n++) {
    uint64_t offset = kBlobfsBlockSize * (superblock_blocks + blockmap_blocks + n);
    uint64_t length = kBlobfsBlockSize;
    status = vmo.write(block, offset, length);
    if (status != ZX_OK) {
      return status;
    }
  }

  // Write the journal.

  auto base_offset = superblock_blocks + blockmap_blocks + nodemap_blocks;
  fs::WriteBlocksFn write_blocks_fn = [&vmo, &superblock, base_offset](
                                          fbl::Span<const uint8_t> buffer, uint64_t block_offset,
                                          uint64_t block_count) {
    uint64_t offset =
        safemath::CheckMul<uint64_t>(safemath::CheckAdd(base_offset, block_offset).ValueOrDie(),
                                     kBlobfsBlockSize)
            .ValueOrDie();
    uint64_t size = safemath::CheckMul<uint64_t>(block_count, kBlobfsBlockSize).ValueOrDie();
    ZX_ASSERT((block_offset + block_count) <= JournalBlocks(superblock));
    ZX_ASSERT(buffer.size() >= size);
    return vmo.write(buffer.data(), offset, size);
  };
  status = fs::MakeJournal(journal_blocks, write_blocks_fn);
  if (status != ZX_OK) {
    return status;
  }

  auto FsToDeviceBlocks = [disk_block = block_info.block_size](uint64_t block) {
    return block * (kBlobfsBlockSize / disk_block);
  };

  block_fifo_request_t requests[4] = {};
  requests[0].opcode = BLOCKIO_WRITE;
  requests[0].vmoid = vmoid.get();
  requests[0].length = static_cast<uint32_t>(FsToDeviceBlocks(superblock_blocks));
  requests[0].vmo_offset = FsToDeviceBlocks(0);
  requests[0].dev_offset = FsToDeviceBlocks(0);

  requests[1].opcode = BLOCKIO_WRITE;
  requests[1].vmoid = vmoid.get();
  requests[1].length = static_cast<uint32_t>(FsToDeviceBlocks(blockmap_blocks));
  requests[1].vmo_offset = FsToDeviceBlocks(superblock_blocks);
  requests[1].dev_offset = FsToDeviceBlocks(BlockMapStartBlock(superblock));

  requests[2].opcode = BLOCKIO_WRITE;
  requests[2].vmoid = vmoid.get();
  requests[2].length = static_cast<uint32_t>(FsToDeviceBlocks(nodemap_blocks));
  requests[2].vmo_offset = FsToDeviceBlocks(superblock_blocks + blockmap_blocks);
  requests[2].dev_offset = FsToDeviceBlocks(NodeMapStartBlock(superblock));

  requests[3].opcode = BLOCKIO_WRITE;
  requests[3].vmoid = vmoid.get();
  requests[3].length = static_cast<uint32_t>(FsToDeviceBlocks(journal_blocks));
  requests[3].vmo_offset = FsToDeviceBlocks(superblock_blocks + blockmap_blocks + nodemap_blocks);
  requests[3].dev_offset = FsToDeviceBlocks(JournalStartBlock(superblock));

  return device->FifoTransaction(requests, std::size(requests));
}

}  // namespace

zx_status_t FormatFilesystem(BlockDevice* device) {
  zx_status_t status;
  fuchsia_hardware_block_BlockInfo block_info = {};
  status = device->BlockGetInfo(&block_info);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: cannot acquire block info: %d\n", status);
    return status;
  }

  if (block_info.flags & BLOCK_FLAG_READONLY) {
    FS_TRACE_ERROR("blobfs: cannot format read-only device\n");
    return ZX_ERR_ACCESS_DENIED;
  }
  if (block_info.block_size == 0 || block_info.block_count == 0) {
    return ZX_ERR_NO_SPACE;
  }
  if (kBlobfsBlockSize % block_info.block_size != 0) {
    return ZX_ERR_IO_INVALID;
  }

  uint64_t blocks = (block_info.block_size * block_info.block_count) / kBlobfsBlockSize;
  Superblock superblock;
  InitializeSuperblock(blocks, &superblock);

  status = TryFormattingFVM(device, &superblock);
  if (status != ZX_OK) {
    return status;
  }

  status = CheckSuperblock(&superblock, blocks);
  if (status != ZX_OK) {
    return status;
  }

  uint64_t blockmap_blocks = BlockMapBlocks(superblock);

  RawBitmap block_bitmap;
  if (block_bitmap.Reset(blockmap_blocks * kBlobfsBlockBits)) {
    FS_TRACE_ERROR("blobfs: Couldn't allocate block map\n");
    return -1;
  }
  if (block_bitmap.Shrink(superblock.data_block_count)) {
    FS_TRACE_ERROR("blobfs: Couldn't shrink block map\n");
    return -1;
  }

  // Reserve first |kStartBlockMinimum| data blocks
  block_bitmap.Set(0, kStartBlockMinimum);

  status = WriteFilesystemToDisk(device, superblock, block_bitmap, block_info);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to write to disk: %d\n", status);
    return status;
  }

  FS_TRACE_DEBUG("BLOBFS: mkfs success\n");
  return ZX_OK;
}

}  // namespace blobfs

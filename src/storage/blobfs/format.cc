// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/format.h"

#include <lib/cksum.h>
#include <lib/stdcompat/span.h>
#include <lib/syslog/cpp/macros.h>

#include <iterator>
#include <optional>
#include <utility>

#include <fbl/ref_ptr.h>
#include <safemath/checked_math.h>
#include <storage/buffer/owned_vmoid.h>

#include "src/lib/storage/vfs/cpp/journal/initializer.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/mkfs.h"
#include "src/storage/fvm/client.h"

namespace blobfs {
namespace {

using ::block_client::BlockDevice;

std::optional<fuchsia_hardware_block_volume_VolumeManagerInfo> TryGetVolumeManagerInfo(
    const BlockDevice& device) {
  fuchsia_hardware_block_volume_VolumeManagerInfo fvm_manager_info = {};
  fuchsia_hardware_block_volume_VolumeInfo volume_info = {};
  zx_status_t status = device.VolumeGetInfo(&fvm_manager_info, &volume_info);
  if (status != ZX_OK) {
    return std::nullopt;
  }
  return fvm_manager_info;
}

// Generates a superblock that will cover the entire device described by |block_info|.
zx::result<Superblock> FormatSuperblock(const fuchsia_hardware_block_BlockInfo& block_info,
                                        const FilesystemOptions& options) {
  uint64_t blocks = (block_info.block_size * block_info.block_count) / kBlobfsBlockSize;
  Superblock superblock;
  if (zx_status_t status = InitializeSuperblock(blocks, options, &superblock); status != ZX_OK) {
    return zx::error(status);
  }

  zx_status_t status = CheckSuperblock(&superblock, blocks);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Check superblock failed: " << status;
    return zx::error(status);
  }
  return zx::ok(superblock);
}

// Generates a FVM-aware superblock with the minimum number of slices reserved for each metadata
// region.
zx::result<Superblock> FormatSuperblockFVM(
    BlockDevice* device, const fuchsia_hardware_block_volume_VolumeManagerInfo& fvm_info,
    const FilesystemOptions& options) {
  Superblock superblock;
  InitializeSuperblockOptions(options, &superblock);

  superblock.slice_size = fvm_info.slice_size;
  superblock.flags |= kBlobFlagFVM;

  if (superblock.slice_size % kBlobfsBlockSize) {
    FX_LOGS(ERROR) << "mkfs: Slice size not multiple of blobfs block";
    return zx::error(ZX_ERR_IO_INVALID);
  }

  zx_status_t status = fvm::ResetAllSlices(device);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "mkfs: Failed to reset slices";
    return zx::error(status);
  }

  const size_t blocks_per_slice = superblock.slice_size / kBlobfsBlockSize;
  // Converts blocks to slices, rounding up to the nearest slice size.
  auto BlocksToSlices = [blocks_per_slice](uint64_t blocks) {
    return fbl::round_up(blocks, blocks_per_slice) / blocks_per_slice;
  };

  uint64_t data_blocks = fbl::round_up(kMinimumDataBlocks, blocks_per_slice);

  // Allocate the minimum number of blocks for a minimal bitmap.
  uint64_t offset = kFVMBlockMapStart / blocks_per_slice;
  uint64_t length = BlocksToSlices(BlocksRequiredForBits(data_blocks));
  superblock.abm_slices = safemath::checked_cast<decltype(superblock.abm_slices)>(length);
  status = device->VolumeExtend(offset, superblock.abm_slices);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "mkfs: Failed to allocate block map";
    return zx::error(status);
  }

  // Allocate the requested number of node blocks in FVM.
  offset = kFVMNodeMapStart / blocks_per_slice;
  length = BlocksToSlices(BlocksRequiredForInode(options.num_inodes));
  superblock.ino_slices = safemath::checked_cast<decltype(superblock.ino_slices)>(length);
  status = device->VolumeExtend(offset, superblock.ino_slices);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "mkfs: Failed to allocate node map";
    return zx::error(status);
  }

  // Allocate the minimum number of journal blocks in FVM.
  offset = kFVMJournalStart / blocks_per_slice;
  length = BlocksToSlices(kMinimumJournalBlocks);
  superblock.journal_slices = safemath::checked_cast<decltype(superblock.journal_slices)>(length);
  status = device->VolumeExtend(offset, superblock.journal_slices);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "mkfs: Failed to allocate journal blocks";
    return zx::error(status);
  }

  // Allocate the minimum number of data blocks in the FVM.
  offset = kFVMDataStart / blocks_per_slice;
  length = BlocksToSlices(kMinimumDataBlocks);
  superblock.dat_slices = safemath::checked_cast<decltype(superblock.dat_slices)>(length);
  status = device->VolumeExtend(offset, superblock.dat_slices);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "mkfs: Failed to allocate data blocks";
    return zx::error(status);
  }

  superblock.inode_count = safemath::checked_cast<decltype(superblock.inode_count)>(
      superblock.ino_slices * superblock.slice_size / kBlobfsInodeSize);
  superblock.data_block_count = safemath::checked_cast<decltype(superblock.data_block_count)>(
      superblock.dat_slices * superblock.slice_size / kBlobfsBlockSize);
  superblock.journal_block_count = safemath::checked_cast<decltype(superblock.journal_block_count)>(
      superblock.journal_slices * superblock.slice_size / kBlobfsBlockSize);

  // Now that we've allocated some slices, re-query FVM for the number of blocks assigned to the
  // partition. We'll use this as a sanity check in CheckSuperblock.
  fuchsia_hardware_block_BlockInfo block_info = {};
  status = device->BlockGetInfo(&block_info);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot acquire block info: " << status;
    return zx::error(status);
  }
  uint64_t blocks = (block_info.block_count * block_info.block_size) / kBlobfsBlockSize;

  status = CheckSuperblock(&superblock, blocks);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Check superblock failed: " << status;
    return zx::error(status);
  }
  return zx::ok(superblock);
}

// Take the contents of the filesystem, generated in-memory, and transfer them to the underlying
// device.
zx_status_t WriteFilesystemToDisk(BlockDevice* device, const Superblock& superblock,
                                  const RawBitmap& block_bitmap, uint64_t block_size) {
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
                                          cpp20::span<const uint8_t> buffer, uint64_t block_offset,
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

  auto FsToDeviceBlocks = [disk_block = block_size](uint64_t block) -> uint64_t {
    return block * (kBlobfsBlockSize / disk_block);
  };

  block_fifo_request_t requests[5] = {};
  using RequestLengthType = decltype(block_fifo_request_t::length);
  static_assert(
      std::is_same_v<RequestLengthType, uint32_t>,
      "Type of length field for block FIFO request has changed, validate conversions below.");

  requests[0].opcode = BLOCKIO_WRITE;
  requests[0].vmoid = vmoid.get();
  requests[0].length =
      safemath::checked_cast<RequestLengthType>(FsToDeviceBlocks(superblock_blocks));
  requests[0].vmo_offset = FsToDeviceBlocks(0);
  requests[0].dev_offset = FsToDeviceBlocks(0);

  requests[1].opcode = BLOCKIO_WRITE;
  requests[1].vmoid = vmoid.get();
  requests[1].length = safemath::checked_cast<RequestLengthType>(FsToDeviceBlocks(blockmap_blocks));
  requests[1].vmo_offset = FsToDeviceBlocks(superblock_blocks);
  requests[1].dev_offset = FsToDeviceBlocks(BlockMapStartBlock(superblock));

  requests[2].opcode = BLOCKIO_WRITE;
  requests[2].vmoid = vmoid.get();
  requests[2].length = safemath::checked_cast<RequestLengthType>(FsToDeviceBlocks(nodemap_blocks));
  requests[2].vmo_offset = FsToDeviceBlocks(superblock_blocks + blockmap_blocks);
  requests[2].dev_offset = FsToDeviceBlocks(NodeMapStartBlock(superblock));

  requests[3].opcode = BLOCKIO_WRITE;
  requests[3].vmoid = vmoid.get();
  requests[3].length = safemath::checked_cast<RequestLengthType>(FsToDeviceBlocks(journal_blocks));
  requests[3].vmo_offset = FsToDeviceBlocks(superblock_blocks + blockmap_blocks + nodemap_blocks);
  requests[3].dev_offset = FsToDeviceBlocks(JournalStartBlock(superblock));

  int count = 4;
  if (superblock.flags & kBlobFlagFVM) {
    requests[4].opcode = BLOCKIO_WRITE;
    requests[4].vmoid = vmoid.get();
    requests[4].length =
        safemath::checked_cast<RequestLengthType>(FsToDeviceBlocks(superblock_blocks));
    requests[4].vmo_offset = FsToDeviceBlocks(0);
    requests[4].dev_offset = FsToDeviceBlocks(kFVMBackupSuperblockOffset);
    ++count;
  }

  status = device->FifoTransaction(requests, count);
  if (status != ZX_OK)
    return status;

  block_fifo_request_t flush_request = {.opcode = BLOCKIO_FLUSH};
  return device->FifoTransaction(&flush_request, 1);
}

}  // namespace

zx_status_t FormatFilesystem(BlockDevice* device, const FilesystemOptions& options) {
  zx_status_t status;
  fuchsia_hardware_block_BlockInfo block_info = {};
  status = device->BlockGetInfo(&block_info);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot acquire block info: " << status;
    return status;
  }

  if (block_info.flags & BLOCK_FLAG_READONLY) {
    FX_LOGS(ERROR) << "Cannot format read-only device";
    return ZX_ERR_ACCESS_DENIED;
  }
  if (block_info.block_size == 0) {
    FX_LOGS(ERROR) << "Device has zero-sized blocks";
    return ZX_ERR_NO_SPACE;
  }
  if (kBlobfsBlockSize % block_info.block_size != 0) {
    FX_LOGS(ERROR) << "Device block size " << block_info.block_size << " invalid";
    return ZX_ERR_IO_INVALID;
  }

  zx::result<Superblock> superblock_or;
  if (auto maybe_volume_info = TryGetVolumeManagerInfo(*device); maybe_volume_info.has_value()) {
    superblock_or = FormatSuperblockFVM(device, maybe_volume_info.value(), options);
  } else {
    superblock_or = FormatSuperblock(block_info, options);
  }
  if (superblock_or.is_error()) {
    return superblock_or.status_value();
  }
  const Superblock& superblock = superblock_or.value();

  uint64_t blockmap_blocks = BlockMapBlocks(superblock);
  RawBitmap block_bitmap;
  if (block_bitmap.Reset(blockmap_blocks * kBlobfsBlockBits)) {
    FX_LOGS(ERROR) << "Couldn't allocate block map";
    return -1;
  }
  if (block_bitmap.Shrink(superblock.data_block_count)) {
    FX_LOGS(ERROR) << "Couldn't shrink block map";
    return -1;
  }

  // Reserve first |kStartBlockMinimum| data blocks
  block_bitmap.Set(0, kStartBlockMinimum);

  status = WriteFilesystemToDisk(device, superblock, block_bitmap, block_info.block_size);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to write to disk: " << status;
    return status;
  }

  FX_LOGS(DEBUG) << "mkfs success";
  return ZX_OK;
}

}  // namespace blobfs

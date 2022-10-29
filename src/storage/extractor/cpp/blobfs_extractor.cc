// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/fd.h>
#include <lib/zx/channel.h>
#include <lib/zx/result.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <utility>

#include <fbl/unique_fd.h>
#include <safemath/checked_math.h>

#include "lib/async/dispatcher.h"
#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/extractor/c/extractor.h"
#include "src/storage/extractor/cpp/extractor.h"

namespace extractor {
namespace {

// Walks the file system and collects interesting metadata.
class FsWalker {
 public:
  ~FsWalker() {
    if (vfs_)
      vfs_->TearDown();
  }

  static zx::result<std::unique_ptr<FsWalker>> Create(fbl::unique_fd input_fd,
                                                      Extractor& extractor);

  zx::result<> Walk(async_dispatcher_t* dispatcher);

 private:
  // Returns maximum addressable block in the fs.
  uint64_t BlockLimit() const { return blobfs::DataStartBlock(info_) + blobfs::DataBlocks(Info()); }

  // Returns maximum addressable byte in the fs.
  uint64_t ByteLimit() const { return BlockLimit() * Info().block_size; }

  // Walks the partition and marks all bytes as reported by ByteLimit() as unused for non-fvm
  // partition or unmapped for fvm partition.
  zx::result<> WalkPartition() const;

  // Walks different segments, like inode table and bitmaps except data segment, of the filesystem.
  // Marks them as data unmodified.
  zx::result<> WalkSegments() const;

  // LoadAndVerify each blob and dump the corrupted files.
  zx::result<> WalkBlobs(blobfs::Blobfs& blobfs) const;

  // Dumps each block in an extent
  zx::result<> ExtentBlockHandler(blobfs::Extent extent) const;

  // Iterates through all the extents of an inode, node_num is inode index,
  // alloc_block is a counter for number of blocks traversed.
  zx::result<> WalkExtentContainer(blobfs::Blobfs& blobfs, uint32_t node_num, uint32_t alloc_block,
                                   blobfs::Inode ino) const;

  zx::result<std::unique_ptr<blobfs::Blobfs>> CreateBlobfs(async_dispatcher_t* dispatcher);

  const blobfs::Superblock& Info() const { return info_; }

  FsWalker(fbl::unique_fd input_fd, Extractor& extractor);

  // Not copyable or movable
  FsWalker(const FsWalker&) = delete;
  FsWalker& operator=(const FsWalker&) = delete;
  FsWalker(FsWalker&&) = delete;
  FsWalker& operator=(FsWalker&&) = delete;

  // Loads superblock located at start_offset. If the copy of superblock has valid
  // magic values, the function returns zx::ok().
  zx::result<> TryLoadSuperblock(uint64_t start_offset);

  // Loads one valid copy of superblock from the input_fd_.
  // Primary superblock location is given highest priority followed by backup superblock
  // of fvm partition and then non-fvm partition.
  zx::result<> LoadSuperblock();

  // The valid copy of superblock.
  blobfs::Superblock info_;

  // Pointer to extractor.
  Extractor& extractor_;

  // File from where the filesystem is parsed/loaded.
  fbl::unique_fd input_fd_;

  // Pointer to vfs.
  std::unique_ptr<fs::PagedVfs> vfs_;
};

blobfs::MountOptions ReadOnlyOptions() {
  return blobfs::MountOptions{.writability = blobfs::Writability::ReadOnlyDisk};
}

zx::result<std::unique_ptr<blobfs::Blobfs>> FsWalker::CreateBlobfs(async_dispatcher_t* dispatcher) {
  zx::result device = block_client::RemoteBlockDevice::Create(input_fd_.get());
  if (device.is_error()) {
    std::cerr << "Error creating Remote Block Device: " << device.status_string() << std::endl;
    return device.take_error();
  }

  vfs_ = std::make_unique<fs::PagedVfs>(dispatcher);
  if (auto status = vfs_->Init(); status.is_error())
    return zx::error(status.error_value());

  auto blobfs_or =
      blobfs::Blobfs::Create(dispatcher, std::move(device.value()), vfs_.get(), ReadOnlyOptions());
  if (blobfs_or.is_error()) {
    std::cerr << "Cannot create filesystem for checking: " << blobfs_or.status_string();
    return zx::error(blobfs_or.status_value());
  }
  return zx::ok(std::move(blobfs_or.value()));
}

FsWalker::FsWalker(fbl::unique_fd input_fd, Extractor& extractor)
    : extractor_(extractor), input_fd_(std::move(input_fd)) {}

zx::result<std::unique_ptr<FsWalker>> FsWalker::Create(fbl::unique_fd input_fd,
                                                       Extractor& extractor) {
  auto walker = std::unique_ptr<FsWalker>(new FsWalker(std::move(input_fd), extractor));

  if (auto status = walker->LoadSuperblock(); status.is_error()) {
    std::cerr << "Loading superblock failed" << std::endl;
    return zx::error(status.error_value());
  }

  return zx::ok(std::move(walker));
}

zx::result<> FsWalker::Walk(async_dispatcher_t* dispatcher) {
  if (auto status = WalkPartition(); status.is_error()) {
    std::cerr << "Walking partition failed" << std::endl;
    return status;
  }

  if (auto status = WalkSegments(); status.is_error()) {
    std::cerr << "Walking segments failed" << std::endl;
    return status;
  }

  auto blob_or = CreateBlobfs(dispatcher);
  if (blob_or.is_error()) {
    std::cerr << "Creating Blobfs instance failed" << std::endl;
    return zx::error(blob_or.error_value());
  }
  return WalkBlobs(*blob_or.value());
}

zx::result<> FsWalker::WalkBlobs(blobfs::Blobfs& blobfs) const {
  for (unsigned n = 0; n < blobfs.Info().inode_count; n++) {
    auto inode_or = blobfs.GetNode(n);
    blobfs::Inode ino = *inode_or.value();
    blobfs::NodePrelude header = ino.header;
    if (header.IsAllocated() && header.IsInode()) {
      uint32_t alloc_block = 0;
      if (auto status = blobfs.LoadAndVerifyBlob(n) != ZX_OK) {
        if (auto status = ExtentBlockHandler(ino.extents[0]); status.is_error()) {
          return status;
        }
        alloc_block += ino.extents[0].Length();
        if (alloc_block < ino.block_count && header.next_node != 0) {
          return WalkExtentContainer(blobfs, header.next_node, alloc_block, ino);
        }
      }
    }
  }
  return zx::ok();
}

zx::result<> FsWalker::WalkExtentContainer(blobfs::Blobfs& blobfs, uint32_t node_num,
                                           uint32_t alloc_block, blobfs::Inode ino) const {
  auto inode_or = blobfs.GetNode(node_num);
  if (inode_or.is_error()) {
    return zx::error(inode_or.error_value());
  }
  blobfs::Inode inode = *inode_or.value();
  blobfs::ExtentContainer* node = inode.AsExtentContainer();
  blobfs::NodePrelude header = node->header;
  for (int i = 0; i < node->extent_count; i++) {
    if (auto status = ExtentBlockHandler(node->extents[i]); status.is_error()) {
      return status;
    }
    alloc_block += node->extents[i].Length();
  }
  if (alloc_block < ino.block_count && header.next_node != 0) {
    return WalkExtentContainer(blobfs, header.next_node, alloc_block, ino);
  }
  return zx::ok();
}

zx::result<> FsWalker::ExtentBlockHandler(blobfs::Extent extent) const {
  ExtentProperties properties = {.extent_kind = ExtentKind::Data,
                                 .data_kind = DataKind::Unmodified};

  if (auto status = extractor_.AddBlocks(extent.Start() + blobfs::DataStartBlock(info_),
                                         extent.Length(), properties);
      status.is_error()) {
    std::cerr << "FAIL: Dump corrupt blob" << std::endl;
    return status;
  }
  return zx::ok();
}

zx::result<> FsWalker::WalkPartition() const {
  auto max_offset = ByteLimit();
  ExtentProperties properties;
  if (!(info_.flags & blobfs::kBlobFlagFVM)) {
    // If this is a non-fvm fs, mark all blocks as unused. Other walkers will override it
    // later. Tnere are no unmapped blocks in non-fvm partition.
    properties.extent_kind = ExtentKind::Unused;
  } else {
    // If this is a fvm fs, mark all blocks as unmapped. Other walkers will override it
    // later. Tnere are no unmapped blocks in non-fvm partition.
    properties.extent_kind = ExtentKind::Unmmapped;
  }
  properties.data_kind = DataKind::Skipped;
  return extractor_.Add(0, max_offset, properties);
}

zx::result<> FsWalker::WalkSegments() const {
  ExtentProperties properties{.extent_kind = ExtentKind::Data, .data_kind = DataKind::Unmodified};
  if (auto status = extractor_.AddBlocks(blobfs::kSuperblockOffset, blobfs::kBlobfsSuperblockBlocks,
                                         properties);
      status.is_error()) {
    std::cerr << "FAIL: Add superblock" << std::endl;
    return status;
  }
  if (info_.flags & blobfs::kBlobFlagFVM) {
    if (auto status = extractor_.AddBlocks(blobfs::kFVMBackupSuperblockOffset,
                                           blobfs::kBlobfsSuperblockBlocks, properties);
        status.is_error()) {
      std::cerr << "FAIL: Add backup superblock" << std::endl;
      return status;
    }
  }
  if (auto status = extractor_.AddBlocks(blobfs::BlockMapStartBlock(info_),
                                         blobfs::BlockMapBlocks(info_), properties);
      status.is_error()) {
    std::cerr << "FAIL: Add blockmap" << std::endl;
    return status;
  }
  if (auto status = extractor_.AddBlocks(blobfs::NodeMapStartBlock(info_),
                                         blobfs::NodeMapBlocks(info_), properties);
      status.is_error()) {
    std::cerr << "FAIL: Add nodemap" << std::endl;
    return status;
  }
  if (auto status = extractor_.AddBlocks(blobfs::JournalStartBlock(info_),
                                         blobfs::JournalBlocks(info_), properties);
      status.is_error()) {
    std::cerr << "FAIL: Add journal" << std::endl;
    return status;
  }
  return zx::ok();
}

zx::result<> FsWalker::TryLoadSuperblock(uint64_t start_offset) {
  off_t pread_offset;
  if (!safemath::MakeCheckedNum<uint64_t>(start_offset)
           .Cast<off_t>()
           .AssignIfValid(&pread_offset)) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  if (pread(input_fd_.get(), &info_, sizeof(info_), pread_offset) != sizeof(info_)) {
    return zx::error(ZX_ERR_IO);
  }

  // Does info_ look like superblock?
  if (info_.magic0 == blobfs::kBlobfsMagic0 && info_.magic1 == blobfs::kBlobfsMagic1) {
    return zx::ok();
  }
  return zx::error(ZX_ERR_BAD_STATE);
}

zx::result<> FsWalker::LoadSuperblock() {
  ExtentProperties properties{.extent_kind = ExtentKind::Data, .data_kind = DataKind::Unmodified};
  auto load_status = TryLoadSuperblock(blobfs::kSuperblockOffset * blobfs::kBlobfsBlockSize);
  if (load_status.is_ok()) {
    return load_status;
  }
  // If we fail to load primary superblock, dump primary superblock
  if (auto status = extractor_.AddBlocks(blobfs::kSuperblockOffset, 1, properties);
      status.is_error()) {
    std::cerr << "FAIL: Add primary superblock" << std::endl;
    return status;
  }
  return load_status;
}

}  // namespace

zx::result<> BlobfsExtract(fbl::unique_fd input_fd, Extractor& extractor) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  if (zx_status_t status = loop.StartThread(); status != ZX_OK) {
    std::cerr << "Cannot initialize dispatch loop: " << zx_status_get_string(status);
    return zx::error(status);
  }

  auto walker_or = FsWalker::Create(std::move(input_fd), extractor);
  if (walker_or.is_error()) {
    std::cerr << "Walker: Init failure: " << walker_or.error_value() << std::endl;
    return zx::error(walker_or.error_value());
  }
  std::unique_ptr<FsWalker> walker = std::move(walker_or.value());
  return walker->Walk(loop.dispatcher());
}

}  // namespace extractor

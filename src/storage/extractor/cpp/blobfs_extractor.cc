// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/status.h>
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

#include "src/storage/blobfs/format.h"
#include "src/storage/extractor/c/extractor.h"
#include "src/storage/extractor/cpp/extractor.h"

namespace extractor {
namespace {

// Walks the file system and collects interesting metadata.
class FsWalker {
 public:
  static zx::status<std::unique_ptr<FsWalker>> Create(fbl::unique_fd input_fd,
                                                      Extractor& extractor);
  zx::status<> Walk() const;

 private:
  // Returns maximum addressable block in the fs.
  uint64_t BlockLimit() const { return blobfs::DataStartBlock(info_) + blobfs::DataBlocks(Info()); }

  // Returns maximum addressable byte in the fs.
  uint64_t ByteLimit() const { return BlockLimit() * Info().block_size; }

  // Walks the partition and marks all bytes as reported by ByteLimit() as unused for non-fvm
  // partition or unmapped for fvm partition.
  zx::status<> WalkPartition() const;

  // Walks different segments, like inode table and bitmaps except data segment, of the filesystem.
  // Marks them as data unmodified.
  zx::status<> WalkSegments() const;

  // Returns a reference to loaded superblock.
  const blobfs::Superblock& Info() const { return info_; }

  FsWalker(fbl::unique_fd input_fd, Extractor& extractor);

  // Not copyable or movable
  FsWalker(const FsWalker&) = delete;
  FsWalker& operator=(const FsWalker&) = delete;
  FsWalker(FsWalker&&) = delete;
  FsWalker& operator=(FsWalker&&) = delete;

  // Loads superblock located at start_offset. If the copy of superblock has valid
  // magic values, the function returns zx::ok().
  zx::status<> TryLoadSuperblock(uint64_t start_offset);

  // Loads one valid copy of superblock from the input_fd_.
  // Primary superblock location is given highest priority followed by backup superblock
  // of fvm partition and then non-fvm partition.
  zx::status<> LoadSuperblock();

  // Loads entire contents of inode table in memory.
  zx::status<> LoadInodeTable();

  // The valid copy of superblock.
  blobfs::Superblock info_;

  // Pointer to extractor.
  Extractor& extractor_;

  // File from where the filesystem is parsed/loaded.
  fbl::unique_fd input_fd_;

  // In-memory copy of inode table.
  std::unique_ptr<blobfs::Inode[]> inode_table_;
};

FsWalker::FsWalker(fbl::unique_fd input_fd, Extractor& extractor)
    : extractor_(extractor), input_fd_(std::move(input_fd)) {}

zx::status<std::unique_ptr<FsWalker>> FsWalker::Create(fbl::unique_fd input_fd,
                                                       Extractor& extractor) {
  auto walker = std::unique_ptr<FsWalker>(new FsWalker(std::move(input_fd), extractor));

  if (auto status = walker->LoadSuperblock(); status.is_error()) {
    std::cerr << "Loading superblock failed" << std::endl;
    return zx::error(status.error_value());
  }

  if (auto status = walker->LoadInodeTable(); status.is_error()) {
    std::cerr << "Loading inode table failed" << std::endl;
    return zx::error(status.error_value());
  }

  return zx::ok(std::move(walker));
}

zx::status<> FsWalker::Walk() const {
  if (auto status = WalkPartition(); status.is_error()) {
    std::cerr << "Walking partition failed" << std::endl;
    return status;
  }

  if (auto status = WalkSegments(); status.is_error()) {
    std::cerr << "Walking segments failed" << std::endl;
    return status;
  }
  return zx::ok();
}

zx::status<> FsWalker::WalkPartition() const {
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

zx::status<> FsWalker::WalkSegments() const {
  ExtentProperties properties{.extent_kind = ExtentKind::Data, .data_kind = DataKind::Unmodified};
  if (auto status = extractor_.AddBlocks(blobfs::kSuperblockOffset, blobfs::kBlobfsSuperblockBlocks,
                                         properties);
      status.is_error()) {
    std::cerr << "FAIL: Add superblock" << std::endl;
    return status;
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
  return zx::ok();
}

zx::status<> FsWalker::TryLoadSuperblock(uint64_t start_offset) {
  if (pread(input_fd_.get(), &info_, sizeof(info_), start_offset) != sizeof(info_)) {
    return zx::error(ZX_ERR_IO);
  }

  // Does info_ look like superblock?
  if (info_.magic0 == blobfs::kBlobfsMagic0 && info_.magic1 == blobfs::kBlobfsMagic1) {
    return zx::ok();
  }
  return zx::error(ZX_ERR_BAD_STATE);
}

zx::status<> FsWalker::LoadSuperblock() {
  return TryLoadSuperblock(blobfs::kSuperblockOffset * blobfs::kBlobfsBlockSize);
}

zx::status<> FsWalker::LoadInodeTable() {
  inode_table_ =
      std::make_unique<blobfs::Inode[]>(NodeMapBlocks(Info()) * blobfs::kBlobfsInodesPerBlock);
  ssize_t size = blobfs::NodeMapBlocks(Info()) * blobfs::kBlobfsBlockSize;
  if (pread(input_fd_.get(), inode_table_.get(), size,
            blobfs::NodeMapStartBlock(info_) * blobfs::kBlobfsBlockSize) != size) {
    return zx::error(ZX_ERR_IO);
  }
  return zx::ok();
}

}  // namespace

zx::status<> BlobfsExtract(fbl::unique_fd input_fd, Extractor& extractor) {
  auto walker_or = FsWalker::Create(std::move(input_fd), extractor);
  if (walker_or.is_error()) {
    std::cerr << "Walker: Init failure: " << walker_or.error_value() << std::endl;
    return zx::error(walker_or.error_value());
  }
  std::unique_ptr<FsWalker> walker = std::move(walker_or.value());

  return walker->Walk();
}

}  // namespace extractor

// Copyright 2020 The Fuchsia Authors. All rights reserved.
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
#include <safemath/safe_math.h>

#include "src/storage/extractor/c/extractor.h"
#include "src/storage/extractor/cpp/extractor.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/fsck.h"

namespace extractor {
namespace {

// Returns true if type of block belonging to an inode may contain pii.
bool IsPii(const minfs::Inode& inode, minfs::BlockType type) {
  return inode.magic == minfs::kMinfsMagicFile && type == minfs::BlockType::kDirect;
}

// Walks the file system and collects interesting metadata.
class FsWalker {
 public:
  static zx::result<std::unique_ptr<FsWalker>> Create(fbl::unique_fd input_fd,
                                                      Extractor& extractor);
  zx::result<> Walk() const;

 private:
  zx::result<> ReadBlock(uint64_t block_number, uint8_t* buf) const {
    off_t offset;
    if (!safemath::CheckMul(block_number, Info().block_size).AssignIfValid(&offset)) {
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }

    if (pread(input_fd_.get(), buf, Info().block_size, offset) != Info().block_size) {
      return zx::error(ZX_ERR_IO);
    }
    return zx::ok();
  }

  // Returns true if the block_number is in addressable range. For non-fvm based partition, it means
  // the block number is less than the partition size. For fvm based partition, this means the
  // block_number is within some allocated/mapped slice range.
  bool IsMapped(uint32_t block_number) const;

  // All the block numbers stored in double indirect and indirect blocks are relative to
  // Superblock.DataStartBlock(). This helper routine converts such block numbers to absolute block
  // number.
  zx::result<uint32_t> DataBlockToAbsoluteBlock(uint32_t n) const {
    auto blk_or = safemath::CheckAdd(n, static_cast<uint32_t>(Info().DataStartBlock()));
    if (blk_or.IsValid()) {
      return zx::ok(blk_or.ValueOrDie());
    }
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  // Given a block that belongs to a file/directory, this function adds the block to extractor with
  // right set of properties.
  zx::result<> InodeBlockHandler(uint32_t block_number, bool pii) const;

  // Walks indirect and double indirect block at block_number.
  zx::result<> WalkXkIndirects(const minfs::Inode& inode, minfs::ino_t ino, uint32_t block_number,
                               bool is_double_indirect) const;

  // Returns reference to inode for given inode number
  const minfs::Inode& GetInode(minfs::ino_t inode_number) const {
    ZX_ASSERT(inode_number < Info().inode_count);
    return inode_table_[inode_number];
  }

  // Walks all in-use inode and calls handler on those.
  // "in-use" here means the file is marked as allocated in inode bitmap table.
  zx::result<> WalkInodes() const;

  // Returns maximum addressable block in the fs.
  uint64_t BlockLimit() const { return Info().DataStartBlock() + DataBlocks(Info()); }

  // Returns maximum addressable byte in the fs.
  uint64_t ByteLimit() const { return BlockLimit() * Info().block_size; }

  // Walks the partition and marks all bytes as reported by ByteLimit() as unused for non-fvm
  // partition or unmapped for fvm partition.
  zx::result<> WalkPartition() const;

  // Walks different segments, like inode table and bitmaps except data segment, of the filesystem.
  // Marks them as data unmodified.
  zx::result<> WalkSegments() const;

  // Returns a reference to loaded superblock.
  const minfs::Superblock& Info() const { return info_; }

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

  // Loads entire contents of inode table in memory.
  zx::result<> LoadInodeTable();

  // The valid copy of superblock.
  minfs::Superblock info_;

  // Pointer to extractor.
  Extractor& extractor_;

  // File from where the filesystem is parsed/loaded.
  fbl::unique_fd input_fd_;

  // In-memory copy of inode table.
  std::unique_ptr<minfs::Inode[]> inode_table_;
};

FsWalker::FsWalker(fbl::unique_fd input_fd, Extractor& extractor)
    : extractor_(extractor), input_fd_(std::move(input_fd)) {}

zx::result<std::unique_ptr<FsWalker>> FsWalker::Create(fbl::unique_fd input_fd,
                                                       Extractor& extractor) {
  auto walker = std::unique_ptr<FsWalker>(new FsWalker(std::move(input_fd), extractor));

  if (auto status = walker->LoadSuperblock(); status.is_error()) {
    std::cerr << "Loading superblock failed\n";
    return zx::error(status.error_value());
  }
  if (auto status = walker->LoadInodeTable(); status.is_error()) {
    return zx::error(status.error_value());
  }
  return zx::ok(std::move(walker));
}

zx::result<> FsWalker::Walk() const {
  if (auto status = WalkPartition(); status.is_error()) {
    return status;
  }

  if (auto status = WalkSegments(); status.is_error()) {
    return status;
  }

  return WalkInodes();
}

bool FsWalker::IsMapped(uint32_t block_number) const {
  auto info = Info();
  if (block_number > BlockLimit()) {
    return false;
  }

  if (block_number == minfs::kSuperblockStart || block_number == info.BackupSuperblockStart()) {
    return true;
  }
  if (block_number >= info.InodeBitmapStartBlock() &&
      block_number < (info.InodeBitmapStartBlock() + minfs::InodeBitmapBlocks(Info()))) {
    return true;
  }
  if (block_number >= info.DataBitmapStartBlock() &&
      block_number < (info.DataBitmapStartBlock() + minfs::BlockBitmapBlocks(Info()))) {
    return true;
  }
  if (block_number >= info.InodeTableStartBlock() &&
      block_number < (info.InodeTableStartBlock() + minfs::InodeBlocks(Info()))) {
    return true;
  }
  if (block_number >= minfs::JournalStartBlock(info) &&
      block_number < (minfs::JournalStartBlock(info) + minfs::JournalBlocks(Info()))) {
    return true;
  }
  if (block_number >= info.DataStartBlock() &&
      block_number < (info.DataStartBlock() + minfs::DataBlocks(Info()))) {
    return true;
  }
  return false;
}

zx::result<> FsWalker::InodeBlockHandler(uint32_t block_number, bool pii) const {
  ExtentProperties properties = {.extent_kind = ExtentKind::Data,
                                 .data_kind = DataKind::Unmodified};

  ZX_ASSERT(block_number > Info().DataStartBlock());

  if (pii) {
    properties.extent_kind = ExtentKind::Pii;
  }

  if (!IsMapped(block_number)) {
    properties.data_kind = DataKind::Skipped;
  }

  return extractor_.AddBlock(block_number, properties);
}

zx::result<> FsWalker::WalkXkIndirects(const minfs::Inode& inode, minfs::ino_t ino,
                                       uint32_t block_number, bool is_double_indirect) const {
  ZX_ASSERT(block_number >= Info().DataStartBlock());
  if (block_number == Info().DataStartBlock()) {
    return zx::ok();
  }

  if (auto status = InodeBlockHandler(block_number, IsPii(inode, minfs::BlockType::kIndirect));
      status.is_error()) {
    return status;
  }

  // If this block is not mapped then we are done here.
  if (!IsMapped(block_number)) {
    return zx::ok();
  }

  uint8_t data[Info().BlockSize()];
  if (auto status = ReadBlock(block_number, data); status.is_error()) {
    return status;
  }
  uint32_t* entry = reinterpret_cast<uint32_t*>(data);
  for (unsigned i = 0; i < minfs::kMinfsDirectPerIndirect; i++) {
    if (entry[i] == 0) {
      continue;
    }
    auto absolute_block_number_or = DataBlockToAbsoluteBlock(entry[i]);
    if (absolute_block_number_or.is_error()) {
      continue;
    }
    if (!is_double_indirect) {
      if (auto status = InodeBlockHandler(absolute_block_number_or.value(),
                                          IsPii(inode, minfs::BlockType::kDirect));
          status.is_error()) {
        return status;
      }
    } else {
      if (auto status = WalkXkIndirects(inode, ino, absolute_block_number_or.value(), false);
          status.is_error()) {
        return status;
      }
    }
  }
  return zx::ok();
}

zx::result<> FsWalker::WalkInodes() const {
  for (minfs::ino_t ino = 0; ino < Info().inode_count; ino++) {
    auto inode = GetInode(ino);
    if (inode.magic != minfs::kMinfsMagicFile && inode.magic != minfs::kMinfsMagicDir) {
      continue;
    }

    for (uint32_t n : inode.dnum) {
      if (n == 0) {
        continue;
      }
      auto absolute_block_number_or = DataBlockToAbsoluteBlock(n);
      if (absolute_block_number_or.is_error()) {
        continue;
      }
      if (auto status = InodeBlockHandler(absolute_block_number_or.value(),
                                          IsPii(inode, minfs::BlockType::kDirect));
          status.is_error()) {
        return status;
      }
    }

    // Walk indirect blocks.
    for (unsigned int n : inode.inum) {
      auto absolute_block_number_or = DataBlockToAbsoluteBlock(n);
      if (absolute_block_number_or.is_error()) {
        continue;
      }
      if (auto status = WalkXkIndirects(inode, ino, absolute_block_number_or.value(), false);
          status.is_error()) {
        return status;
      }
    }

    // Walk double indirect blocks.
    for (unsigned int n : inode.dinum) {
      auto absolute_block_number_or = DataBlockToAbsoluteBlock(n);
      if (absolute_block_number_or.is_error()) {
        continue;
      }
      if (auto status = WalkXkIndirects(inode, ino, absolute_block_number_or.value(), true);
          status.is_error()) {
        return status;
      }
    }
  }
  return zx::ok();
}

zx::result<> FsWalker::WalkPartition() const {
  auto max_offset = ByteLimit();
  ExtentProperties properties;
  if (!Info().GetFlagFvm()) {
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
  auto info = Info();

  ExtentProperties properties{.extent_kind = ExtentKind::Data, .data_kind = DataKind::Unmodified};

  if (auto status =
          extractor_.AddBlocks(minfs::kSuperblockStart, minfs::kSuperblockBlocks, properties);
      status.is_error()) {
    return status;
  }
  if (auto status =
          extractor_.AddBlocks(info.BackupSuperblockStart(), minfs::kSuperblockBlocks, properties);
      status.is_error()) {
    return status;
  }
  if (auto status =
          extractor_.AddBlocks(info.InodeBitmapStartBlock(), InodeBitmapBlocks(info), properties);
      status.is_error()) {
    return status;
  }
  if (auto status =
          extractor_.AddBlocks(info.DataBitmapStartBlock(), BlockBitmapBlocks(info), properties);
      status.is_error()) {
    return status;
  }
  if (auto status =
          extractor_.AddBlocks(info.InodeTableStartBlock(), InodeBlocks(info), properties);
      status.is_error()) {
    return status;
  }
  if (auto status =
          extractor_.AddBlocks(minfs::JournalStartBlock(info), JournalBlocks(info), properties);
      status.is_error()) {
    return status;
  }

  // Mark all data blocks as unused/skipped.
  properties.extent_kind = ExtentKind::Unused;
  properties.data_kind = DataKind::Skipped;
  return extractor_.AddBlocks(info_.DataStartBlock(), DataBlocks(info), properties);
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
  if (info_.magic0 == minfs::kMinfsMagic0 && info_.magic1 == minfs::kMinfsMagic1) {
    return zx::ok();
  }
  return zx::error(ZX_ERR_BAD_STATE);
}

zx::result<> FsWalker::LoadSuperblock() {
  if (auto status = TryLoadSuperblock(minfs::kSuperblockStart * minfs::kMinfsBlockSize);
      status.is_ok()) {
    return status;
  }
  if (auto status = TryLoadSuperblock(minfs::kFvmSuperblockBackup * minfs::kMinfsBlockSize);
      status.is_ok()) {
    std::cerr << "Found fvm backup superblock valid\n";
    return status;
  }

  auto status = TryLoadSuperblock(minfs::kNonFvmSuperblockBackup * minfs::kMinfsBlockSize);
  if (status.is_ok()) {
    std::cerr << "Found non-fvm backup superblock valid\n";
  }
  return status;
}

zx::result<> FsWalker::LoadInodeTable() {
  inode_table_ =
      std::make_unique<minfs::Inode[]>(InodeBlocks(Info()) * minfs::kMinfsInodesPerBlock);
  auto size = safemath::checked_cast<ssize_t>(InodeBlocks(Info()) * Info().BlockSize());
  if (pread(input_fd_.get(), inode_table_.get(), size,
            safemath::checked_cast<off_t>(info_.InodeTableStartBlock() * Info().BlockSize())) !=
      size) {
    return zx::error(ZX_ERR_IO);
  }
  return zx::ok();
}

}  // namespace

zx::result<> MinfsExtract(fbl::unique_fd input_fd, Extractor& extractor) {
  auto walker_or = FsWalker::Create(std::move(input_fd), extractor);
  if (walker_or.is_error()) {
    std::cerr << "Walker: Init failure: " << walker_or.error_value() << std::endl;
    return zx::error(walker_or.error_value());
  }
  std::unique_ptr<FsWalker> walker = std::move(walker_or.value());

  return walker->Walk();
}

}  // namespace extractor

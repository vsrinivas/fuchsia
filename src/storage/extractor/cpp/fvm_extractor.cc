// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/fd.h>
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
#include <safemath/safe_math.h>

#include "src/storage/extractor/c/extractor.h"
#include "src/storage/extractor/cpp/extractor.h"
#include "src/storage/fvm/format.h"

namespace extractor {
namespace {

// Walks the fvm and collects interesting metadata.
class FvmWalker {
 public:
  static zx::result<std::unique_ptr<FvmWalker>> Create(fbl::unique_fd input_fd,
                                                       Extractor& extractor);

  zx::result<> Walk();

 private:
  // Returns maximum addressable byte in the fvm.
  uint64_t ByteLimit() const { return info_.fvm_partition_size; }

  // Walks the fvm partition and marks all bytes as reported by ByteLimit() as unmapped
  zx::result<> WalkPartition() const;

  // Walks different segments, like partition table and allocation table, of the fvm partition.
  // Marks them as data unmodified.
  zx::result<> WalkSegments() const;

  const fvm::Header& Info() const { return info_; }

  FvmWalker(fbl::unique_fd input_fd, Extractor& extractor);

  // Not copyable or movable
  FvmWalker(const FvmWalker&) = delete;
  FvmWalker& operator=(const FvmWalker&) = delete;
  FvmWalker(FvmWalker&&) = delete;
  FvmWalker& operator=(FvmWalker&&) = delete;

  // Loads superblock located at start_offset. If the copy of superblock has valid
  // magic values, the function returns zx::ok().
  zx::result<> TryLoadSuperblock(uint64_t start_offset);

  // Tries to load the primary superblock. If primary superblock is corrupt,
  // dumps the first copy of metadata based on max sizes for the
  // partition and allocation tables
  zx::result<> LoadSuperblock();

  // The primary superblock, if valid
  fvm::Header info_;

  // Pointer to extractor.
  Extractor& extractor_;

  // File from where the fvm is parsed/loaded.
  fbl::unique_fd input_fd_;
};

FvmWalker::FvmWalker(fbl::unique_fd input_fd, Extractor& extractor)
    : extractor_(extractor), input_fd_(std::move(input_fd)) {}

zx::result<std::unique_ptr<FvmWalker>> FvmWalker::Create(fbl::unique_fd input_fd,
                                                         Extractor& extractor) {
  auto walker = std::unique_ptr<FvmWalker>(new FvmWalker(std::move(input_fd), extractor));

  if (auto status = walker->LoadSuperblock(); status.is_error()) {
    std::cerr << "Loading primary superblock failed" << std::endl;
    return zx::error(status.error_value());
  }
  return zx::ok(std::move(walker));
}

zx::result<> FvmWalker::Walk() {
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

zx::result<> FvmWalker::WalkPartition() const {
  auto max_offset = ByteLimit();
  ExtentProperties properties{.extent_kind = ExtentKind::Unmmapped, .data_kind = DataKind::Skipped};
  return extractor_.Add(0, max_offset, properties);
}

zx::result<> FvmWalker::WalkSegments() const {
  ExtentProperties properties{.extent_kind = ExtentKind::Data, .data_kind = DataKind::Unmodified};
  if (auto status = extractor_.AddBlocks(info_.GetSuperblockOffset(fvm::SuperblockType::kPrimary),
                                         1, properties);
      status.is_error()) {
    std::cerr << "FAIL: Add first superblock copy" << std::endl;
    return status;
  }
  if (auto status =
          extractor_.AddBlocks(info_.GetPartitionTableOffset() / fvm::kBlockSize,
                               info_.GetPartitionTableByteSize() / fvm::kBlockSize, properties);
      status.is_error()) {
    std::cerr << "FAIL: Add first partition table copy" << std::endl;
    return status;
  }
  if (auto status = extractor_.AddBlocks(info_.GetAllocationTableOffset() / fvm::kBlockSize,
                                         info_.GetAllocationTableUsedByteSize() / fvm::kBlockSize,
                                         properties);
      status.is_error()) {
    std::cerr << "FAIL: Add first allocation table copy" << std::endl;
    return status;
  }
  size_t secondarySuperblockOffset =
      info_.GetSuperblockOffset(fvm::SuperblockType::kSecondary) / fvm::kBlockSize;
  if (auto status = extractor_.AddBlocks(secondarySuperblockOffset, 1, properties);
      status.is_error()) {
    std::cerr << "FAIL: Add second superblock copy" << std::endl;
    return status;
  }
  if (auto status = extractor_.AddBlocks(
          secondarySuperblockOffset + info_.GetPartitionTableOffset() / fvm::kBlockSize,
          info_.GetPartitionTableByteSize() / fvm::kBlockSize, properties);
      status.is_error()) {
    std::cerr << "FAIL: Add second partition table copy" << std::endl;
    return status;
  }
  if (auto status = extractor_.AddBlocks(
          secondarySuperblockOffset + info_.GetAllocationTableOffset() / fvm::kBlockSize,
          info_.GetAllocationTableUsedByteSize() / fvm::kBlockSize, properties);
      status.is_error()) {
    std::cerr << "FAIL: Add second allocation table copy" << std::endl;
    return status;
  }
  return zx::ok();
}

zx::result<> FvmWalker::TryLoadSuperblock(uint64_t start_offset) {
  off_t pread_offset;
  if (!safemath::MakeCheckedNum(start_offset).Cast<off_t>().AssignIfValid(&pread_offset)) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  char buffer[fvm::kBlockSize];
  pread(input_fd_.get(), buffer, fvm::kBlockSize, pread_offset);
  memcpy(&info_, buffer, sizeof(info_));
  if (info_.magic == fvm::kMagic) {
    return zx::ok();
  }
  std::cerr << "Magic does not match" << std::endl;
  return zx::error(ZX_ERR_BAD_STATE);
}

zx::result<> FvmWalker::LoadSuperblock() {
  auto load_status = TryLoadSuperblock(0);
  if (load_status.is_ok()) {
    return load_status;
  }
  // If we fail to load the superblock, just dump the primary superblock
  ExtentProperties properties{.extent_kind = ExtentKind::Data, .data_kind = DataKind::Unmodified};
  if (auto status = extractor_.AddBlocks(0, 1, properties); status.is_error()) {
    std::cerr << "FAIL: Add primary superblock" << std::endl;
    return status;
  }
  return load_status;
}

}  // namespace

zx::result<> FvmExtract(fbl::unique_fd input_fd, Extractor& extractor) {
  auto walker_or = FvmWalker::Create(std::move(input_fd), extractor);
  if (walker_or.is_error()) {
    std::cerr << "Walker: Init failure: " << walker_or.error_value() << std::endl;
    return zx::error(walker_or.error_value());
  }
  std::unique_ptr<FvmWalker> walker = std::move(walker_or.value());
  return walker->Walk();
}

}  // namespace extractor

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_BLOBFS_CHECKER_H_
#define SRC_STORAGE_BLOBFS_BLOBFS_CHECKER_H_

#ifdef __Fuchsia__
#include "blobfs.h"
#else
#include <blobfs/host.h>
#endif

#include <memory>

namespace blobfs {

class BlobfsChecker {
 public:
  struct Options {
    // If true, repair simple issues.
    bool repair = true;
  };

  explicit BlobfsChecker(std::unique_ptr<Blobfs> blobfs) : BlobfsChecker(std::move(blobfs), {}) {}
  explicit BlobfsChecker(std::unique_ptr<Blobfs> blobfs, Options option);

  BlobfsChecker(const BlobfsChecker&) = delete;
  BlobfsChecker& operator=(const BlobfsChecker&) = delete;

  // Initialize validates the underlying FVM partition and optionally replays the journal.
  zx_status_t Initialize(bool apply_journal);

  // Check validates the blobfs filesystem provided when the Checker was
  // constructed. It walks each of the inode and block allocation bitmaps
  // only once.
  zx_status_t Check();

 private:
  std::unique_ptr<Blobfs> blobfs_;
  uint32_t alloc_inodes_ = 0;
  uint32_t alloc_blocks_ = 0;
  uint32_t error_blobs_ = 0;
  uint32_t inode_blocks_ = 0;
  const Options options_;

  void TraverseInodeBitmap();
  void TraverseBlockBitmap();
  zx_status_t CheckAllocatedCounts() const;
};

#ifdef __Fuchsia__
// Validate that the contents of the superblock matches the results claimed in the underlying
// volume manager.
//
// If the results are inconsistent, update the FVM's allocation accordingly.
zx_status_t CheckFvmConsistency(const Superblock* info, BlockDevice* device, bool repair);
#endif  // __Fuchsia__

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_BLOBFS_CHECKER_H_

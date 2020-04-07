// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <thread>

#include <blobfs/common.h>
#include <fbl/auto_call.h>
#include <fvm/format.h>
#include <zxtest/zxtest.h>

#include "blobfs_fixtures.h"
#include "load_generator.h"

namespace {

namespace fio = ::llcpp::fuchsia::io;

using blobfs::BlobInfo;
using blobfs::GenerateBlob;
using blobfs::GenerateRandomBlob;
using blobfs::RandomFill;
using blobfs::StreamAll;
using blobfs::VerifyContents;
using fs::FilesystemTest;
using fs::RamDisk;

class LargeBlobTest : public BlobfsFixedDiskSizeTest {
 public:
  LargeBlobTest() : BlobfsFixedDiskSizeTest(GetDiskSize()) {}

  static uint64_t GetDataBlockCount() { return 12 * blobfs::kBlobfsBlockBits / 10; }

 private:
  static uint64_t GetDiskSize() {
    // Create blobfs with enough data blocks to ensure 2 block bitmap blocks.
    // Any number above kBlobfsBlockBits should do, and the larger the
    // number, the bigger the disk (and memory used for the test).
    blobfs::Superblock superblock;
    superblock.flags = 0;
    superblock.inode_count = blobfs::kBlobfsDefaultInodeCount;
    superblock.journal_block_count = blobfs::kDefaultJournalBlocks;
    superblock.data_block_count = GetDataBlockCount();
    return blobfs::TotalBlocks(superblock) * blobfs::kBlobfsBlockSize;
  }
};

TEST_F(LargeBlobTest, UseSecondBitmap) {
  // Create (and delete) a blob large enough to overflow into the second bitmap block.
  std::unique_ptr<BlobInfo> info;
  size_t blob_size = ((GetDataBlockCount() / 2) + 1) * blobfs::kBlobfsBlockSize;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, blob_size, &info));

  fbl::unique_fd fd;
  ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));
  ASSERT_EQ(syncfs(fd.get()), 0);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(info->path), 0);
}

}  // namespace

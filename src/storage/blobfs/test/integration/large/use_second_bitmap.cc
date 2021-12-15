// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <thread>

#include <gtest/gtest.h>

#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/compression_settings.h"
#include "src/storage/blobfs/test/blob_utils.h"
#include "src/storage/blobfs/test/integration/blobfs_fixtures.h"

namespace blobfs {
namespace {

fs_test::TestFilesystemOptions TestParams(uint64_t disk_size) {
  auto options = BlobfsWithFixedDiskSizeTestParam(disk_size);
  // Disabling compression speeds up the test.  Since we want to generate an uncompressible blob to
  // take up the maximum amount of space anyways, compression is wasted effort.
  options.blob_compression_algorithm = CompressionAlgorithm::kUncompressed;
  return options;
}

class LargeBlobTest : public BaseBlobfsTest {
 public:
  LargeBlobTest() : BaseBlobfsTest(TestParams(GetDiskSize())) {}

  static uint64_t GetDataBlockCount() { return kBlobfsBlockBits + 1; }

 private:
  static uint64_t GetDiskSize() {
    // Create blobfs with enough data blocks to ensure 2 block bitmap blocks.
    // Any number above kBlobfsBlockBits should do, and the larger the
    // number, the bigger the disk (and memory used for the test).
    Superblock superblock;
    superblock.flags = 0;
    superblock.inode_count = kBlobfsDefaultInodeCount;
    superblock.journal_block_count = kDefaultJournalBlocks;
    superblock.data_block_count = GetDataBlockCount();
    return TotalBlocks(superblock) * kBlobfsBlockSize;
  }
};

TEST_F(LargeBlobTest, UseSecondBitmap) {
  // Create (and delete) a blob large enough to overflow into the second bitmap block.
  size_t blob_size = ((GetDataBlockCount() / 2) + 1) * kBlobfsBlockSize;
  std::unique_ptr<BlobInfo> info =
      GenerateBlob([](void*, size_t) { /* NOP */ }, fs().mount_path(), blob_size);

  fbl::unique_fd fd;
  fprintf(stderr, "Writing %zu bytes...\n", blob_size);
  ASSERT_NO_FATAL_FAILURE(MakeBlob(*info, &fd));
  fprintf(stderr, "Done writing %zu bytes\n", blob_size);
  ASSERT_EQ(syncfs(fd.get()), 0);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(info->path), 0);
}

}  // namespace
}  // namespace blobfs

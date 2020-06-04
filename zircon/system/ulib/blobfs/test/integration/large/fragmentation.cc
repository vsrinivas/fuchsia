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

// The following test attempts to fragment the underlying blobfs partition
// assuming a trivial linear allocator. A more intelligent allocator may require
// modifications to this test.
void RunFragmentationTest(FilesystemTest* test) {
  // Keep generating blobs until we run out of space, in a pattern of large,
  // small, large, small, large.
  //
  // At the end of  the test, we'll free the small blobs, and observe if it is
  // possible to allocate a larger blob. With a simple allocator and no
  // defragmentation, this would result in a NO_SPACE error.
  constexpr size_t kSmallSize = (1 << 16);
  constexpr size_t kLargeSize = (1 << 17);

  fbl::Vector<fbl::String> small_blobs;

  bool do_small_blob = true;
  size_t count = 0;
  while (true) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FAILURES(
        GenerateRandomBlob(kMountPath, do_small_blob ? kSmallSize : kLargeSize, &info));
    fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd, "Failed to create blob");
    ASSERT_EQ(0, ftruncate(fd.get(), info->size_data));
    if (StreamAll(write, fd.get(), info->data.get(), info->size_data) < 0) {
      ASSERT_EQ(ENOSPC, errno, "Blobfs expected to run out of space");
      break;
    }
    if (do_small_blob) {
      small_blobs.push_back(fbl::String(info->path));
    }

    do_small_blob = !do_small_blob;

    if (++count % 50 == 0) {
      fprintf(stderr, "Allocated %lu blobs\n", count);
    }
  }

  // We have filled up the disk with both small and large blobs.
  // Observe that we cannot add another large blob.
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, kLargeSize, &info));

  // Calculate actual number of blocks required to store the blob (including the merkle tree).
  blobfs::Inode large_inode;
  large_inode.blob_size = kLargeSize;
  size_t kLargeBlocks =
      blobfs::ComputeNumMerkleTreeBlocks(large_inode) + blobfs::BlobDataBlocks(large_inode);

  // We shouldn't have space (before we try allocating) ...
  fio::FilesystemInfo usage;
  ASSERT_NO_FAILURES(test->GetFsInfo(&usage));
  ASSERT_LT(usage.total_bytes - usage.used_bytes, kLargeBlocks * blobfs::kBlobfsBlockSize);

  // ... and we don't have space (as we try allocating).
  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd);
  ASSERT_EQ(0, ftruncate(fd.get(), info->size_data));
  ASSERT_NE(0, StreamAll(write, fd.get(), info->data.get(), info->size_data));
  ASSERT_EQ(ENOSPC, errno, "Blobfs expected to be out of space");
  fd.reset();

  // Unlink all small blobs -- except for the last one, since we may have free
  // trailing space at the end.
  for (size_t i = 0; i < small_blobs.size() - 1; i++) {
    ASSERT_EQ(0, unlink(small_blobs[i].c_str()), "Unlinking old blob");
  }

  // This asserts an assumption of our test: Freeing these blobs should provide
  // enough space.
  ASSERT_GT(kSmallSize * (small_blobs.size() - 1), kLargeSize);

  // Validate that we have enough space (before we try allocating)...
  ASSERT_NO_FAILURES(test->GetFsInfo(&usage));
  ASSERT_GE(usage.total_bytes - usage.used_bytes, kLargeBlocks * blobfs::kBlobfsBlockSize);

  fd.reset(open(info->path, O_CREAT | O_RDWR));
  // Now that blobfs supports extents, verify that we can still allocate a large
  // blob, even if it is fragmented.
  ASSERT_EQ(0, ftruncate(fd.get(), info->size_data));

  // Sanity check that we can write and read the fragmented blob.
  ASSERT_EQ(0, StreamAll(write, fd.get(), info->data.get(), info->size_data));
  std::unique_ptr<char[]> buf(new char[info->size_data]);
  ASSERT_EQ(0, lseek(fd.get(), 0, SEEK_SET));
  ASSERT_EQ(0, StreamAll(read, fd.get(), buf.get(), info->size_data));
  ASSERT_BYTES_EQ(info->data.get(), buf.get(), info->size_data);

  // Sanity check that we can re-open and unlink the fragmented blob.
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd);
  ASSERT_EQ(0, unlink(info->path));
}

TEST_F(BlobfsTest, Fragmentation) { RunFragmentationTest(this); }

TEST_F(BlobfsTestWithFvm, Fragmentation) { RunFragmentationTest(this); }

}  // namespace

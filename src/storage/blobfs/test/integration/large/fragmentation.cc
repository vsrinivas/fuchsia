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
#include <vector>

#include <gtest/gtest.h>

#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/test/integration/blobfs_fixtures.h"

namespace blobfs {
namespace {

using FragmentationTest = ParameterizedBlobfsTest;
// The following test attempts to fragment the underlying blobfs partition
// assuming a trivial linear allocator. A more intelligent allocator may require
// modifications to this test.
TEST_P(FragmentationTest, Fragmentation) {
  // Keep generating blobs until we run out of space, in a pattern of large,
  // small, large, small, large.
  //
  // At the end of  the test, we'll free the small blobs, and observe if it is
  // possible to allocate a larger blob. With a simple allocator and no
  // defragmentation, this would result in a NO_SPACE error.
  constexpr size_t kSmallSize = (1 << 16);
  constexpr size_t kLargeSize = (1 << 17);

  std::vector<fbl::String> small_blobs;

  bool do_small_blob = true;
  bool capture_large_blob_storage_space_usage = true;
  size_t large_blob_storage_space_usage = 0;
  size_t count = 0;
  while (true) {
    std::unique_ptr<BlobInfo> info =
        GenerateRandomBlob(fs().mount_path(), do_small_blob ? kSmallSize : kLargeSize);
    fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
    ASSERT_TRUE(fd) << "Failed to create blob";
    if (capture_large_blob_storage_space_usage && !do_small_blob) {
      // Record how much space was used by blobfs before writing a large blob.
      const zx::result fs_info = fs().GetFsInfo();
      ASSERT_TRUE(fs_info.is_ok()) << fs_info.status_string();
      large_blob_storage_space_usage = fs_info.value().used_bytes;
    }
    ASSERT_EQ(0, ftruncate(fd.get(), info->size_data));
    if (StreamAll(write, fd.get(), info->data.get(), info->size_data) < 0) {
      ASSERT_EQ(ENOSPC, errno) << "Blobfs expected to run out of space";
      break;
    }
    if (capture_large_blob_storage_space_usage && !do_small_blob) {
      // Determine how much space was required to store the large by blob by comparing blobfs'
      // space usage before and after writing the blob.
      const zx::result fs_info = fs().GetFsInfo();
      ASSERT_TRUE(fs_info.is_ok()) << fs_info.status_string();
      large_blob_storage_space_usage = fs_info.value().used_bytes - large_blob_storage_space_usage;
      capture_large_blob_storage_space_usage = false;
    }
    if (do_small_blob) {
      small_blobs.emplace_back(info->path);
    }

    do_small_blob = !do_small_blob;

    if (++count % 50 == 0) {
      fprintf(stderr, "Allocated %lu blobs\n", count);
    }
  }

  // We have filled up the disk with both small and large blobs.
  // Observe that we cannot add another large blob.
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), kLargeSize);

  // ... and we don't have space (as we try allocating).
  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);
  ASSERT_EQ(0, ftruncate(fd.get(), info->size_data));
  ASSERT_NE(0, StreamAll(write, fd.get(), info->data.get(), info->size_data));
  ASSERT_EQ(ENOSPC, errno) << "Blobfs expected to be out of space";
  fd.reset();

  // Unlink all small blobs -- except for the last one, since we may have free
  // trailing space at the end.
  for (size_t i = 0; i < small_blobs.size() - 1; i++) {
    ASSERT_EQ(0, unlink(small_blobs[i].c_str())) << "Unlinking old blob";
  }

  // This asserts an assumption of our test: Freeing these blobs should provide
  // enough space.
  ASSERT_GT(kSmallSize * (small_blobs.size() - 1), kLargeSize);

  // Validate that we have enough space (before we try allocating)...
  const zx::result fs_info = fs().GetFsInfo();
  ASSERT_TRUE(fs_info.is_ok()) << fs_info.status_string();
  ASSERT_GE(fs_info.value().total_bytes - fs_info.value().used_bytes,
            large_blob_storage_space_usage);

  fd.reset(open(info->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  // Now that blobfs supports extents, verify that we can still allocate a large
  // blob, even if it is fragmented.
  ASSERT_EQ(0, ftruncate(fd.get(), info->size_data));

  // Sanity check that we can write and read the fragmented blob.
  ASSERT_EQ(0, StreamAll(write, fd.get(), info->data.get(), info->size_data));
  std::unique_ptr<char[]> buf(new char[info->size_data]);
  ASSERT_EQ(0, lseek(fd.get(), 0, SEEK_SET));
  ASSERT_EQ(0, StreamAll(read, fd.get(), buf.get(), info->size_data));
  ASSERT_EQ(memcmp(info->data.get(), buf.get(), info->size_data), 0);

  // Sanity check that we can re-open and unlink the fragmented blob.
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd);
  ASSERT_EQ(0, unlink(info->path));
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, FragmentationTest,
                         testing::Values(BlobfsDefaultTestParam(), BlobfsWithFvmTestParam(),
                                         BlobfsWithPaddedLayoutTestParam()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace blobfs

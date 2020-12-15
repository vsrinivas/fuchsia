// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <fbl/auto_call.h>
#include <gtest/gtest.h>

#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/test/integration/blobfs_fixtures.h"
#include "src/storage/fvm/format.h"

namespace blobfs {
namespace {

TEST_F(BlobfsTest, MaxReservation) {
  // Create and destroy kBlobfsDefaultInodeCount number of blobs.
  // This verifies that creating blobs does not lead to stray node reservations.
  // Refer to fxbug.dev/54001 for the bug that lead to this test.
  size_t count = 0;
  for (uint64_t i = 0; i < kBlobfsDefaultInodeCount; i++) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs().mount_path(), 64, &info));

    // Write the blob
    {
      fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
      ASSERT_TRUE(fd) << "Failed to create blob";
      ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);
      ASSERT_EQ(StreamAll(write, fd.get(), info->data.get(), info->size_data), 0);
    }

    // Delete the blob
    ASSERT_EQ(unlink(info->path), 0) << "Unlinking blob";

    if (++count % 1000 == 0) {
      fprintf(stderr, "Allocated and deleted %lu blobs\n", count);
    }
  }
}

}  // namespace
}  // namespace blobfs

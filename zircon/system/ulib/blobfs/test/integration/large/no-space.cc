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

void RunNoSpaceTest() {
  std::unique_ptr<BlobInfo> last_info = nullptr;

  // Keep generating blobs until we run out of space.
  size_t count = 0;
  while (true) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << 17, &info));

    fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd, "Failed to create blob");
    int r = ftruncate(fd.get(), info->size_data);
    ASSERT_EQ(r, 0);
    r = StreamAll(write, fd.get(), info->data.get(), info->size_data);
    if (r < 0) {
      ASSERT_EQ(errno, ENOSPC, "Blobfs expected to run out of space");
      // We ran out of space, as expected. Can we allocate if we
      // unlink a previously allocated blob of the desired size?
      fd.reset();
      ASSERT_EQ(unlink(last_info->path), 0, "Unlinking old blob");
      fd.reset(open(info->path, O_CREAT | O_RDWR));
      ASSERT_TRUE(fd);
      int r = ftruncate(fd.get(), info->size_data);
      ASSERT_EQ(r, 0);
      ASSERT_EQ(StreamAll(write, fd.get(), info->data.get(), info->size_data), 0,
                "Did not free enough space");
      // Yay! allocated successfully.
      break;
    }
    last_info = std::move(info);

    if (++count % 50 == 0) {
      fprintf(stderr, "Allocated %lu blobs\n", count);
    }
  }
}

TEST_F(BlobfsTest, NoSpace) { RunNoSpaceTest(); }

TEST_F(BlobfsTestWithFvm, NoSpace) { RunNoSpaceTest(); }

}  // namespace

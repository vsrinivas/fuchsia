// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <fbl/unique_fd.h>
#include <blobfs/format.h>
#include <zxtest/zxtest.h>

#include "blobfs_fixtures.h"

namespace {

using fs::FilesystemTest;
using SuperblockTest = BlobfsTest;
using SuperblockTestWithFvm = BlobfsTestWithFvm;

void FsyncFilesystem() {
  // Open the root directory to fsync the filesystem.
  fbl::unique_fd fd_mount(open(kMountPath, O_RDONLY));
  ASSERT_TRUE(fd_mount);
  ASSERT_EQ(fsync(fd_mount.get()), 0);
}

void ReadSuperblock(const std::string& device_path, blobfs::Superblock* info) {
  fbl::unique_fd device(open(device_path.c_str(), O_RDWR));
  ASSERT_TRUE(device);
  ASSERT_EQ(blobfs::kBlobfsBlockSize, pread(device.get(), info, blobfs::kBlobfsBlockSize, 0));
}

void RunCheckDirtyBitOnMountTest(FilesystemTest* test) {
  blobfs::Superblock info;

  ASSERT_NO_FAILURES(FsyncFilesystem());

  // Check if clean bit is unset.
  ReadSuperblock(test->device_path(), &info);
  ASSERT_EQ(0, info.flags & blobfs::kBlobFlagClean);

  // Unmount and check if clean bit is set.
  ASSERT_NO_FAILURES(test->Unmount());

  ReadSuperblock(test->device_path(), &info);
  ASSERT_EQ(blobfs::kBlobFlagClean, info.flags & blobfs::kBlobFlagClean);
}

TEST_F(SuperblockTest, CheckDirtyBitOnMount) { RunCheckDirtyBitOnMountTest(this); }

TEST_F(SuperblockTestWithFvm, CheckDirtyBitOnMount) { RunCheckDirtyBitOnMountTest(this); }

}  // namespace

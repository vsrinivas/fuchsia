// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/sys/component/cpp/service_client.h>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/test/integration/blobfs_fixtures.h"

namespace blobfs {
namespace {

using SuperblockTest = ParameterizedBlobfsTest;

void FsyncFilesystem(fs_test::TestFilesystem& fs) {
  // Open the root directory to fsync the filesystem.
  fbl::unique_fd fd_mount(open(fs.mount_path().c_str(), O_RDONLY));
  ASSERT_TRUE(fd_mount) << strerror(errno);
  ASSERT_EQ(fsync(fd_mount.get()), 0) << strerror(errno);
}

void ReadSuperblock(const std::string& device_path, Superblock& superblock) {
  zx::result device = component::Connect<fuchsia_hardware_block::Block>(device_path);
  ASSERT_TRUE(device.is_ok()) << device.status_string();
  zx_status_t status =
      block_client::SingleReadBytes(device.value(), &superblock, sizeof(superblock), 0);
  ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);
}

TEST_P(SuperblockTest, CheckDirtyBitOnMount) {
  Superblock info;

  ASSERT_NO_FATAL_FAILURE(FsyncFilesystem(fs()));
  zx::result device_path = fs().DevicePath();
  ASSERT_TRUE(device_path.is_ok()) << device_path.status_string();

  // Check if clean bit is unset.
  ReadSuperblock(device_path.value(), info);
  ASSERT_EQ(info.flags & kBlobFlagClean, 0u);

  // Unmount and check if clean bit is set.
  {
    zx::result result = fs().Unmount();
    ASSERT_TRUE(result.is_ok()) << result.status_string();
  }

  ReadSuperblock(device_path.value(), info);
  ASSERT_EQ(kBlobFlagClean, info.flags & kBlobFlagClean);
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, SuperblockTest,
                         testing::Values(BlobfsDefaultTestParam(), BlobfsWithFvmTestParam()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace blobfs

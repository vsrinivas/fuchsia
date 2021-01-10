// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/vmo.h>
#include <sys/statfs.h>
#include <unistd.h>
#include <zircon/device/vfs.h>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/isolated_devmgr/v2_component/fvm.h"
#include "src/lib/isolated_devmgr/v2_component/ram_disk.h"
#include "src/storage/fshost/fshost_integration_test_fixture.h"

namespace devmgr {
namespace {

namespace fio = ::llcpp::fuchsia::io;

constexpr uint32_t kBlockCount = 1024 * 256;
constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kSliceSize = 32'768;
constexpr size_t kDeviceSize = kBlockCount * kBlockSize;

using FsRecoveryTest = FshostIntegrationTest;

TEST_F(FsRecoveryTest, EmptyPartitionRecoveryTest) {
  PauseWatcher();  // Pause whilst we create a ramdisk.

  // Create a ramdisk with an unformatted minfs partitition.
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(kDeviceSize, 0, &vmo), ZX_OK);

  // Create a child VMO so that we can keep hold of the original.
  zx::vmo child_vmo;
  ASSERT_EQ(vmo.create_child(ZX_VMO_CHILD_SLICE, 0, kDeviceSize, &child_vmo), ZX_OK);

  // Now create the ram-disk with a single FVM partition.
  {
    auto ramdisk_or = isolated_devmgr::RamDisk::CreateWithVmo(std::move(child_vmo), kBlockSize);
    ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);
    isolated_devmgr::FvmOptions options{
        .name = "minfs",
        .type = std::array<uint8_t, BLOCK_GUID_LEN>{GUID_DATA_VALUE},
    };
    auto fvm_partition_or =
        isolated_devmgr::CreateFvmPartition(ramdisk_or->path(), kSliceSize, options);
    ASSERT_EQ(fvm_partition_or.status_value(), ZX_OK);
  }

  ResumeWatcher();

  // Now reattach the ram-disk and fshost should format it.
  auto ramdisk_or = isolated_devmgr::RamDisk::CreateWithVmo(std::move(vmo), kBlockSize);
  ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);

  std::cout << "Waiting for data partition to be mounted" << std::endl;

  // There is nothing we can watch so all we can do is loop until it appears.
  for (;;) {
    fidl::SynchronousInterfacePtr<fuchsia::io::Node> minfs_root;
    zx_status_t status = exposed_dir()->Open(fuchsia::io::OPEN_RIGHT_READABLE, 0,
                                             std::string("minfs"), minfs_root.NewRequest());
    ASSERT_EQ(status, ZX_OK);

    fbl::unique_fd fd;
    ASSERT_EQ(
        fdio_fd_create(minfs_root.Unbind().TakeChannel().release(), fd.reset_and_get_address()),
        ZX_OK);

    struct statfs buf;
    ASSERT_EQ(fstatfs(fd.get(), &buf), 0);
    if (buf.f_type == VFS_TYPE_MINFS)
      break;

    sleep(1);
  }
}

}  // namespace
}  // namespace devmgr

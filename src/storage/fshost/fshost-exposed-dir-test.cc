// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.update.verify/cpp/wire.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/fdio/fd.h>
#include <sys/statfs.h>
#include <zircon/device/block.h>
#include <zircon/device/vfs.h>

#include <fbl/unique_fd.h>
#include <fs-management/admin.h>
#include <gtest/gtest.h>

#include "src/storage/fshost/fshost_integration_test.h"
#include "src/storage/testing/fvm.h"
#include "src/storage/testing/ram_disk.h"

namespace fshost {
namespace {

constexpr uint32_t kBlockCount = 1024 * 256;
constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kSliceSize = 32'768;
constexpr size_t kDeviceSize = kBlockCount * kBlockSize;

using FshostExposedDirTest = FshostIntegrationTest;

TEST_F(FshostExposedDirTest, ExposesDiagnosticsAndServicesForBlobfs) {
  PauseWatcher();  // Pause whilst we create a ramdisk.

  // Create a ramdisk with an empty blobfs partitition.
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(kDeviceSize, 0, &vmo), ZX_OK);

  // Create a child VMO so that we can keep hold of the original.
  zx::vmo child_vmo;
  ASSERT_EQ(vmo.create_child(ZX_VMO_CHILD_SLICE, 0, kDeviceSize, &child_vmo), ZX_OK);

  // Now create the ram-disk with a single FVM partition.
  {
    auto ramdisk_or = storage::RamDisk::CreateWithVmo(std::move(child_vmo), kBlockSize);
    ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);
    storage::FvmOptions options{
        .name = "blobfs",
        .type = std::array<uint8_t, BLOCK_GUID_LEN>{GUID_BLOB_VALUE},
    };
    auto fvm_partition_or = storage::CreateFvmPartition(ramdisk_or->path(), kSliceSize, options);
    ASSERT_EQ(fvm_partition_or.status_value(), ZX_OK);

    // Format the blobfs partition.
    ASSERT_EQ(mkfs(fvm_partition_or->c_str(), DISK_FORMAT_BLOBFS, launch_stdio_sync, MkfsOptions()),
              ZX_OK);
    ASSERT_EQ(fsck(fvm_partition_or->c_str(), DISK_FORMAT_BLOBFS, FsckOptions(), launch_stdio_sync),
              ZX_OK);
  }

  ResumeWatcher();

  // Now reattach the ram-disk and fshost should pick it up.
  auto ramdisk_or = storage::RamDisk::CreateWithVmo(std::move(vmo), kBlockSize);
  ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);

  auto [fd, fs_type] = WaitForMount("blob");
  ASSERT_TRUE(fd);
  EXPECT_EQ(fs_type, VFS_TYPE_BLOBFS);

  fidl::SynchronousInterfacePtr<fuchsia::io::Node> exposed_dir_client;
  ASSERT_EQ(
      exposed_dir()->Clone(fuchsia::io::CLONE_FLAG_SAME_RIGHTS, exposed_dir_client.NewRequest()),
      ZX_OK);

  fbl::unique_fd export_dir_fd;
  ASSERT_EQ(fdio_fd_create(exposed_dir_client.Unbind().TakeChannel().release(),
                           export_dir_fd.reset_and_get_address()),
            ZX_OK);
  ASSERT_TRUE(export_dir_fd);

  std::string svc_name = "diagnostics/blobfs";
  fbl::unique_fd blobfs_diag_dir_fd(
      openat(export_dir_fd.get(), svc_name.c_str(), fuchsia::io::OPEN_FLAG_DESCRIBE, 0644));
  EXPECT_TRUE(blobfs_diag_dir_fd) << "failed to open " << svc_name << ": " << strerror(errno);

  svc_name = fidl::DiscoverableProtocolName<fuchsia_update_verify::BlobfsVerifier>;
  fbl::unique_fd blobfs_health_check_dir_fd(
      openat(export_dir_fd.get(), svc_name.c_str(), fuchsia::io::OPEN_FLAG_DESCRIBE, 0644));
  EXPECT_TRUE(blobfs_health_check_dir_fd)
      << "failed to open " << svc_name << ": " << strerror(errno);
}

}  // namespace
}  // namespace fshost

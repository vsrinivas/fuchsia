// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/block-device-manager.h"

#include <fcntl.h>
#include <fidl/fuchsia.fs/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/sys/component/cpp/service_client.h>
#include <sys/statfs.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/storage/blobfs/fsck.h"
#include "src/storage/blobfs/mkfs.h"
#include "src/storage/fshost/config.h"
#include "src/storage/fshost/constants.h"
#include "src/storage/fshost/testing/fshost_integration_test.h"
#include "src/storage/fshost/testing/mock-block-device.h"
#include "src/storage/testing/fvm.h"
#include "src/storage/testing/ram_disk.h"

namespace fshost {
namespace {

namespace volume = fuchsia_hardware_block_volume;

using ::fshost::testing::MockBlobfsDevice;
using ::fshost::testing::MockBlockDevice;
using ::fshost::testing::MockMinfsDevice;
using ::fshost::testing::MockZxcryptDevice;

// For tests that want the full integration test suite.
using BlockDeviceManagerIntegration = testing::FshostIntegrationTest;

TEST(BlockDeviceManager, BlobfsLimit) {
  auto config = DefaultConfig();
  config.blobfs_max_bytes() = 7654321;
  BlockDeviceManager manager(&config);

  // When there's no FVM we expect no match and no max size call.
  MockBlobfsDevice blobfs_device;
  manager.AddDevice(blobfs_device);
  ASSERT_FALSE(blobfs_device.max_size());

  // Add FVM and re-try. This should call the limit set function.
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
  manager.AddDevice(blobfs_device);
  ASSERT_TRUE(blobfs_device.max_size());
  EXPECT_EQ(7654321u, *blobfs_device.max_size());

  // Make a blobfs that looks like it's in a ramdisk, the limit should not be set.
  MockBlockDevice::Options ramdisk_opts = MockBlobfsDevice::BlobfsOptions();
  ramdisk_opts.topological_path =
      "/dev/sys/platform/00:00:2d/ramctl" + ramdisk_opts.topological_path;
  MockBlockDevice ramdisk_blobfs(ramdisk_opts);
  manager.AddDevice(ramdisk_blobfs);
  ASSERT_FALSE(ramdisk_blobfs.max_size());
}

TEST(BlockDeviceManager, MinfsLimit) {
  auto config = DefaultConfig();
  config.data_max_bytes() = 7654321;
  BlockDeviceManager manager(&config);

  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);

  MockBlockDevice::Options device_options = MockZxcryptDevice::ZxcryptOptions();
  device_options.content_format = fs_management::kDiskFormatUnknown;
  MockZxcryptDevice zxcrypt_device(device_options);
  EXPECT_EQ(manager.AddDevice(zxcrypt_device), ZX_OK);

  MockMinfsDevice minfs_device;
  EXPECT_EQ(manager.AddDevice(minfs_device), ZX_OK);
  ASSERT_TRUE(minfs_device.max_size());
  EXPECT_EQ(7654321u, *minfs_device.max_size());
}

// The component for the fshost integration test sets the fshost config:
//   minfs_maximum_runtime_bytes = 32768
// which in turn sets the fshost variable kMinfsMaxBytes. This test is checking that this setting
// actually was sent to fshost and applies to FVM.
TEST_F(BlockDeviceManagerIntegration, MaxSize) {
  constexpr uint64_t kBlockCount{UINT64_C(9) * 1024 * 256};
  constexpr uint64_t kBlockSize{UINT64_C(512)};
  constexpr uint64_t kSliceSize{UINT64_C(32'768)};
  constexpr uint64_t kDeviceSize{kBlockCount * kBlockSize};

  PauseWatcher();  // Pause whilst we create a ramdisk.

  // Create a ramdisk with an unformatted minfs partitition.
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
        .name = kDataPartitionLabel,
        .type = std::array<uint8_t, BLOCK_GUID_LEN>{GUID_DATA_VALUE},
    };
    auto fvm_partition_or = storage::CreateFvmPartition(ramdisk_or->path(), kSliceSize, options);
    ASSERT_EQ(fvm_partition_or.status_value(), ZX_OK);
  }

  ResumeWatcher();

  // Now reattach the ram-disk and fshost should format it.
  auto ramdisk_or = storage::RamDisk::CreateWithVmo(std::move(vmo), kBlockSize);
  ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);
  auto [fd, fs_type] = WaitForMount("data");
  ASSERT_TRUE(fd);
  auto expected_fs_type = fuchsia_fs::VfsType::kMinfs;
  if (DataFilesystemFormat() == "f2fs") {
    expected_fs_type = fuchsia_fs::VfsType::kF2Fs;
  } else if (DataFilesystemFormat() == "fxfs") {
    expected_fs_type = fuchsia_fs::VfsType::kFxfs;
  }
  EXPECT_EQ(fs_type, fidl::ToUnderlying(expected_fs_type));

  // FVM will be at something like "/dev/sys/platform/00:00:2d/ramctl/ramdisk-1/block/fvm"
  std::string fvm_path = ramdisk_or.value().path() + "/fvm";
  fbl::unique_fd fvm_fd(open(fvm_path.c_str(), O_RDONLY));
  ASSERT_TRUE(fvm_fd);

  // The minfs partition will be the only one inside FVM.
  std::string partition_path = fvm_path + "/";
  partition_path.append(kDataPartitionLabel);
  partition_path.append("-p-1/block");
  fbl::unique_fd partition_fd(open(partition_path.c_str(), O_RDONLY));
  ASSERT_TRUE(partition_fd);

  // Query the minfs partition instance guid. This is needed to query the limit later on.
  fdio_cpp::UnownedFdioCaller partition_caller(partition_fd.get());
  auto guid_result =
      fidl::WireCall(partition_caller.borrow_as<volume::Volume>())->GetInstanceGuid();
  ASSERT_EQ(ZX_OK, guid_result.status());
  ASSERT_EQ(ZX_OK, guid_result.value().status);

  // Query the partition limit for the minfs partition.
  fdio_cpp::UnownedFdioCaller fvm_caller(fvm_fd.get());
  auto limit_result = fidl::WireCall(fvm_caller.borrow_as<volume::VolumeManager>())
                          ->GetPartitionLimit(*guid_result.value().guid);
  ASSERT_EQ(ZX_OK, limit_result.status());
  ASSERT_EQ(ZX_OK, limit_result.value().status);

  // The partition limit should match the value set in the integration test fshost configuration
  // (see the BUILD.gn file).
  constexpr uint64_t kMaxRuntimeBytes = UINT64_C(117440512);
  EXPECT_EQ(limit_result.value().slice_count, kMaxRuntimeBytes / kSliceSize);
}

TEST_F(BlockDeviceManagerIntegration, MinfsPartitionsRenamedToPreferredName) {
  constexpr uint64_t kBlockCount{UINT64_C(9) * 1024 * 256};
  constexpr uint64_t kBlockSize{UINT64_C(512)};
  constexpr uint64_t kSliceSize{UINT64_C(32'768)};
  constexpr uint64_t kDeviceSize{kBlockCount * kBlockSize};

  if (DataFilesystemFormat() == "fxfs") {
    // Fxfs partitions use a new matcher which does not have the logic to migrate legacy names.
    return;
  }

  PauseWatcher();  // Pause whilst we create a ramdisk.

  // Create a ramdisk with an unformatted minfs partitition.
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
        .name = "minfs",  // Use a legacy name
        .type = std::array<uint8_t, BLOCK_GUID_LEN>{GUID_DATA_VALUE},
    };
    auto fvm_partition_or = storage::CreateFvmPartition(ramdisk_or->path(), kSliceSize, options);
    ASSERT_EQ(fvm_partition_or.status_value(), ZX_OK);
  }

  ResumeWatcher();

  // Now reattach the ram-disk and fshost should format it.
  auto ramdisk_or = storage::RamDisk::CreateWithVmo(std::move(vmo), kBlockSize);
  ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);
  auto [fd, fs_type] = WaitForMount("data");
  ASSERT_TRUE(fd);
  auto expected_fs_type = fuchsia_fs::VfsType::kMinfs;
  if (DataFilesystemFormat() == "f2fs") {
    expected_fs_type = fuchsia_fs::VfsType::kF2Fs;
  } else if (DataFilesystemFormat() == "fxfs") {
    expected_fs_type = fuchsia_fs::VfsType::kFxfs;
  }
  EXPECT_EQ(fs_type, fidl::ToUnderlying(expected_fs_type));

  // FVM will be at something like "/dev/sys/platform/00:00:2d/ramctl/ramdisk-1/block/fvm"
  std::string fvm_path = ramdisk_or.value().path() + "/fvm";
  fbl::unique_fd fvm_fd(open(fvm_path.c_str(), O_RDONLY));
  ASSERT_TRUE(fvm_fd);

  // The minfs partition will be the only one inside FVM.
  std::string partition_path = fvm_path + "/minfs-p-1/block";
  fbl::unique_fd partition_fd(open(partition_path.c_str(), O_RDONLY));
  ASSERT_TRUE(partition_fd);

  // Query the partition name.
  fdio_cpp::UnownedFdioCaller partition_caller(partition_fd.get());
  auto result = fidl::WireCall(partition_caller.borrow_as<volume::Volume>())->GetName();
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_EQ(result.value().status, ZX_OK);

  // It should be the preferred name.
  ASSERT_EQ(result.value().name.get(), kDataPartitionLabel);
}

TEST_F(BlockDeviceManagerIntegration, StartBlobfsComponent) {
  constexpr uint64_t kBlockCount{UINT64_C(9) * 1024 * 256};
  constexpr uint64_t kBlockSize{UINT64_C(512)};
  constexpr uint64_t kSliceSize{UINT64_C(32'768)};
  constexpr uint64_t kDeviceSize{kBlockCount * kBlockSize};

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread("blobfs test caller thread"), ZX_OK);

  // Call query on the blob directory. We expect that the request will be queued, but not responed
  // to until blobfs is mounted by fshost later.
  auto node_client_end = component::ConnectAt<fuchsia_io::Node>(exposed_dir().client_end(), "blob");
  ASSERT_EQ(node_client_end.status_value(), ZX_OK);
  fidl::WireSharedClient<fuchsia_io::Node> node(std::move(*node_client_end), loop.dispatcher());
  sync_completion_t query_completion;
  node->QueryFilesystem().ThenExactlyOnce(
      [query_completion =
           &query_completion](fidl::WireUnownedResult<fuchsia_io::Node::QueryFilesystem>& res) {
        EXPECT_EQ(res.status(), ZX_OK);
        EXPECT_EQ(res.value().s, ZX_OK);
        EXPECT_EQ(res.value().info->fs_type, fidl::ToUnderlying(fuchsia_fs::VfsType::kBlobfs));
        sync_completion_signal(query_completion);
      });

  ASSERT_FALSE(sync_completion_signaled(&query_completion));
  PauseWatcher();  // Pause whilst we create a ramdisk.

  // Create a ramdisk with an unformatted blobfs partitition.
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

    // Format the new fvm partition with blobfs.
    zx::result device = component::Connect<fuchsia_hardware_block::Block>(fvm_partition_or.value());
    ASSERT_TRUE(device.is_ok()) << device.status_string();

    std::unique_ptr<block_client::RemoteBlockDevice> blobfs_device;
    ASSERT_EQ(block_client::RemoteBlockDevice::Create(std::move(device.value()), &blobfs_device),
              ZX_OK);
    ASSERT_EQ(blobfs::FormatFilesystem(blobfs_device.get(), blobfs::FilesystemOptions{}), ZX_OK);

    // Check the newly formatted blobfs for good measure.
    ASSERT_EQ(blobfs::Fsck(std::move(blobfs_device), blobfs::MountOptions{}), ZX_OK);
  }

  ResumeWatcher();

  // At this point, the query request should still be pending.
  ASSERT_FALSE(sync_completion_signaled(&query_completion));

  // Now reattach the ram-disk and fshost should find it and start blobfs.
  auto ramdisk_or = storage::RamDisk::CreateWithVmo(std::move(vmo), kBlockSize);
  ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);

  // Now the query request should be responded to.
  // Query should get a response now.
  ASSERT_EQ(sync_completion_wait(&query_completion, ZX_TIME_INFINITE), ZX_OK);
}

}  // namespace
}  // namespace fshost

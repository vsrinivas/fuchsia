// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/vmo.h>

#include <memory>

#include <fs-management/fvm.h>
#include <fs-management/mount.h>
#include <fvm/format.h>
#include <ramdevice-client/ramdisk.h>
#include <zxtest/zxtest.h>

namespace {

using devmgr_integration_test::IsolatedDevmgr;

constexpr uint32_t kBlockCount = 1024 * 256;
constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kSliceSize = (1 << 20);
constexpr size_t kDeviceSize = kBlockCount * kBlockSize;
const char* kDataName = "minfs";
const char* kRamdiskPath = "misc/ramctl";

// Test fixture that builds a ramdisk and destroys it when destructed.
class FsRecoveryTest : public zxtest::Test {
 public:
  // Create an IsolatedDevmgr that can load device drivers such as fvm, zxcrypt, etc.
  zx_status_t Initialize() {
    auto args = IsolatedDevmgr::DefaultArgs();
    args.disable_block_watcher = false;
    args.sys_device_driver = devmgr_integration_test::IsolatedDevmgr::kSysdevDriver;
    args.load_drivers.push_back(devmgr_integration_test::IsolatedDevmgr::kSysdevDriver);
    args.driver_search_paths.push_back("/boot/driver");
    args.path_prefix = "/pkg/";
    return IsolatedDevmgr::Create(std::move(args), &devmgr_);
  }

  // Create a ram disk that is back by a VMO, which is formatted to look like an FVM volume.
  void CreateFvmRamdisk(size_t device_size, size_t block_size) {
    // Calculate total size of data + metadata.
    size_t slice_count = (device_size + fvm::kBlockSize - 1) / fvm::kBlockSize;
    device_size = fvm::Header::FromSliceCount(fvm::kMaxUsablePartitions,
                                              slice_count, fvm::kBlockSize)
                      .fvm_partition_size;

    zx::vmo disk;
    ASSERT_EQ(zx::vmo::create(device_size, 0, &disk), ZX_OK);
    int fd = -1;
    ASSERT_EQ(fdio_fd_create(disk.get(), &fd), ZX_OK);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(fvm_init_with_size(fd, device_size, kSliceSize), ZX_OK);

    fbl::unique_fd ramdisk = WaitForDevice(kRamdiskPath);
    ASSERT_TRUE(ramdisk);

    ASSERT_OK(ramdisk_create_at_from_vmo(devmgr_.devfs_root().get(), disk.get(), &ramdisk_client_));
  }

  // Create a partition in the FVM volume that has the data guid. Returns the path to the FVM block
  // device.
  std::string CreateFvmPartition() {
    std::string fvm_path = std::string(ramdisk_get_path(ramdisk_client_)) + "/fvm";
    fbl::unique_fd fvm_fd = WaitForDevice(fvm_path);
    EXPECT_TRUE(fvm_fd);

    // Allocate a FVM partition with the data guid but don't actually format the partition.
    alloc_req_t req;
    memset(&req, 0, sizeof(alloc_req_t));
    req.slice_count = 1;
    static const uint8_t data_guid[GPT_GUID_LEN] = GUID_DATA_VALUE;
    memcpy(req.type, data_guid, BLOCK_GUID_LEN);

    fuchsia_hardware_block_partition_GUID type_guid;
    memcpy(type_guid.value, req.type, BLOCK_GUID_LEN);
    fuchsia_hardware_block_partition_GUID instance_guid;
    memcpy(instance_guid.value, req.guid, BLOCK_GUID_LEN);

    fdio_cpp::UnownedFdioCaller caller(fvm_fd.get());
    zx_status_t status;
    EXPECT_OK(fuchsia_hardware_block_volume_VolumeManagerAllocatePartition(
        caller.borrow_channel(), req.slice_count, &type_guid, &instance_guid, kDataName,
        strlen(kDataName), req.flags, &status));
    EXPECT_OK(status);

    std::string fvm_block_path = fvm_path + "/" + kDataName + "-p-1/block";
    fbl::unique_fd fvm_block_fd = WaitForDevice(fvm_block_path);
    EXPECT_TRUE(fvm_block_fd);

    return fvm_block_path;
  }

  // Wait for the device to be available and then check to make sure it is formatted of the passed
  // in type. Since formatting can take some time after the device becomes available, we must
  // recheck.
  bool WaitForDiskFormat(const std::string& path, disk_format_t format, zx::duration deadline) {
    fbl::unique_fd fd = WaitForDevice(path);
    if (!fd) {
      return false;
    }
    const zx::time absolute_deadline = zx::deadline_after(deadline);
    for (;;) {
      fd.reset(openat(devmgr_.devfs_root().get(), path.c_str(), O_RDONLY));
      if (detect_disk_format(fd.get()) == format) {
        return true;
      }
      zx::time next_deadline = zx::deadline_after(zx::duration(zx::sec(1)));
      if (next_deadline > absolute_deadline) {
        break;
      }
      zx::nanosleep(next_deadline);
    }
    return false;
  }

 private:
  fbl::unique_fd WaitForDevice(const std::string& path) {
    fbl::unique_fd result;
    EXPECT_OK(
        devmgr_integration_test::RecursiveWaitForFile(devmgr_.devfs_root(), path.c_str(), &result));
    return result;
  }

  ramdisk_client_t* ramdisk_client_ = nullptr;
  devmgr_integration_test::IsolatedDevmgr devmgr_;
};

TEST_F(FsRecoveryTest, EmptyPartitionRecoveryTest) {
  ASSERT_OK(Initialize());

  // Creates an FVM partition under an isolated devmgr. It creates, but does not properly format the
  // data partition.
  CreateFvmRamdisk(kDeviceSize, kBlockSize);
  std::string fvm_block_path = CreateFvmPartition();

  // We then expect the devmgr to self-recover, i.e., format the zxcrypt/data partitions as expected
  // from the FVM partition.

  // First, wait for the zxcrypt partition to be formatted.
  EXPECT_TRUE(WaitForDiskFormat(fvm_block_path, DISK_FORMAT_ZXCRYPT, zx::duration(zx::sec(100))));

  // Second, wait for the data partition to be formatted.
  std::string data_path = fvm_block_path + "/zxcrypt/unsealed/block";
  EXPECT_TRUE(WaitForDiskFormat(data_path, DISK_FORMAT_MINFS, zx::duration(zx::sec(100))));
}

}  // namespace

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/zx/vmo.h>

#include <memory>

#include <fs-management/fvm.h>
#include <fs-management/mount.h>
#include <fvm/format.h>
#include <ramdevice-client/ramdisk.h>
#include <unittest/unittest.h>

namespace {

using devmgr_integration_test::IsolatedDevmgr;

const uint32_t kBlockCount = 1024 * 256;
const uint32_t kBlockSize = 512;
const uint32_t kSliceSize = (1 << 20);
const size_t kDeviceSize = kBlockCount * kBlockSize;
const char* kDataName = "fs-recovery-data";
const char* kRamdiskPath = "misc/ramctl";

// Test fixture that builds a ramdisk and destroys it when destructed.
class FsRecoveryTest {
 public:
  // Create an IsolatedDevmgr that can load device drivers such as fvm,
  // zxcrypt, etc.
  bool Initialize() {
    BEGIN_HELPER;
    auto args = IsolatedDevmgr::DefaultArgs();
    args.disable_block_watcher = false;
    args.sys_device_driver = devmgr_integration_test::IsolatedDevmgr::kSysdevDriver;
    args.load_drivers.push_back(devmgr_integration_test::IsolatedDevmgr::kSysdevDriver);
    args.driver_search_paths.push_back("/boot/driver");
    ASSERT_EQ(IsolatedDevmgr::Create(std::move(args), &devmgr_), ZX_OK);
    END_HELPER;
  }

  // Create a ram disk that is back by a VMO, which is formatted to look like
  // an FVM volume.
  bool CreateFvmRamdisk(size_t device_size, size_t block_size) {
    BEGIN_HELPER;

    // Calculate total size of data + metadata.
    device_size = fbl::round_up(device_size, fvm::kBlockSize);
    size_t old_meta = fvm::MetadataSize(device_size, fvm::kBlockSize);
    size_t new_meta = fvm::MetadataSize(old_meta + device_size, fvm::kBlockSize);
    while (old_meta != new_meta) {
      old_meta = new_meta;
      new_meta = fvm::MetadataSize(old_meta + device_size, fvm::kBlockSize);
    }
    device_size = device_size + (new_meta * 2);

    zx::vmo disk;
    ASSERT_EQ(zx::vmo::create(device_size, 0, &disk), ZX_OK);
    int fd = -1;
    ASSERT_EQ(fdio_fd_create(disk.get(), &fd), ZX_OK);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(fvm_init_with_size(fd, device_size, kSliceSize), ZX_OK);

    fbl::unique_fd ramdisk;
    ASSERT_TRUE(WaitForDevice(kRamdiskPath, &ramdisk));

    ASSERT_EQ(ramdisk_create_at_from_vmo(devmgr_.devfs_root().get(), disk.get(), &ramdisk_client_),
              ZX_OK);
    END_HELPER;
  }

  // Create a partition in the FVM volume that has the data guid.
  bool CreateFvmPartition(char* fvm_block_path) {
    BEGIN_HELPER;

    char fvm_path[PATH_MAX];
    snprintf(fvm_path, PATH_MAX, "%s/fvm", ramdisk_get_path(ramdisk_client_));
    fbl::unique_fd fvm_fd;
    ASSERT_TRUE(WaitForDevice(fvm_path, &fvm_fd));

    // Allocate a FVM partition with the data guid but don't actually format the
    // partition.
    alloc_req_t req;
    memset(&req, 0, sizeof(alloc_req_t));
    req.slice_count = 1;
    static const uint8_t data_guid[GPT_GUID_LEN] = GUID_DATA_VALUE;
    memcpy(req.type, data_guid, BLOCK_GUID_LEN);
    snprintf(req.name, BLOCK_NAME_LEN, "%s", kDataName);

    fuchsia_hardware_block_partition_GUID type_guid;
    memcpy(type_guid.value, req.type, BLOCK_GUID_LEN);
    fuchsia_hardware_block_partition_GUID instance_guid;
    memcpy(instance_guid.value, req.guid, BLOCK_GUID_LEN);

    fdio_cpp::UnownedFdioCaller caller(fvm_fd.get());
    zx_status_t status;
    ASSERT_EQ(fuchsia_hardware_block_volume_VolumeManagerAllocatePartition(
                  caller.borrow_channel(), req.slice_count, &type_guid, &instance_guid, req.name,
                  BLOCK_NAME_LEN, req.flags, &status),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);

    snprintf(fvm_block_path, PATH_MAX, "%s/%s-p-1/block", fvm_path, kDataName);
    fbl::unique_fd fvm_block_fd;
    ASSERT_TRUE(WaitForDevice(fvm_block_path, &fvm_block_fd));

    END_HELPER;
  }

  // Wait for the device to be available and then check to make sure it is
  // formatted of the passed in type. Since formatting can take some time
  // after the device becomes available, we must recheck.
  bool WaitForDiskFormat(const char* path, disk_format_t format, zx::duration deadline) {
    fbl::unique_fd fd;
    if (!WaitForDevice(path, &fd)) {
      return false;
    }
    while (deadline.get() > 0) {
      fd.reset(openat(devmgr_.devfs_root().get(), path, O_RDONLY));
      if (detect_disk_format(fd.get()) == format)
        return true;
      sleep(1);
      deadline -= zx::duration(ZX_SEC(1));
    }
    return false;
  }

 private:
  bool WaitForDevice(const char* path, fbl::unique_fd* fd) {
    BEGIN_HELPER;
    printf("Wait for device %s\n", path);
    ASSERT_EQ(devmgr_integration_test::RecursiveWaitForFile(devmgr_.devfs_root(), path, fd), ZX_OK);

    ASSERT_TRUE(*fd);
    END_HELPER;
  }

  ramdisk_client_t* ramdisk_client_;
  devmgr_integration_test::IsolatedDevmgr devmgr_;
};

bool EmptyPartitionRecoveryTest() {
  BEGIN_TEST;

  char fvm_block_path[PATH_MAX];
  std::unique_ptr<FsRecoveryTest> recovery(new FsRecoveryTest());
  ASSERT_TRUE(recovery->Initialize());
  // Creates an FVM partition under an isolated devmgr. It creates, but does
  // not properly format the data partition.
  ASSERT_TRUE(recovery->CreateFvmRamdisk(kDeviceSize, kBlockSize));
  ASSERT_TRUE(recovery->CreateFvmPartition(fvm_block_path));

  // We then expect the devmgr to self-recover, i.e., format the zxcrypt/data
  // partitions as expected from the FVM partition.

  // First, wait for the zxcrypt partition to be formatted.
  EXPECT_TRUE(
      recovery->WaitForDiskFormat(fvm_block_path, DISK_FORMAT_ZXCRYPT, zx::duration(ZX_SEC(3))));

  // Second, wait for the data partition to be formatted.
  char data_path[PATH_MAX];
  snprintf(data_path, sizeof(data_path), "%s/zxcrypt/unsealed/block", fvm_block_path);
  EXPECT_TRUE(recovery->WaitForDiskFormat(data_path, DISK_FORMAT_MINFS, zx::duration(ZX_SEC(3))));

  END_TEST;
}

BEGIN_TEST_CASE(FsRecoveryTest)
RUN_TEST(EmptyPartitionRecoveryTest)
END_TEST_CASE(FsRecoveryTest)

}  // namespace

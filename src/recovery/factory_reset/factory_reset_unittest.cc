// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "factory_reset.h"

#include <fbl/algorithm.h>
#include <fcntl.h>
#include <fs-management/fvm.h>
#include <fs-management/mount.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/device/manager/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fzl/fdio.h>
#include <lib/zx/vmo.h>
#include <ramdevice-client/ramdisk.h>
#include <zircon/hw/gpt.h>
#include <zxcrypt/fdio-volume.h>

#include "gtest/gtest.h"

namespace {

using devmgr_integration_test::IsolatedDevmgr;
using ::testing::Test;

const uint32_t kBlockCount = 1024 * 256;
const uint32_t kBlockSize = 512;
const uint32_t kSliceSize = (1 << 20);
const size_t kDeviceSize = kBlockCount * kBlockSize;
const char* kDataName = "fdr-data";
const char* kRamCtlPath = "misc/ramctl";
const size_t kKeyBytes = 32; // Generate a 256-bit key for the zxcrypt volume

class MockAdmin : public fuchsia::device::manager::Administrator {
 public:
  bool suspend_called() { return suspend_called_; }

 private:
  void Suspend(uint32_t flags, SuspendCallback callback) override {
    ASSERT_FALSE(suspend_called_);
    suspend_called_ = true;
    ASSERT_EQ(fuchsia::device::manager::SUSPEND_FLAG_REBOOT, flags);
    callback(ZX_OK);
  }

  bool suspend_called_ = false;
};

class FactoryResetTest : public Test {
 public:
  // Create an IsolatedDevmgr that can load device drivers such as fvm,
  // zxcrypt, etc.
  void SetUp() {
    devmgr_.reset(new IsolatedDevmgr());
    auto args = IsolatedDevmgr::DefaultArgs();
    args.disable_block_watcher = true;
    args.sys_device_driver = devmgr_integration_test::IsolatedDevmgr::kSysdevDriver;
    args.load_drivers.push_back(devmgr_integration_test::IsolatedDevmgr::kSysdevDriver);
    args.driver_search_paths.push_back("/boot/driver");
    ASSERT_EQ(IsolatedDevmgr::Create(std::move(args), devmgr_.get()), ZX_OK);

    CreateRamdisk();
    CreateFvmPartition();
    CreateZxcrypt();
  }

  bool PartitionHasFormat(disk_format_t format) {
    fbl::unique_fd fd(openat(devmgr_->devfs_root().get(), fvm_block_path_, O_RDONLY));
    return detect_disk_format(fd.get()) == format;
  }

  fbl::unique_fd devfs_root() { return devmgr_->devfs_root().duplicate(); }

 private:
  void WaitForZxcrypt() {
    char data_block_path[PATH_MAX];
    // Second, wait for the data partition to be formatted.
    snprintf(data_block_path, sizeof(data_block_path), "%s/zxcrypt/unsealed/block",
             fvm_block_path_);
    fbl::unique_fd fd;
    WaitForDevice(data_block_path, &fd);
  }

  void CreateRamdisk() {
    zx::vmo disk;
    ASSERT_EQ(zx::vmo::create(kDeviceSize, 0, &disk), ZX_OK);
    int fd = -1;
    ASSERT_EQ(fdio_fd_create(disk.get(), &fd), ZX_OK);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(fvm_init_with_size(fd, kDeviceSize, kSliceSize), ZX_OK);

    fbl::unique_fd ramctl;
    WaitForDevice(kRamCtlPath, &ramctl);
    ASSERT_EQ(ramdisk_create_at_from_vmo(devfs_root().get(), disk.release(), &ramdisk_client_),
              ZX_OK);
  }

  zx_status_t AttachDriver(const fbl::unique_fd& fd, const fbl::StringPiece& driver) {
    fzl::UnownedFdioCaller connection(fd.get());
    zx_status_t call_status = ZX_OK;
    auto resp = ::llcpp::fuchsia::device::Controller::Call::Bind(
        zx::unowned_channel(connection.borrow_channel()),
        ::fidl::StringView(driver.data(), driver.length()));
    zx_status_t io_status = resp.status();
    if (io_status != ZX_OK) {
      return io_status;
    }
    if (resp->result.is_err()) {
      call_status = resp->result.err();
    }
    return call_status;
  }

  void BindFvm() {
    fbl::unique_fd ramdisk_fd(ramdisk_get_block_fd(ramdisk_client_));
    ASSERT_EQ(AttachDriver(ramdisk_fd, "/boot/driver/fvm.so"), ZX_OK);
  }

  void CreateFvmPartition() {
    BindFvm();
    fbl::unique_fd fvm_fd;
    char fvm_path[PATH_MAX];
    snprintf(fvm_path, PATH_MAX, "%s/fvm", ramdisk_get_path(ramdisk_client_));
    WaitForDevice(fvm_path, &fvm_fd);

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

    fzl::UnownedFdioCaller caller(fvm_fd.get());
    zx_status_t status;
    ASSERT_EQ(fuchsia_hardware_block_volume_VolumeManagerAllocatePartition(
                  caller.borrow_channel(), req.slice_count, &type_guid, &instance_guid, req.name,
                  BLOCK_NAME_LEN, req.flags, &status),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);

    snprintf(fvm_block_path_, sizeof(fvm_block_path_), "%s/%s-p-1/block", fvm_path, kDataName);
    fbl::unique_fd fd;
    WaitForDevice(fvm_block_path_, &fd);
  }

  void CreateZxcrypt() {
    std::unique_ptr<zxcrypt::FdioVolume> zxcrypt_volume;

    fbl::unique_fd fd;
    WaitForDevice(fvm_block_path_, &fd);
    // Use an explicit key for this test volume.  Other key sources may not be
    // available in the isolated test environment.
    crypto::Secret key;
    ASSERT_EQ(key.Generate(kKeyBytes), ZX_OK);
    ASSERT_EQ(zxcrypt::FdioVolume::Create(fd.duplicate(), devfs_root(), key, &zxcrypt_volume), ZX_OK);

    zx::channel zxc_manager_chan;
    ASSERT_EQ(zxcrypt_volume->OpenManager(zx::duration::infinite(),
                                          zxc_manager_chan.reset_and_get_address()),
              ZX_OK);
    zxcrypt::FdioVolumeManager volume_manager(std::move(zxc_manager_chan));
    ASSERT_EQ(volume_manager.Unseal(key.get(), key.len(), 0), ZX_OK);
    WaitForZxcrypt();
  }

  void WaitForDevice(const char* path, fbl::unique_fd* fd) {
    printf("wait for device %s\n", path);
    ASSERT_EQ(devmgr_integration_test::RecursiveWaitForFile(devfs_root(), path, fd), ZX_OK);

    ASSERT_TRUE(*fd);
  }

  ramdisk_client_t* ramdisk_client_;
  char fvm_block_path_[PATH_MAX];
  std::unique_ptr<IsolatedDevmgr> devmgr_;
};

// Tests that FactoryReset can find the correct block device and overwrite its
// superblocks, causing it to look like an unknown partition (which upon reboot
// will cause recovery to happen).
TEST_F(FactoryResetTest, CanShredVolume) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  MockAdmin mock_admin;
  fidl::BindingSet<fuchsia::device::manager::Administrator> binding;
  fidl::InterfacePtr<fuchsia::device::manager::Administrator> admin =
      binding.AddBinding(&mock_admin).Bind();

  factory_reset::FactoryReset reset((fbl::unique_fd(devfs_root())), std::move(admin));
  EXPECT_TRUE(PartitionHasFormat(DISK_FORMAT_ZXCRYPT));
  zx_status_t status = ZX_ERR_BAD_STATE;
  reset.Reset([&status](zx_status_t s) { status = s; });
  loop.RunUntilIdle();
  ASSERT_TRUE(mock_admin.suspend_called());
  EXPECT_EQ(status, ZX_OK);
  EXPECT_TRUE(PartitionHasFormat(DISK_FORMAT_UNKNOWN));
}

}  // namespace

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "factory_reset.h"

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>
#include <fuchsia/hardware/power/statecontrol/cpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/zx/vmo.h>
#include <zircon/hw/gpt.h>

#include <string_view>

#include <fbl/algorithm.h>
#include <gtest/gtest.h>
#include <ramdevice-client/ramdisk.h>

#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/fs_management/cpp/fvm.h"
#include "src/security/fcrypto/secret.h"
#include "src/security/zxcrypt/client.h"

namespace {

using driver_integration_test::IsolatedDevmgr;
using ::testing::Test;

const uint32_t kBlockCount = 1024 * 256;
const uint32_t kBlockSize = 512;
const uint32_t kSliceSize = (1 << 20);
const size_t kDeviceSize = kBlockCount * kBlockSize;
constexpr std::string_view kDataName = "fdr-data";
const char* kRamCtlPath = "sys/platform/00:00:2d/ramctl";
const size_t kKeyBytes = 32;  // Generate a 256-bit key for the zxcrypt volume

class MockAdmin : public fuchsia::hardware::power::statecontrol::testing::Admin_TestBase {
 public:
  bool suspend_called() const { return suspend_called_; }

 private:
  void NotImplemented_(const std::string& name) override {
    printf("'%s' was called unexpectedly", name.c_str());
    ASSERT_TRUE(false);
  }

  void Reboot(fuchsia::hardware::power::statecontrol::RebootReason req,
              RebootCallback callback) override {
    ASSERT_FALSE(suspend_called_);
    suspend_called_ = true;
    ASSERT_EQ(fuchsia::hardware::power::statecontrol::RebootReason::FACTORY_DATA_RESET, req);
    callback(fuchsia::hardware::power::statecontrol::Admin_Reboot_Result::WithResponse(
        fuchsia::hardware::power::statecontrol::Admin_Reboot_Response(ZX_OK)));
  }

  bool suspend_called_ = false;
};

class FactoryResetTest : public Test {
 public:
  // Create an IsolatedDevmgr that can load device drivers such as fvm,
  // zxcrypt, etc.
  void SetUp() override {
    IsolatedDevmgr::Args args;
    args.disable_block_watcher = true;

    ASSERT_EQ(IsolatedDevmgr::Create(&args, &devmgr_), ZX_OK);

    CreateRamdisk();
    CreateFvmPartition();
  }

  void TearDown() override { ASSERT_EQ(ramdisk_destroy(ramdisk_client_), ZX_OK); }

  bool PartitionHasFormat(fs_management::DiskFormat format) {
    fbl::unique_fd fd(openat(devmgr_.devfs_root().get(), fvm_block_path_.c_str(), O_RDONLY));
    return fs_management::DetectDiskFormat(fd.get()) == format;
  }

  void CreateZxcrypt() {
    fbl::unique_fd fd;
    WaitForDevice(fvm_block_path_, &fd);

    zxcrypt::VolumeManager zxcrypt_volume(std::move(fd), devfs_root());
    zx::channel zxc_manager_chan;
    ASSERT_EQ(zxcrypt_volume.OpenClient(zx::duration::infinite(), zxc_manager_chan), ZX_OK);

    // Use an explicit key for this test volume.  Other key sources may not be
    // available in the isolated test environment.
    crypto::Secret key;
    ASSERT_EQ(key.Generate(kKeyBytes), ZX_OK);
    zxcrypt::EncryptedVolumeClient volume_client(std::move(zxc_manager_chan));

    ASSERT_EQ(volume_client.Format(key.get(), key.len(), 0), ZX_OK);
    ASSERT_EQ(volume_client.Unseal(key.get(), key.len(), 0), ZX_OK);
    WaitForZxcrypt();
  }

  void CreateCorruptedZxcrypt() {
    fbl::unique_fd fd;
    WaitForDevice(fvm_block_path_, &fd);

    // Write just the zxcrypt magic at the start of the volume.
    // It will not be possible to unseal this device, but we want to ensure that
    // factory reset completes anyway and shreds what key material would reside
    // in that block.

    // Prepare a buffer of the native block size that starts with zxcrypt_magic.
    // Block reads and writes via fds must match the block size.
    ssize_t block_size;
    GetBlockSize(fd, &block_size);

    zx::vmo vmo;
    {
      zx_status_t status = zx::vmo::create(block_size, 0, &vmo);
      ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);
    }

    {
      zx_status_t status =
          vmo.write(fs_management::kZxcryptMagic, 0, sizeof(fs_management::kZxcryptMagic));
      ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);
    }
    ASSERT_NO_FATAL_FAILURE(WriteBlocks(fd, std::move(vmo), block_size, 0));
  }

  void CreateFakeBlobfs() {
    // Writes just the blobfs magic at the start of the volume, just as something
    // else we expect to detect so we can see if the block gets randomized later
    // or not.

    fbl::unique_fd fd;
    WaitForDevice(fvm_block_path_, &fd);

    // Prepare a buffer of the native block size that starts with blobfs_magic.
    // Block reads and writes via fds must match the block size.
    ssize_t block_size;
    GetBlockSize(fd, &block_size);

    zx::vmo vmo;
    {
      zx_status_t status = zx::vmo::create(block_size, 0, &vmo);
      ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);
    }

    {
      zx_status_t status =
          vmo.write(fs_management::kBlobfsMagic, 0, sizeof(fs_management::kBlobfsMagic));
      ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);
    }
    ASSERT_NO_FATAL_FAILURE(WriteBlocks(fd, std::move(vmo), block_size, 0));
  }

  void CreateFakeFxfs() {
    // Writes just the Fxfs magic byte sequence (FxfsSupr) so that we detect that the filesystem
    // is Fxfs and shred it accordingly.
    fbl::unique_fd fd;
    WaitForDevice(fvm_block_path_, &fd);

    ssize_t block_size;
    GetBlockSize(fd, &block_size);

    fdio_cpp::UnownedFdioCaller caller(fd.get());

    zx::vmo vmo;
    {
      zx_status_t status = zx::vmo::create(block_size, 0, &vmo);
      ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);
    }

    // Initialize one megabyte of NULL for the A/B super block extents.
    const size_t num_blocks = (1L << 20) / block_size;
    for (size_t i = 0; i < num_blocks; i++) {
      zx::vmo dup;
      {
        zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
        ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);
      }
      ASSERT_NO_FATAL_FAILURE(WriteBlocks(fd, std::move(dup), block_size, i * block_size));
    }

    // Add magic bytes at the correct offsets.
    {
      zx_status_t status =
          vmo.write(fs_management::kFxfsMagic, 0, sizeof(fs_management::kFxfsMagic));
      ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);
    }
    for (off_t ofs : {0L, 512L << 10}) {
      zx::vmo dup;
      {
        zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
        ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);
      }
      ASSERT_NO_FATAL_FAILURE(WriteBlocks(fd, std::move(dup), block_size, ofs));
    }
  }

  fbl::unique_fd devfs_root() { return devmgr_.devfs_root().duplicate(); }

 private:
  void WaitForZxcrypt() {
    char data_block_path[PATH_MAX];
    // Second, wait for the data partition to be formatted.
    snprintf(data_block_path, sizeof(data_block_path), "%s/zxcrypt/unsealed/block",
             fvm_block_path_.c_str());
    fbl::unique_fd fd;
    WaitForDevice(data_block_path, &fd);
  }

  static void GetBlockSize(const fbl::unique_fd& fd, ssize_t* out_size) {
    fdio_cpp::UnownedFdioCaller caller(fd.get());
    ASSERT_TRUE(caller);
    const fidl::WireResult result =
        fidl::WireCall(caller.borrow_as<fuchsia_hardware_block::Block>())->GetInfo();
    ASSERT_TRUE(result.ok()) << result.status_string();
    const fidl::WireResponse response = result.value();
    ASSERT_EQ(response.status, ZX_OK) << zx_status_get_string(response.status);
    *out_size = response.info->block_size;
  }

  static void WriteBlocks(const fbl::unique_fd& fd, zx::vmo vmo, uint64_t length, uint64_t offset) {
    fdio_cpp::UnownedFdioCaller caller(fd.get());
    ASSERT_TRUE(caller);
    const fidl::WireResult result =
        fidl::WireCall(caller.borrow_as<fuchsia_hardware_block::Block>())
            ->WriteBlocks(std::move(vmo), length, offset, 0);
    ASSERT_TRUE(result.ok()) << result.status_string();
    const fidl::WireResponse response = result.value();
    ASSERT_EQ(response.status, ZX_OK) << zx_status_get_string(response.status);
  }

  void CreateRamdisk() {
    fbl::unique_fd ramctl;
    WaitForDevice(kRamCtlPath, &ramctl);
    ASSERT_EQ(ramdisk_create_at(devfs_root().get(), kBlockSize, kBlockCount, &ramdisk_client_),
              ZX_OK);
    ASSERT_EQ(fs_management::FvmInitPreallocated(ramdisk_get_block_fd(ramdisk_client_), kDeviceSize,
                                                 kDeviceSize, kSliceSize),
              ZX_OK);
  }

  static zx_status_t AttachDriver(const fbl::unique_fd& fd, std::string_view driver) {
    fdio_cpp::UnownedFdioCaller connection(fd.get());
    zx_status_t call_status = ZX_OK;
    auto resp = fidl::WireCall(connection.borrow_as<fuchsia_device::Controller>())
                    ->Bind(::fidl::StringView::FromExternal(driver.data(), driver.length()));
    zx_status_t io_status = resp.status();
    if (io_status != ZX_OK) {
      return io_status;
    }
    if (resp->is_error()) {
      call_status = resp->error_value();
    }
    return call_status;
  }

  void BindFvm() {
    fbl::unique_fd ramdisk_fd(ramdisk_get_block_fd(ramdisk_client_));
    ASSERT_EQ(AttachDriver(ramdisk_fd, "fvm.so"), ZX_OK);
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

    fuchsia_hardware_block_partition::wire::Guid type_guid;
    memcpy(type_guid.value.data(), req.type, BLOCK_GUID_LEN);
    fuchsia_hardware_block_partition::wire::Guid instance_guid;
    memcpy(instance_guid.value.data(), req.guid, BLOCK_GUID_LEN);

    fdio_cpp::UnownedFdioCaller caller(fvm_fd.get());
    auto response =
        fidl::WireCall(fidl::UnownedClientEnd<fuchsia_hardware_block_volume::VolumeManager>(
                           caller.borrow_channel()))
            ->AllocatePartition(req.slice_count, type_guid, instance_guid,
                                fidl::StringView::FromExternal(kDataName), req.flags);
    ASSERT_EQ(response.status(), ZX_OK);
    ASSERT_EQ(response.value().status, ZX_OK);

    fvm_block_path_ = fvm_path;
    fvm_block_path_.append("/");
    fvm_block_path_.append(kDataName);
    fvm_block_path_.append("-p-1/block");
    fbl::unique_fd fd;
    WaitForDevice(fvm_block_path_, &fd);
  }

  void WaitForDevice(const std::string& path, fbl::unique_fd* fd) {
    printf("wait for device %s\n", path.c_str());
    ASSERT_EQ(device_watcher::RecursiveWaitForFile(devfs_root(), path.c_str(), fd), ZX_OK);

    ASSERT_TRUE(*fd);
  }

  ramdisk_client_t* ramdisk_client_;
  std::string fvm_block_path_;
  IsolatedDevmgr devmgr_;
};

// Tests that FactoryReset can find the correct block device and overwrite its
// superblocks, causing it to look like an unknown partition (which upon reboot
// will cause recovery to happen).
TEST_F(FactoryResetTest, CanShredVolume) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  // Set up a normal zxcrypt superblock
  CreateZxcrypt();

  MockAdmin mock_admin;
  fidl::BindingSet<fuchsia::hardware::power::statecontrol::Admin> binding;
  fidl::InterfacePtr<fuchsia::hardware::power::statecontrol::Admin> admin =
      binding.AddBinding(&mock_admin).Bind();

  factory_reset::FactoryReset reset(devfs_root(), std::move(admin));
  EXPECT_TRUE(PartitionHasFormat(fs_management::kDiskFormatZxcrypt));
  zx_status_t status = ZX_ERR_BAD_STATE;
  reset.Reset([&status](zx_status_t s) { status = s; });
  loop.RunUntilIdle();
  EXPECT_EQ(status, ZX_OK);
  EXPECT_TRUE(mock_admin.suspend_called());
  EXPECT_TRUE(PartitionHasFormat(fs_management::kDiskFormatUnknown));
}

TEST_F(FactoryResetTest, ShredsVolumeWithInvalidSuperblockIfMagicPresent) {
  // This test ensures that even if we can't unseal the zxcrypt device, we can
  // still wipe it.

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  // Set up a corrupted zxcrypt superblock -- just enough to recognize the
  // magic, but not enough to successfully unseal the device.
  CreateCorruptedZxcrypt();

  MockAdmin mock_admin;
  fidl::BindingSet<fuchsia::hardware::power::statecontrol::Admin> binding;
  fidl::InterfacePtr<fuchsia::hardware::power::statecontrol::Admin> admin =
      binding.AddBinding(&mock_admin).Bind();

  // Verify that we re-shred that superblock anyway when we run factory reset.
  factory_reset::FactoryReset reset(devfs_root(), std::move(admin));
  EXPECT_TRUE(PartitionHasFormat(fs_management::kDiskFormatZxcrypt));
  zx_status_t status = ZX_ERR_BAD_STATE;
  reset.Reset([&status](zx_status_t s) { status = s; });
  loop.RunUntilIdle();
  EXPECT_EQ(status, ZX_OK);
  EXPECT_TRUE(mock_admin.suspend_called());
  EXPECT_TRUE(PartitionHasFormat(fs_management::kDiskFormatUnknown));
}

TEST_F(FactoryResetTest, DoesntShredUnknownVolumeType) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  // Make this block device look like it contains blobfs.
  CreateFakeBlobfs();

  MockAdmin mock_admin;
  fidl::BindingSet<fuchsia::hardware::power::statecontrol::Admin> binding;
  fidl::InterfacePtr<fuchsia::hardware::power::statecontrol::Admin> admin =
      binding.AddBinding(&mock_admin).Bind();

  factory_reset::FactoryReset reset(devfs_root(), std::move(admin));
  EXPECT_TRUE(PartitionHasFormat(fs_management::kDiskFormatBlobfs));
  zx_status_t status = ZX_ERR_BAD_STATE;
  reset.Reset([&status](zx_status_t s) { status = s; });
  loop.RunUntilIdle();
  EXPECT_EQ(status, ZX_OK);
  EXPECT_TRUE(mock_admin.suspend_called());
  // Expect factory reset to still succeed, but to not touch the block device.
  // In a world where fshost knew more about expected topology, we'd want to
  // shred this block device anyway, but that won't happen until we have a
  // clearer block device topology story.
  EXPECT_TRUE(PartitionHasFormat(fs_management::kDiskFormatBlobfs));
}

TEST_F(FactoryResetTest, ShredsFxfs) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  // Make this block device look like it contains blobfs.
  CreateFakeFxfs();

  MockAdmin mock_admin;
  fidl::BindingSet<fuchsia::hardware::power::statecontrol::Admin> binding;
  fidl::InterfacePtr<fuchsia::hardware::power::statecontrol::Admin> admin =
      binding.AddBinding(&mock_admin).Bind();

  factory_reset::FactoryReset reset(devfs_root(), std::move(admin));
  EXPECT_TRUE(PartitionHasFormat(fs_management::kDiskFormatFxfs));
  zx_status_t status = ZX_ERR_BAD_STATE;
  reset.Reset([&status](zx_status_t s) { status = s; });
  loop.RunUntilIdle();
  EXPECT_EQ(status, ZX_OK);
  EXPECT_TRUE(mock_admin.suspend_called());
  EXPECT_FALSE(PartitionHasFormat(fs_management::kDiskFormatFxfs));
}

}  // namespace

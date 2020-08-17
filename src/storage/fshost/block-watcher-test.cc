// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/fshost/llcpp/fidl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/watcher.h>
#include <zircon/assert.h>
#include <zircon/device/block.h>
#include <zircon/hw/gpt.h>

#include <ramdevice-client/ramdisk.h>
#include <zxtest/zxtest.h>

#include "block-device-interface.h"
#include "block-watcher-test-data.h"
#include "encrypted-volume-interface.h"

namespace {
using devmgr_integration_test::RecursiveWaitForFile;
using driver_integration_test::IsolatedDevmgr;

class MockBlockDevice : public devmgr::BlockDeviceInterface {
 public:
  disk_format_t GetFormat() override = 0;
  void SetFormat(disk_format_t format) override {
    ZX_PANIC("Test should not invoke function %s\n", __FUNCTION__);
  }
  bool Netbooting() override { return false; }
  zx_status_t GetInfo(fuchsia_hardware_block_BlockInfo* out_info) override {
    fuchsia_hardware_block_BlockInfo info = {};
    info.flags = 0;
    info.block_size = 512;
    info.block_count = 1024;
    *out_info = info;
    return ZX_OK;
  }
  zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) override {
    ZX_PANIC("Test should not invoke function %s\n", __FUNCTION__);
  }
  zx_status_t AttachDriver(const std::string_view& driver) override {
    ZX_PANIC("Test should not invoke function %s\n", __FUNCTION__);
  }
  zx_status_t UnsealZxcrypt() override {
    ZX_PANIC("Test should not invoke function %s\n", __FUNCTION__);
  }
  zx_status_t IsTopologicalPathSuffix(const std::string_view& expected_path,
                                      bool* is_path) override {
    ZX_PANIC("Test should not invoke function %s\n", __FUNCTION__);
  }
  zx_status_t IsUnsealedZxcrypt(bool* is_unsealed_zxcrypt) override {
    ZX_PANIC("Test should not invoke function %s\n", __FUNCTION__);
  }
  zx_status_t FormatZxcrypt() override {
    ZX_PANIC("Test should not invoke function %s\n", __FUNCTION__);
  }
  bool ShouldCheckFilesystems() override {
    ZX_PANIC("Test should not invoke function %s\n", __FUNCTION__);
  }
  zx_status_t CheckFilesystem() override {
    ZX_PANIC("Test should not invoke function %s\n", __FUNCTION__);
  }
  zx_status_t FormatFilesystem() override {
    ZX_PANIC("Test should not invoke function %s\n", __FUNCTION__);
  }
  zx_status_t MountFilesystem() override {
    ZX_PANIC("Test should not invoke function %s\n", __FUNCTION__);
  }
};

// Tests adding a device which has no GUID and an unknown format.
TEST(AddDeviceTestCase, AddUnknownDevice) {
  class UnknownDevice : public MockBlockDevice {
   public:
    disk_format_t GetFormat() final { return DISK_FORMAT_UNKNOWN; }
    zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final {
      return ZX_ERR_NOT_SUPPORTED;
    }
  };
  UnknownDevice device;
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device.Add());
}

// Tests adding a device with an unknown GUID and unknown format.
TEST(AddDeviceTestCase, AddUnknownPartition) {
  class UnknownDevice : public MockBlockDevice {
   public:
    disk_format_t GetFormat() final { return DISK_FORMAT_UNKNOWN; }
    zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final {
      const uint8_t expected[GPT_GUID_LEN] = GUID_EMPTY_VALUE;
      memcpy(out_guid->value, expected, sizeof(expected));
      return ZX_OK;
    }
  };
  UnknownDevice device;
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device.Add());
}

// Tests adding a device which is smaller than the expected header size
TEST(AddDeviceTestCase, AddSmallDevice) {
  class SmallDevice : public MockBlockDevice {
   public:
    disk_format_t GetFormat() final { return DISK_FORMAT_UNKNOWN; }
    zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final {
      return ZX_ERR_NOT_SUPPORTED;
    }
    zx_status_t GetInfo(fuchsia_hardware_block_BlockInfo* out_info) override {
      fuchsia_hardware_block_BlockInfo info = {};
      info.flags = 0;
      info.block_size = 512;
      info.block_count = 1;
      *out_info = info;
      return ZX_OK;
    }
  };
  SmallDevice device;
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device.Add());
}

// Tests adding a device with a GPT format.
TEST(AddDeviceTestCase, AddGPTDevice) {
  class GptDevice : public MockBlockDevice {
   public:
    disk_format_t GetFormat() final { return DISK_FORMAT_GPT; }
    zx_status_t AttachDriver(const std::string_view& driver) final {
      EXPECT_STR_EQ(devmgr::kGPTDriverPath, driver.data());
      attached = true;
      return ZX_OK;
    }
    bool attached = false;
  };
  GptDevice device;
  EXPECT_OK(device.Add());
  EXPECT_TRUE(device.attached);
}

// Tests adding a device with an FVM format.
TEST(AddDeviceTestCase, AddFVMDevice) {
  class FvmDevice : public MockBlockDevice {
   public:
    disk_format_t GetFormat() final { return DISK_FORMAT_FVM; }
    zx_status_t AttachDriver(const std::string_view& driver) final {
      EXPECT_STR_EQ(devmgr::kFVMDriverPath, driver.data());
      attached = true;
      return ZX_OK;
    }
    bool attached = false;
  };
  FvmDevice device;
  EXPECT_OK(device.Add());
  EXPECT_TRUE(device.attached);
}

// Tests adding a device with an MBR format.
TEST(AddDeviceTestCase, AddMBRDevice) {
  class MbrDevice : public MockBlockDevice {
   public:
    disk_format_t GetFormat() final { return DISK_FORMAT_MBR; }
    zx_status_t AttachDriver(const std::string_view& driver) final {
      EXPECT_STR_EQ(devmgr::kMBRDriverPath, driver.data());
      attached = true;
      return ZX_OK;
    }
    bool attached = false;
  };
  MbrDevice device;
  EXPECT_OK(device.Add());
  EXPECT_TRUE(device.attached);
}

// Tests adding a device with a factory GUID but an unknown disk format.
TEST(AddDeviceTestCase, AddUnformattedBlockVerityDevice) {
  class BlockVerityDevice : public MockBlockDevice {
   public:
    // in FCT mode we need to be able to bind the block-verity driver to devices that don't yet
    // have detectable magic, so Add relies on the gpt guid.
    disk_format_t GetFormat() final { return DISK_FORMAT_UNKNOWN; }
    zx_status_t AttachDriver(const std::string_view& driver) final {
      EXPECT_STR_EQ(devmgr::kBlockVerityDriverPath, driver.data());
      attached = true;
      return ZX_OK;
    }
    zx_status_t IsTopologicalPathSuffix(const std::string_view& expected_path,
                                        bool* is_path) final {
      EXPECT_STR_EQ("/mutable/block", expected_path.data());
      *is_path = false;
      return ZX_OK;
    }
    zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final {
      *out_guid = GPT_FACTORY_TYPE_GUID;
      return ZX_OK;
    }
    bool attached = false;
  };
  BlockVerityDevice device;
  EXPECT_OK(device.Add());
  EXPECT_TRUE(device.attached);
}

// Tests adding a device with a factory GUID but an unknown disk format with the topological path
// suffix /mutable/block
TEST(AddDeviceTestCase, AddUnformattedMutableBlockVerityDevice) {
  class BlockVerityDevice : public MockBlockDevice {
   public:
    disk_format_t GetFormat() final { return DISK_FORMAT_UNKNOWN; }
    zx_status_t AttachDriver(const std::string_view& driver) final {
      ADD_FATAL_FAILURE("Should not attach a driver");
      return ZX_OK;
    }
    zx_status_t IsTopologicalPathSuffix(const std::string_view& expected_path,
                                        bool* is_path) final {
      EXPECT_STR_EQ("/mutable/block", expected_path.data());
      *is_path = true;
      return ZX_OK;
    }
    zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final {
      *out_guid = GPT_FACTORY_TYPE_GUID;
      return ZX_OK;
    }
  };
  BlockVerityDevice device;
  EXPECT_OK(device.Add());
}

// Tests adding a device with the block-verity disk format.
TEST(AddDeviceTestCase, AddFormattedBlockVerityDevice) {
  class BlockVerityDevice : public MockBlockDevice {
   public:
    disk_format_t GetFormat() final { return DISK_FORMAT_BLOCK_VERITY; }
    zx_status_t AttachDriver(const std::string_view& driver) final {
      EXPECT_STR_EQ(devmgr::kBlockVerityDriverPath, driver.data());
      attached = true;
      return ZX_OK;
    }
    bool attached = false;
  };
  BlockVerityDevice device;
  EXPECT_OK(device.Add());
  EXPECT_TRUE(device.attached);
}

// Tests adding blobfs which does not not have a valid type GUID.
TEST(AddDeviceTestCase, AddNoGUIDBlobDevice) {
  class BlobDevice : public MockBlockDevice {
   public:
    disk_format_t GetFormat() final { return DISK_FORMAT_BLOBFS; }
    zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final {
      *out_guid = GUID_TEST_VALUE;
      return ZX_OK;
    }
    zx_status_t CheckFilesystem() final {
      ADD_FATAL_FAILURE("Should not check filesystem");
      return ZX_OK;
    }
    zx_status_t MountFilesystem() final {
      ADD_FATAL_FAILURE("Should not mount filesystem");
      return ZX_OK;
    }
  };
  BlobDevice device;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, device.Add());
}

// Tests adding blobfs with a valid type GUID, but invalid metadata.
TEST(AddDeviceTestCase, AddInvalidBlobDevice) {
  class BlobDevice : public MockBlockDevice {
   public:
    disk_format_t GetFormat() final { return DISK_FORMAT_BLOBFS; }
    zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final {
      const uint8_t expected[GPT_GUID_LEN] = GUID_BLOB_VALUE;
      memcpy(out_guid->value, expected, sizeof(expected));
      return ZX_OK;
    }
    zx_status_t CheckFilesystem() final {
      checked = true;
      return ZX_ERR_BAD_STATE;
    }
    zx_status_t FormatFilesystem() final {
      formatted = true;
      return ZX_OK;
    }
    zx_status_t MountFilesystem() final {
      mounted = true;
      return ZX_OK;
    }

    bool checked = false;
    bool formatted = false;
    bool mounted = false;
  };
  BlobDevice device;
  EXPECT_EQ(ZX_ERR_BAD_STATE, device.Add());
  EXPECT_TRUE(device.checked);
  EXPECT_FALSE(device.formatted);
  EXPECT_FALSE(device.mounted);
}

// Tests adding blobfs with a valid type GUID and valid metadata.
TEST(AddDeviceTestCase, AddValidBlobDevice) {
  class BlobDevice : public MockBlockDevice {
   public:
    disk_format_t GetFormat() final { return DISK_FORMAT_BLOBFS; }
    zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final {
      const uint8_t expected[GPT_GUID_LEN] = GUID_BLOB_VALUE;
      memcpy(out_guid->value, expected, sizeof(expected));
      return ZX_OK;
    }
    zx_status_t CheckFilesystem() final {
      checked = true;
      return ZX_OK;
    }
    zx_status_t FormatFilesystem() final {
      formatted = true;
      return ZX_OK;
    }
    zx_status_t MountFilesystem() final {
      mounted = true;
      return ZX_OK;
    }

    bool checked = false;
    bool formatted = false;
    bool mounted = false;
  };
  BlobDevice device;
  EXPECT_OK(device.Add());
  EXPECT_TRUE(device.checked);
  EXPECT_FALSE(device.formatted);
  EXPECT_TRUE(device.mounted);
}

// Tests adding minfs which does not not have a valid type GUID.
TEST(AddDeviceTestCase, AddNoGUIDMinfsDevice) {
  class MinfsDevice : public MockBlockDevice {
   public:
    disk_format_t GetFormat() final { return DISK_FORMAT_MINFS; }
    zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final {
      *out_guid = GUID_TEST_VALUE;
      return ZX_OK;
    }
    zx_status_t CheckFilesystem() final {
      ADD_FATAL_FAILURE("Should not check filesystem");
      return ZX_OK;
    }
    zx_status_t MountFilesystem() final {
      ADD_FATAL_FAILURE("Should not mount filesystem");
      return ZX_OK;
    }
  };
  MinfsDevice device;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, device.Add());
}

// Tests adding minfs with a valid type GUID and invalid metadata. Observe that
// the filesystem reformats itself.
TEST(AddDeviceTestCase, AddInvalidMinfsDevice) {
  class MinfsDevice : public MockBlockDevice {
   public:
    disk_format_t GetFormat() final { return DISK_FORMAT_MINFS; }
    zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final {
      const uint8_t expected[GPT_GUID_LEN] = GUID_DATA_VALUE;
      memcpy(out_guid->value, expected, sizeof(expected));
      return ZX_OK;
    }
    zx_status_t CheckFilesystem() final {
      checked = true;
      return ZX_ERR_BAD_STATE;
    }
    zx_status_t FormatFilesystem() final {
      formatted = true;
      return ZX_OK;
    }
    zx_status_t MountFilesystem() final {
      mounted = true;
      return ZX_OK;
    }

    bool checked = false;
    bool formatted = false;
    bool mounted = false;
  };
  MinfsDevice device;
  EXPECT_OK(device.Add());
  EXPECT_TRUE(device.checked);
  EXPECT_TRUE(device.formatted);
  EXPECT_TRUE(device.mounted);
}

// Tests adding minfs with a valid type GUID and invalid format. Observe that
// the filesystem reformats itself.
TEST(AddDeviceTestCase, AddUnknownFormatMinfsDevice) {
  class MinfsDevice : public MockBlockDevice {
   public:
    disk_format_t GetFormat() final { return format; }
    zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final {
      const uint8_t expected[GPT_GUID_LEN] = GUID_DATA_VALUE;
      memcpy(out_guid->value, expected, sizeof(expected));
      return ZX_OK;
    }
    zx_status_t FormatFilesystem() final {
      formatted = true;
      return ZX_OK;
    }
    zx_status_t CheckFilesystem() final { return ZX_OK; }
    zx_status_t MountFilesystem() final {
      EXPECT_TRUE(formatted);
      mounted = true;
      return ZX_OK;
    }
    zx_status_t IsUnsealedZxcrypt(bool* is_unsealed_zxcrypt) final {
      *is_unsealed_zxcrypt = true;
      return ZX_OK;
    }
    void SetFormat(disk_format_t f) final { format = f; }

    disk_format_t format = DISK_FORMAT_UNKNOWN;
    bool formatted = false;
    bool mounted = false;
  };
  MinfsDevice device;
  EXPECT_FALSE(device.formatted);
  EXPECT_FALSE(device.mounted);
  EXPECT_OK(device.Add());
  EXPECT_TRUE(device.formatted);
  EXPECT_TRUE(device.mounted);
}

// Tests adding zxcrypt with a valid type GUID and invalid format. Observe that
// the partition reformats itself.
TEST(AddDeviceTestCase, AddUnknownFormatZxcryptDevice) {
  class ZxcryptDevice : public MockBlockDevice {
   public:
    disk_format_t GetFormat() final { return format; }
    zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final {
      const uint8_t expected[GPT_GUID_LEN] = GUID_DATA_VALUE;
      memcpy(out_guid->value, expected, sizeof(expected));
      return ZX_OK;
    }
    zx_status_t FormatZxcrypt() final {
      formatted_zxcrypt = true;
      return ZX_OK;
    }
    zx_status_t FormatFilesystem() final {
      formatted_filesystem = true;
      return ZX_OK;
    }
    zx_status_t CheckFilesystem() final { return ZX_OK; }
    zx_status_t UnsealZxcrypt() final { return ZX_OK; }
    zx_status_t IsUnsealedZxcrypt(bool* is_unsealed_zxcrypt) final {
      *is_unsealed_zxcrypt = false;
      return ZX_OK;
    }
    void SetFormat(disk_format_t f) final { format = f; }
    zx_status_t AttachDriver(const std::string_view& driver) final {
      EXPECT_STR_EQ(devmgr::kZxcryptDriverPath, driver.data());
      return ZX_OK;
    }

    disk_format_t format = DISK_FORMAT_UNKNOWN;
    bool formatted_zxcrypt = false;
    bool formatted_filesystem = false;
  };
  ZxcryptDevice device;
  EXPECT_OK(device.Add());
  EXPECT_TRUE(device.formatted_zxcrypt);
  EXPECT_FALSE(device.formatted_filesystem);
}

// Durable partition tests
// Tests adding minfs on durable partition with a valid type GUID and valid metadata.
TEST(AddDeviceTestCase, AddValidDurableDevice) {
  class DurableDevice : public MockBlockDevice {
   public:
    disk_format_t GetFormat() final { return DISK_FORMAT_MINFS; }
    zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final {
      const uint8_t expected[GPT_GUID_LEN] = GPT_DURABLE_TYPE_GUID;
      memcpy(out_guid->value, expected, sizeof(expected));
      return ZX_OK;
    }
    zx_status_t CheckFilesystem() final {
      checked = true;
      return ZX_OK;
    }
    zx_status_t FormatFilesystem() final {
      formatted = true;
      return ZX_OK;
    }
    zx_status_t MountFilesystem() final {
      mounted = true;
      return ZX_OK;
    }

    bool checked = false;
    bool formatted = false;
    bool mounted = false;
  };
  DurableDevice device;
  EXPECT_OK(device.Add());
  EXPECT_TRUE(device.checked);
  EXPECT_FALSE(device.formatted);
  EXPECT_TRUE(device.mounted);
}

// Tests adding minfs backed durable partition with a valid type GUID and invalid metadata.
// Observe that the filesystem reformats itself.
TEST(AddDeviceTestCase, AddInvalidDurableDevice) {
  class DurableDevice : public MockBlockDevice {
   public:
    disk_format_t GetFormat() final { return DISK_FORMAT_MINFS; }
    zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final {
      const uint8_t expected[GPT_GUID_LEN] = GPT_DURABLE_TYPE_GUID;
      memcpy(out_guid->value, expected, sizeof(expected));
      return ZX_OK;
    }
    zx_status_t CheckFilesystem() final {
      checked = true;
      return ZX_ERR_BAD_STATE;
    }
    zx_status_t FormatFilesystem() final {
      formatted = true;
      return ZX_OK;
    }
    zx_status_t MountFilesystem() final {
      mounted = true;
      return ZX_OK;
    }

    bool checked = false;
    bool formatted = false;
    bool mounted = false;
  };
  DurableDevice device;
  EXPECT_OK(device.Add());
  EXPECT_TRUE(device.checked);
  EXPECT_TRUE(device.formatted);
  EXPECT_TRUE(device.mounted);
}

// Tests adding a boot partition device with unknown format can be added with
// the correct driver.
TEST(AddDeviceTestCase, AddUnknownFormatBootPartitionDevice) {
  class BootPartDevice : public MockBlockDevice {
   public:
    disk_format_t GetFormat() final { return DISK_FORMAT_UNKNOWN; }
    zx_status_t GetInfo(fuchsia_hardware_block_BlockInfo* out_info) override {
      fuchsia_hardware_block_BlockInfo info = {};
      info.flags = BLOCK_FLAG_BOOTPART;
      info.block_size = 512;
      info.block_count = 1024;
      *out_info = info;
      return ZX_OK;
    }
    zx_status_t AttachDriver(const std::string_view& driver) final {
      EXPECT_STR_EQ(devmgr::kBootpartDriverPath, driver.data());
      return ZX_OK;
    }
    zx_status_t IsUnsealedZxcrypt(bool* is_unsealed_zxcrypt) final {
      *is_unsealed_zxcrypt = false;
      checked_unsealed_zxcrypt = true;
      return ZX_OK;
    }
    bool checked_unsealed_zxcrypt = false;
  };
  BootPartDevice device;
  EXPECT_OK(device.Add());
  EXPECT_FALSE(device.checked_unsealed_zxcrypt);
}

TEST(AddDeviceTestCase, AddPermanentlyMiskeyedZxcryptVolume) {
  class ZxcryptVolume : public devmgr::EncryptedVolumeInterface {
   public:
    zx_status_t Unseal() final {
      // Simulate a device where we've lost the key -- can't unlock until we
      // format the device with a new key, but can afterwards.
      if (formatted) {
        postformat_unseal_attempt_count++;
        return ZX_OK;
      } else {
        preformat_unseal_attempt_count++;
        return ZX_ERR_ACCESS_DENIED;
      }
    }
    zx_status_t Format() final {
      formatted = true;
      return ZX_OK;
    }

    int preformat_unseal_attempt_count = 0;
    int postformat_unseal_attempt_count = 0;
    bool formatted = false;
  };
  ZxcryptVolume volume;
  EXPECT_OK(volume.EnsureUnsealedAndFormatIfNeeded());
  EXPECT_TRUE(volume.preformat_unseal_attempt_count > 1);
  EXPECT_TRUE(volume.formatted);
  EXPECT_EQ(volume.postformat_unseal_attempt_count, 1);
}

TEST(AddDeviceTestCase, AddTransientlyMiskeyedZxcryptVolume) {
  class ZxcryptVolume : public devmgr::EncryptedVolumeInterface {
   public:
    zx_status_t Unseal() final {
      // Simulate a transient error -- fail the first time we try to unseal the
      // volume, but succeed on a retry or any subsequent attempt.
      unseal_attempt_count++;
      if (unseal_attempt_count > 1) {
        return ZX_OK;
      } else {
        return ZX_ERR_ACCESS_DENIED;
      }
    }

    zx_status_t Format() final {
      // We expect this to never be called.
      formatted = true;
      return ZX_OK;
    }

    int unseal_attempt_count = 0;
    bool formatted = false;
  };
  ZxcryptVolume volume;
  EXPECT_OK(volume.EnsureUnsealedAndFormatIfNeeded());
  EXPECT_FALSE(volume.formatted);
  EXPECT_EQ(volume.unseal_attempt_count, 2);
}

TEST(AddDeviceTestCase, AddFailingZxcryptVolumeShouldNotFormat) {
  class ZxcryptVolume : public devmgr::EncryptedVolumeInterface {
   public:
    zx_status_t Unseal() final {
      // Errors that are not ZX_ERR_ACCESS_DENIED should not trigger
      // formatting.
      return ZX_ERR_INTERNAL;
    }
    zx_status_t Format() final {
      // Expect this to not be called.
      formatted = true;
      return ZX_OK;
    }

    bool formatted = false;
  };
  ZxcryptVolume volume;
  EXPECT_EQ(ZX_ERR_INTERNAL, volume.EnsureUnsealedAndFormatIfNeeded());
  EXPECT_FALSE(volume.formatted);
}

class BlockWatcherTest : public zxtest::Test {
 protected:
  BlockWatcherTest() {
    // Launch the isolated devmgr.
    IsolatedDevmgr::Args args;
    args.driver_search_paths.push_back("/boot/driver");
    args.path_prefix = "/pkg/";
    args.disable_block_watcher = false;
    ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr_));

    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "misc/ramctl", &fd));

    zx::channel remote;
    ASSERT_OK(zx::channel::create(0, &watcher_chan_, &remote));
    ASSERT_OK(fdio_service_connect_at(devmgr_.fshost_outgoing_dir().get(),
                                      "/svc/fuchsia.fshost.BlockWatcher", remote.release()));
  }

  void CreateGptRamdisk(ramdisk_client** client) {
    zx::vmo ramdisk_vmo;
    ASSERT_OK(zx::vmo::create(kTestDiskSectors * kBlockSize, 0, &ramdisk_vmo));
    // Write the GPT into the VMO.
    ASSERT_OK(ramdisk_vmo.write(kTestGptProtectiveMbr, 0, sizeof(kTestGptProtectiveMbr)));
    ASSERT_OK(ramdisk_vmo.write(kTestGptBlock1, kBlockSize, sizeof(kTestGptBlock1)));
    ASSERT_OK(ramdisk_vmo.write(kTestGptBlock2, 2 * kBlockSize, sizeof(kTestGptBlock2)));

    ASSERT_OK(
        ramdisk_create_at_from_vmo(devmgr_.devfs_root().get(), ramdisk_vmo.release(), client));
  }

  void PauseWatcher() {
    auto result = llcpp::fuchsia::fshost::BlockWatcher::Call::Pause(watcher_chan_.borrow());
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  void ResumeWatcher() {
    auto result = llcpp::fuchsia::fshost::BlockWatcher::Call::Resume(watcher_chan_.borrow());
    ASSERT_OK(result.status());
    ASSERT_OK(result->status);
  }

  void WaitForBlockDevice(int number) {
    auto path = fbl::StringPrintf("class/block/%03d", number);
    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURES(RecursiveWaitForFile(devmgr_.devfs_root(), path.data(), &fd));
  }

  // Check that the number of block devices bound by the block watcher
  // matches what we expect. Can only be called while the block watcher is running.
  //
  // This works by adding a new block device with a valid GPT.
  // We then wait for that block device to appear at class/block/|next_device_number|.
  // The block watcher should then bind the GPT driver to that block device, causing
  // another entry in class/block to appear representing the only partition on the GPT.
  //
  // We make sure that this entry's toplogical path corresponds to it being the first partition
  // of the block device we added.
  void CheckEventsDropped(int* next_device_number, ramdisk_client** client) {
    ASSERT_NO_FATAL_FAILURES(CreateGptRamdisk(client));

    // Wait for the basic block driver to be bound
    auto path = fbl::StringPrintf("class/block/%03d", *next_device_number);
    *next_device_number += 1;
    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), path.data(), &fd));

    // And now, wait for the GPT driver to be bound, and the first
    // partition to appear.
    path = fbl::StringPrintf("class/block/%03d", *next_device_number);
    *next_device_number += 1;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), path.data(), &fd));

    // Figure out the expected topological path of the last block device.
    std::string expected_path(ramdisk_get_path(*client));
    expected_path = "/dev/" + expected_path + "/part-000/block";

    zx_handle_t handle;
    ASSERT_OK(fdio_get_service_handle(fd.release(), &handle));
    zx::channel channel(handle);
    // Get the actual topological path of the block device.
    auto result = llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(channel.borrow());
    ASSERT_OK(result.status());
    ASSERT_FALSE(result->result.is_err());

    auto actual_path =
        std::string(result->result.response().path.begin(), result->result.response().path.size());
    // Make sure expected path matches the actual path.
    ASSERT_EQ(expected_path, actual_path);
  }

  IsolatedDevmgr devmgr_;
  zx::channel watcher_chan_;
};

TEST_F(BlockWatcherTest, TestBlockWatcherDisable) {
  ASSERT_NO_FATAL_FAILURES(PauseWatcher());
  int next_device_number = 0;
  // Add a block device.
  ramdisk_client* client;
  ASSERT_NO_FATAL_FAILURES(CreateGptRamdisk(&client));
  ASSERT_NO_FATAL_FAILURES(WaitForBlockDevice(next_device_number));
  next_device_number++;

  ASSERT_NO_FATAL_FAILURES(ResumeWatcher());

  ramdisk_client* client2;
  ASSERT_NO_FATAL_FAILURES(CheckEventsDropped(&next_device_number, &client2));

  ASSERT_OK(ramdisk_destroy(client));
  ASSERT_OK(ramdisk_destroy(client2));
}

TEST_F(BlockWatcherTest, TestBlockWatcherAdd) {
  int next_device_number = 0;
  // Add a block device.
  ramdisk_client* client;
  ASSERT_NO_FATAL_FAILURES(CreateGptRamdisk(&client));
  ASSERT_NO_FATAL_FAILURES(WaitForBlockDevice(next_device_number));
  next_device_number++;

  ASSERT_NO_FATAL_FAILURES(PauseWatcher());
  fbl::unique_fd fd;
  // Look for the first partition of the device we just added.
  ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "class/block/001", &fd));
  ASSERT_NO_FATAL_FAILURES(ResumeWatcher());

  ASSERT_OK(ramdisk_destroy(client));
}

TEST_F(BlockWatcherTest, TestBlockWatcherUnmatchedResume) {
  auto result = llcpp::fuchsia::fshost::BlockWatcher::Call::Resume(watcher_chan_.borrow());
  ASSERT_OK(result.status());
  ASSERT_STATUS(result->status, ZX_ERR_BAD_STATE);
}

TEST_F(BlockWatcherTest, TestMultiplePause) {
  ASSERT_NO_FATAL_FAILURES(PauseWatcher());
  ASSERT_NO_FATAL_FAILURES(PauseWatcher());
  int next_device_number = 0;

  // Add a block device.
  ramdisk_client* client;
  ASSERT_NO_FATAL_FAILURES(CreateGptRamdisk(&client));
  ASSERT_NO_FATAL_FAILURES(WaitForBlockDevice(next_device_number));
  next_device_number++;

  // Resume once.
  ASSERT_NO_FATAL_FAILURES(ResumeWatcher());

  ramdisk_client* client2;
  ASSERT_NO_FATAL_FAILURES(CreateGptRamdisk(&client2));
  ASSERT_NO_FATAL_FAILURES(WaitForBlockDevice(next_device_number));
  next_device_number++;

  fbl::unique_fd fd;
  RecursiveWaitForFile(devmgr_.devfs_root(), ramdisk_get_path(client2), &fd);
  // Resume again. The block watcher should be running again.
  ASSERT_NO_FATAL_FAILURES(ResumeWatcher());

  // Make sure neither device was seen by the watcher.
  ramdisk_client* client3;
  ASSERT_NO_FATAL_FAILURES(CheckEventsDropped(&next_device_number, &client3));

  // Pause again.
  ASSERT_NO_FATAL_FAILURES(PauseWatcher());
  ramdisk_client* client4;
  ASSERT_NO_FATAL_FAILURES(CreateGptRamdisk(&client4));
  ASSERT_NO_FATAL_FAILURES(WaitForBlockDevice(next_device_number));
  next_device_number++;
  // Resume again.
  ASSERT_NO_FATAL_FAILURES(ResumeWatcher());

  // Make sure the last device wasn't added.
  ramdisk_client* client5;
  ASSERT_NO_FATAL_FAILURES(CheckEventsDropped(&next_device_number, &client5));

  ASSERT_OK(ramdisk_destroy(client));
  ASSERT_OK(ramdisk_destroy(client2));
  ASSERT_OK(ramdisk_destroy(client3));
  ASSERT_OK(ramdisk_destroy(client4));
  ASSERT_OK(ramdisk_destroy(client5));
}

}  // namespace

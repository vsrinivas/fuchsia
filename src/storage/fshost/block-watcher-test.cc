// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/fshost/llcpp/fidl.h>
#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/watcher.h>
#include <zircon/assert.h>
#include <zircon/device/block.h>
#include <zircon/hw/gpt.h>

#include <string_view>

#include <fbl/string_printf.h>
#include <gtest/gtest.h>
#include <ramdevice-client/ramdisk.h>

#include "block-device-interface.h"
#include "block-device-manager.h"
#include "block-watcher-test-data.h"
#include "encrypted-volume-interface.h"
#include "mock-block-device.h"
#include "src/lib/files/glob.h"
#include "src/lib/isolated_devmgr/v2_component/ram_disk.h"

namespace devmgr {
namespace {

BlockDeviceManager::Options TestOptions() { return BlockDeviceManager::DefaultOptions(); }
BlockDeviceManager::Options FactoryOptions() {
  auto options = TestOptions();
  options.options.insert({BlockDeviceManager::Options::kFactory, {}});
  return options;
}
BlockDeviceManager::Options DurableOptions() {
  auto options = TestOptions();
  options.options.insert({BlockDeviceManager::Options::kDurable, {}});
  return options;
}

// Tests adding a device which has an unknown format.
TEST(AddDeviceTestCase, AddUnknownDevice) {
  MockBlockDevice device;
  BlockDeviceManager manager(TestOptions());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, manager.AddDevice(device));
}

// Tests adding a device which is smaller than the expected header size
TEST(AddDeviceTestCase, AddSmallDevice) {
  class SmallDevice : public MockBlockDevice {
   public:
    zx_status_t GetInfo(fuchsia_hardware_block_BlockInfo* out_info) const override {
      fuchsia_hardware_block_BlockInfo info = {};
      info.flags = 0;
      info.block_size = 512;
      info.block_count = 1;
      *out_info = info;
      return ZX_OK;
    }
  };
  SmallDevice device;
  BlockDeviceManager manager(TestOptions());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, manager.AddDevice(device));
}

// Tests adding a device with a GPT format.
TEST(AddDeviceTestCase, AddGPTDevice) {
  MockBlockDevice device(MockBlockDevice::GptOptions());
  BlockDeviceManager manager(TestOptions());
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.attached());
}

// Tests adding a device with an FVM format.
TEST(AddDeviceTestCase, AddFVMDevice) {
  BlockDeviceManager manager(TestOptions());
  MockBlockDevice device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.attached());
}

// Tests adding a device with an MBR format.
TEST(AddDeviceTestCase, AddMBRDevice) {
  auto options = TestOptions();
  options.options[BlockDeviceManager::Options::kMbr] = std::string();
  BlockDeviceManager manager(options);
  MockBlockDevice device(MockBlockDevice::Options{
      .content_format = DISK_FORMAT_MBR,
      .driver_path = kMBRDriverPath,
  });
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.attached());
}

TEST(AddDeviceTestCase, AddBlockVerityDevice) {
  BlockDeviceManager manager(FactoryOptions());
  MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
  EXPECT_EQ(manager.AddDevice(gpt_device), ZX_OK);
  MockBlockVerityDevice device(/*allow_authoring=*/true);
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.attached());
}

TEST(AddDeviceTestCase, NonFactoryBlockVerityDeviceNotAttached) {
  BlockDeviceManager manager(FactoryOptions());
  MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
  EXPECT_EQ(manager.AddDevice(gpt_device), ZX_OK);
  MockBlockDevice::Options options = MockBlockVerityDevice::VerityOptions();
  options.partition_name = "not-factory";
  MockBlockVerityDevice device(/*allow_authoring=*/true, options);
  EXPECT_EQ(manager.AddDevice(device), ZX_ERR_NOT_SUPPORTED);
  EXPECT_FALSE(device.attached());
}

// Tests adding a device with the block-verity disk format
TEST(AddDeviceTestCase, AddFormattedBlockVerityDevice) {
  BlockDeviceManager manager(FactoryOptions());
  MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
  EXPECT_EQ(manager.AddDevice(gpt_device), ZX_OK);
  MockSealedBlockVerityDevice device;
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.attached());
  EXPECT_TRUE(device.opened());
}

// Tests adding a device with block-verity format but no seal provided by
// bootloader
TEST(AddDeviceTestCase, AddFormattedBlockVerityDeviceWithoutSeal) {
  class BlockVerityDeviceWithNoSeal : public MockBlockVerityDevice {
   public:
    BlockVerityDeviceWithNoSeal() : MockBlockVerityDevice(/*allow_authoring=*/false) {}

    zx::status<std::string> VeritySeal() final {
      seal_read_ = true;
      return zx::error_status(ZX_ERR_NOT_FOUND);
    }
    zx_status_t OpenBlockVerityForVerifiedRead(std::string seal_hex) final {
      ADD_FAILURE() << "Should not call OpenBlockVerityForVerifiedRead";
      return ZX_OK;
    }
    bool seal_read() const { return seal_read_; }

   private:
    bool seal_read_ = false;
  };
  BlockDeviceManager manager(FactoryOptions());
  MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
  EXPECT_EQ(manager.AddDevice(gpt_device), ZX_OK);
  BlockVerityDeviceWithNoSeal device;
  EXPECT_EQ(ZX_ERR_NOT_FOUND, manager.AddDevice(device));
  EXPECT_TRUE(device.attached());
  EXPECT_TRUE(device.seal_read());
}

// Tests adding a device with block-verity format while in factory authoring mode
TEST(AddDeviceTestCase, AddFormattedBlockVerityDeviceInAuthoringMode) {
  class BlockVerityDeviceInAuthoringMode : public MockBlockVerityDevice {
   public:
    BlockVerityDeviceInAuthoringMode() : MockBlockVerityDevice(/*allow_authoring=*/true) {}

    zx::status<std::string> VeritySeal() final {
      ADD_FAILURE() << "Should not call VeritySeal";
      return zx::error_status(ZX_ERR_NOT_FOUND);
    }
    zx_status_t OpenBlockVerityForVerifiedRead(std::string seal_hex) final {
      ADD_FAILURE() << "Should not call OpenBlockVerityForVerifiedRead";
      return ZX_OK;
    }
  };
  BlockDeviceManager manager(FactoryOptions());
  MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
  EXPECT_EQ(manager.AddDevice(gpt_device), ZX_OK);
  BlockVerityDeviceInAuthoringMode device;
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.attached());
}

// Tests adding blobfs which does not not have a valid type GUID.
TEST(AddDeviceTestCase, AddNoGUIDBlobDevice) {
  class BlobDeviceWithInvalidTypeGuid : public MockBlobfsDevice {
   public:
    const fuchsia_hardware_block_partition_GUID& GetTypeGuid() const final {
      static fuchsia_hardware_block_partition_GUID guid = GUID_TEST_VALUE;
      return guid;
    }
  };

  BlockDeviceManager manager(TestOptions());
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
  BlobDeviceWithInvalidTypeGuid device;
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, manager.AddDevice(device));
  EXPECT_FALSE(device.mounted());
}

// Tests adding blobfs with a valid type GUID, but invalid metadata.
TEST(AddDeviceTestCase, AddInvalidBlobDevice) {
  class BlobDeviceWithInvalidMetadata : public MockBlobfsDevice {
   public:
    zx_status_t CheckFilesystem() final {
      MockBlobfsDevice::CheckFilesystem();
      return ZX_ERR_BAD_STATE;
    }
  };
  BlockDeviceManager manager(TestOptions());
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
  BlobDeviceWithInvalidMetadata device;
  EXPECT_EQ(ZX_ERR_BAD_STATE, manager.AddDevice(device));
  EXPECT_TRUE(device.checked());
  EXPECT_FALSE(device.formatted());
  EXPECT_FALSE(device.mounted());
}

// Tests adding blobfs with a valid type GUID and valid metadata.
TEST(AddDeviceTestCase, AddValidBlobDevice) {
  BlockDeviceManager manager(TestOptions());
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
  MockBlobfsDevice device;
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.checked());
  EXPECT_FALSE(device.formatted());
  EXPECT_TRUE(device.mounted());
}

TEST(AddDeviceTestCase, NetbootingDoesNotMountBlobfs) {
  auto options = TestOptions();
  options.options[BlockDeviceManager::Options::kNetboot] = std::string();
  BlockDeviceManager manager(options);
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
  MockBlobfsDevice device;
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, manager.AddDevice(device));
  EXPECT_FALSE(device.mounted());
}

// Tests adding minfs which does not not have a valid type GUID.
TEST(AddDeviceTestCase, AddNoGUIDMinfsDevice) {
  class MinfsDeviceWithInvalidGuid : public MockBlockDevice {
   public:
    using MockBlockDevice::MockBlockDevice;

    const fuchsia_hardware_block_partition_GUID& GetTypeGuid() const final {
      static fuchsia_hardware_block_partition_GUID guid = GUID_TEST_VALUE;
      return guid;
    }
  };
  BlockDeviceManager manager(TestOptions());
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
  MinfsDeviceWithInvalidGuid device(MockZxcryptDevice::ZxcryptOptions());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, manager.AddDevice(device));
  EXPECT_FALSE(device.attached());
}

// Tests adding minfs with a valid type GUID and invalid metadata. Observe that
// the filesystem reformats itself.
TEST(AddDeviceTestCase, AddInvalidMinfsDeviceWithFormatOnCorruptionEnabled) {
  class MinfsDeviceWithInvalidMetadata : public MockMinfsDevice {
   public:
    zx_status_t CheckFilesystem() final {
      MockMinfsDevice::CheckFilesystem();
      return ZX_ERR_BAD_STATE;
    }
  };
  auto options = TestOptions();
  EXPECT_TRUE(options.is_set(BlockDeviceManager::Options::kFormatMinfsOnCorruption));
  BlockDeviceManager manager(options);
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
  MockZxcryptDevice zxcrypt_device;
  EXPECT_EQ(manager.AddDevice(zxcrypt_device), ZX_OK);
  MinfsDeviceWithInvalidMetadata device;
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.checked());
  EXPECT_TRUE(device.formatted());
  EXPECT_TRUE(device.mounted());
}

// Tests adding minfs with a valid type GUID and invalid metadata. Observe that
// the filesystem does not reformats itself and adding device fails.
TEST(AddDeviceTestCase, AddInvalidMinfsDeviceWithFormatOnCorruptionDisabled) {
  class MinfsDeviceWithInvalidMetadata : public MockMinfsDevice {
   public:
    zx_status_t CheckFilesystem() final {
      MockMinfsDevice::CheckFilesystem();
      return ZX_ERR_BAD_STATE;
    }
  };
  auto options = TestOptions();
  EXPECT_EQ(options.options.erase(BlockDeviceManager::Options::kFormatMinfsOnCorruption), 1ul);
  EXPECT_FALSE(options.is_set(BlockDeviceManager::Options::kFormatMinfsOnCorruption));
  BlockDeviceManager manager(options);
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
  MockZxcryptDevice zxcrypt_device;
  EXPECT_EQ(manager.AddDevice(zxcrypt_device), ZX_OK);
  MinfsDeviceWithInvalidMetadata device;
  EXPECT_EQ(ZX_ERR_BAD_STATE, manager.AddDevice(device));
}

// Tests adding zxcrypt with a valid type GUID and invalid format. Observe that
// the partition reformats itself.
TEST(AddDeviceTestCase, FormatZxcryptDevice) {
  BlockDeviceManager manager(TestOptions());
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
  MockBlockDevice::Options options = MockZxcryptDevice::ZxcryptOptions();
  options.content_format = DISK_FORMAT_UNKNOWN;
  MockZxcryptDevice zxcrypt_device(options);
  EXPECT_EQ(manager.AddDevice(zxcrypt_device), ZX_OK);
  MockMinfsDevice device;
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(zxcrypt_device.formatted_zxcrypt());
  EXPECT_TRUE(device.formatted());
  EXPECT_TRUE(device.mounted());
}

// Tests adding zxcrypt with a valid type GUID and minfs format i.e. it's a minfs partition without
// zxcrypt. Observe that the partition reformats itself.
TEST(AddDeviceTestCase, FormatMinfsDeviceWithZxcrypt) {
  BlockDeviceManager manager(TestOptions());
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
  MockBlockDevice::Options options = MockZxcryptDevice::ZxcryptOptions();
  options.content_format = DISK_FORMAT_MINFS;
  MockZxcryptDevice zxcrypt_device(options);
  EXPECT_EQ(manager.AddDevice(zxcrypt_device), ZX_OK);
  MockMinfsDevice device;
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(zxcrypt_device.formatted_zxcrypt());
  EXPECT_TRUE(device.formatted());
  EXPECT_TRUE(device.mounted());
}

TEST(AddDeviceTestCase, MinfsWithNoZxcryptOptionMountsWithoutZxcrypt) {
  auto options = TestOptions();
  options.options["no-zxcrypt"] = std::string();
  BlockDeviceManager manager(options);
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
  auto minfs_options = MockMinfsDevice::MinfsOptions();
  minfs_options.topological_path = MockBlockDevice::BaseTopologicalPath() + "/fvm/minfs-p-2/block";
  minfs_options.partition_name = "minfs";
  MockMinfsDevice device(minfs_options);
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.mounted());
}

TEST(AddDeviceTestCase, MinfsRamdiskMounts) {
  // The fvm-ramdisk option will check that the topological path actually has an expected ramdisk
  // prefix.
  auto manager_options = TestOptions();
  manager_options.options[BlockDeviceManager::Options::kFvmRamdisk] = std::string();
  BlockDeviceManager manager(manager_options);
  auto options = MockBlockDevice::FvmOptions();
  constexpr std::string_view kBasePath = "/dev/misc/ramctl/mock_device/block";
  options.topological_path = kBasePath;
  MockBlockDevice fvm_device(options);
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
  options = MockMinfsDevice::MinfsOptions();
  options.topological_path = std::string(kBasePath) + "/fvm/minfs-p-2/block";
  options.partition_name = "minfs";
  MockMinfsDevice device(options);
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.mounted());
}

TEST(AddDeviceTestCase, MinfsRamdiskDeviceNotRamdiskDoesNotMount) {
  auto options = TestOptions();
  options.options[BlockDeviceManager::Options::kFvmRamdisk] = std::string();
  options.options[BlockDeviceManager::Options::kAttachZxcryptToNonRamdisk] = std::string();
  BlockDeviceManager manager(options);
  auto fvm_options = MockBlockDevice::FvmOptions();
  fvm_options.topological_path = "/dev/misc/ramctl/mock_device/block";
  MockBlockDevice ramdisk_fvm_device(fvm_options);
  EXPECT_EQ(manager.AddDevice(ramdisk_fvm_device), ZX_OK);
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
  MockZxcryptDevice zxcrypt_device;
  EXPECT_EQ(manager.AddDevice(zxcrypt_device), ZX_OK);
  MockMinfsDevice device;
  EXPECT_EQ(manager.AddDevice(device), ZX_ERR_NOT_SUPPORTED);
  EXPECT_FALSE(device.mounted());
}

TEST(AddDeviceTestCase, MinfsRamdiskWithoutZxcryptAttachOption) {
  auto options = TestOptions();
  options.options[BlockDeviceManager::Options::kFvmRamdisk] = std::string();
  BlockDeviceManager manager(options);
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
  MockZxcryptDevice zxcrypt_device;
  EXPECT_EQ(manager.AddDevice(zxcrypt_device), ZX_ERR_NOT_SUPPORTED);
}

TEST(AddDeviceTestCase, MinfsWithAlternateNameMounts) {
  for (std::string name : {GUID_DATA_NAME, "data"}) {
    BlockDeviceManager manager(TestOptions());
    MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
    EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
    MockZxcryptDevice zxcrypt_device;
    EXPECT_EQ(manager.AddDevice(zxcrypt_device), ZX_OK);
    auto minfs_options = MockMinfsDevice::MinfsOptions();
    minfs_options.partition_name = name;
    MockMinfsDevice device(minfs_options);
    EXPECT_EQ(manager.AddDevice(device), ZX_OK);
    EXPECT_TRUE(device.mounted());
  }
}

// Durable partition tests
// Tests adding minfs on durable partition with a valid type GUID and valid metadata.
TEST(AddDeviceTestCase, AddValidDurableDevice) {
  class DurableZxcryptDevice : public MockZxcryptDevice {
   public:
    DurableZxcryptDevice()
        : MockZxcryptDevice(Options{
              .content_format = DISK_FORMAT_ZXCRYPT,
              .driver_path = kZxcryptDriverPath,
              .topological_path =
                  MockBlockDevice::BaseTopologicalPath() + "/" GPT_DURABLE_NAME "-004/block",
              .partition_name = GPT_DURABLE_NAME,
          }) {}

    const fuchsia_hardware_block_partition_GUID& GetTypeGuid() const final {
      static fuchsia_hardware_block_partition_GUID guid = GPT_DURABLE_TYPE_GUID;
      return guid;
    }
  };
  class DurableDevice : public MockBlockDevice {
   public:
    using MockBlockDevice::MockBlockDevice;

    const fuchsia_hardware_block_partition_GUID& GetTypeGuid() const final {
      static fuchsia_hardware_block_partition_GUID guid = GPT_DURABLE_TYPE_GUID;
      return guid;
    }
    zx_status_t CheckFilesystem() final {
      checked_ = true;
      return ZX_OK;
    }
    zx_status_t FormatFilesystem() final {
      formatted_ = true;
      return ZX_OK;
    }
    zx_status_t MountFilesystem() final {
      mounted_ = true;
      return ZX_OK;
    }

    bool checked() const { return checked_; }
    bool formatted() const { return formatted_; }
    bool mounted() const { return mounted_; }

   private:
    bool checked_ = false;
    bool formatted_ = false;
    bool mounted_ = false;
  };
  BlockDeviceManager manager(DurableOptions());
  MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
  EXPECT_EQ(manager.AddDevice(gpt_device), ZX_OK);
  DurableZxcryptDevice zxcrypt_device;
  EXPECT_EQ(manager.AddDevice(zxcrypt_device), ZX_OK);
  DurableDevice device(MockBlockDevice::DurableOptions());
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.checked());
  EXPECT_FALSE(device.formatted());
  EXPECT_TRUE(device.mounted());
}

// Tests adding a boot partition device with unknown format can be added with
// the correct driver.
TEST(AddDeviceTestCase, AddUnknownFormatBootPartitionDevice) {
  class BootPartDevice : public MockBlockDevice {
   public:
    BootPartDevice()
        : MockBlockDevice(Options{
              .driver_path = kBootpartDriverPath,
          }) {}
    zx_status_t GetInfo(fuchsia_hardware_block_BlockInfo* out_info) const override {
      fuchsia_hardware_block_BlockInfo info = {};
      info.flags = BLOCK_FLAG_BOOTPART;
      info.block_size = 512;
      info.block_count = 1024;
      *out_info = info;
      return ZX_OK;
    }
  };
  BootPartDevice device;
  BlockDeviceManager manager(TestOptions());
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.attached());
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
  EXPECT_EQ(volume.EnsureUnsealedAndFormatIfNeeded(), ZX_OK);
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
  EXPECT_EQ(volume.EnsureUnsealedAndFormatIfNeeded(), ZX_OK);
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

// Tests adding factoryfs with valid factoryfs magic, as a verified child of a
// block-verity device, but with invalid metadata.
TEST(AddDeviceTestCase, AddInvalidFactoryfsDevice) {
  class FactoryfsWithInvalidMetadata : public MockFactoryfsDevice {
    zx_status_t CheckFilesystem() override {
      MockFactoryfsDevice::CheckFilesystem();
      return ZX_ERR_BAD_STATE;
    }
  };
  BlockDeviceManager manager(FactoryOptions());
  MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
  EXPECT_EQ(manager.AddDevice(gpt_device), ZX_OK);
  MockSealedBlockVerityDevice verity_device;
  EXPECT_EQ(manager.AddDevice(verity_device), ZX_OK);
  FactoryfsWithInvalidMetadata device;
  EXPECT_EQ(ZX_ERR_BAD_STATE, manager.AddDevice(device));
  EXPECT_TRUE(device.checked());
  EXPECT_FALSE(device.formatted());
  EXPECT_FALSE(device.mounted());
}

// Tests adding factoryfs with valid fasctoryfs magic, as a verified child of a
// block-verity device, and valid metadata.
TEST(AddDeviceTestCase, AddValidFactoryfsDevice) {
  BlockDeviceManager manager(FactoryOptions());
  MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
  EXPECT_EQ(manager.AddDevice(gpt_device), ZX_OK);
  MockSealedBlockVerityDevice verity_device;
  EXPECT_EQ(manager.AddDevice(verity_device), ZX_OK);
  MockFactoryfsDevice device;
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.checked());
  EXPECT_FALSE(device.formatted());
  EXPECT_TRUE(device.mounted());
}

// Tests adding factoryfs with a valid superblock, as a device which is not a
// verified child of a block-verity device.
TEST(AddDeviceTestCase, AddUnverifiedFactoryFsDevice) {
  BlockDeviceManager manager(TestOptions());
  MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
  EXPECT_EQ(manager.AddDevice(gpt_device), ZX_OK);
  MockFactoryfsDevice device;
  EXPECT_EQ(manager.AddDevice(device), ZX_ERR_NOT_SUPPORTED);
  EXPECT_FALSE(device.checked());
  EXPECT_FALSE(device.formatted());
  EXPECT_FALSE(device.mounted());
}

TEST(AddDeviceTestCase, MultipleFvmDevicesDoNotMatch) {
  BlockDeviceManager manager(TestOptions());
  {
    MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
    EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
  }
  // If another FVM device appears, it should fail.
  {
    MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, manager.AddDevice(fvm_device));
  }
}

TEST(AddDeviceTestCase, MultipleGptDevicesDoNotMatch) {
  BlockDeviceManager manager(TestOptions());
  {
    MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
    EXPECT_EQ(manager.AddDevice(gpt_device), ZX_OK);
  }
  // If another GPT device appears, it should fail.
  {
    MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, manager.AddDevice(gpt_device));
  }
}

TEST(AddDeviceTestCase, MultipleGptDevicesWithGptAllOptionMatch) {
  auto options = TestOptions();
  options.options.insert({BlockDeviceManager::Options::kGptAll, {}});
  BlockDeviceManager manager(options);
  {
    MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
    EXPECT_EQ(manager.AddDevice(gpt_device), ZX_OK);
  }
  {
    MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
    EXPECT_EQ(manager.AddDevice(gpt_device), ZX_OK);
  }
}

class BlockWatcherTest : public testing::Test {
 protected:
  void SetUp() override {
    fidl::SynchronousInterfacePtr<fuchsia::sys2::Realm> realm;
    std::string service_name = std::string("/svc/") + fuchsia::sys2::Realm::Name_;
    auto status = zx::make_status(
        fdio_service_connect(service_name.c_str(), realm.NewRequest().TakeChannel().get()));
    ASSERT_TRUE(status.is_ok());
    fidl::SynchronousInterfacePtr<fuchsia::io::Directory> exposed_dir;
    fuchsia::sys2::Realm_BindChild_Result result;
    status = zx::make_status(realm->BindChild(fuchsia::sys2::ChildRef{.name = "test-fshost"},
                                              exposed_dir.NewRequest(), &result));
    ASSERT_TRUE(status.is_ok() && !result.is_err());

    fuchsia::io::NodeInfo info;
    ASSERT_EQ(exposed_dir->Describe(&info), ZX_OK);

    zx::channel request;
    status = zx::make_status(zx::channel::create(0, &watcher_chan_, &request));
    ASSERT_EQ(status.status_value(), ZX_OK);
    service_name = std::string("svc/") + llcpp::fuchsia::fshost::BlockWatcher::Name;
    status = zx::make_status(
        exposed_dir->Open(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE, 0,
                          llcpp::fuchsia::fshost::BlockWatcher::Name,
                          fidl::InterfaceRequest<fuchsia::io::Node>(std::move(request))));
    ASSERT_EQ(status.status_value(), ZX_OK);
  }

  isolated_devmgr::RamDisk CreateGptRamdisk() {
    zx::vmo ramdisk_vmo;
    EXPECT_EQ(zx::vmo::create(kTestDiskSectors * kBlockSize, 0, &ramdisk_vmo), ZX_OK);
    // Write the GPT into the VMO.
    EXPECT_EQ(ramdisk_vmo.write(kTestGptProtectiveMbr, 0, sizeof(kTestGptProtectiveMbr)), ZX_OK);
    EXPECT_EQ(ramdisk_vmo.write(kTestGptBlock1, kBlockSize, sizeof(kTestGptBlock1)), ZX_OK);
    EXPECT_EQ(ramdisk_vmo.write(kTestGptBlock2, 2 * kBlockSize, sizeof(kTestGptBlock2)), ZX_OK);

    return isolated_devmgr::RamDisk::CreateWithVmo(std::move(ramdisk_vmo), kBlockSize).value();
  }

  void PauseWatcher() {
    auto result = llcpp::fuchsia::fshost::BlockWatcher::Call::Pause(watcher_chan_.borrow());
    ASSERT_EQ(result.status(), ZX_OK);
    ASSERT_EQ(result->status, ZX_OK);
  }

  void ResumeWatcher() {
    auto result = llcpp::fuchsia::fshost::BlockWatcher::Call::Resume(watcher_chan_.borrow());
    ASSERT_EQ(result.status(), ZX_OK);
    ASSERT_EQ(result->status, ZX_OK);
  }

  fbl::unique_fd WaitForBlockDevice(int number) {
    auto path = fbl::StringPrintf("/dev/class/block/%03d", number);
    EXPECT_EQ(wait_for_device(path.data(), ZX_TIME_INFINITE), ZX_OK);
    return fbl::unique_fd(open(path.data(), O_RDWR));
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
  void CheckEventsDropped(int& next_device_number, isolated_devmgr::RamDisk& ramdisk) {
    ASSERT_NO_FATAL_FAILURE(ramdisk = CreateGptRamdisk());

    // Wait for the basic block driver to be bound
    WaitForBlockDevice(next_device_number);
    next_device_number += 1;

    // And now, wait for the GPT driver to be bound, and the first
    // partition to appear.
    fbl::unique_fd fd = WaitForBlockDevice(next_device_number);
    next_device_number += 1;

    // Figure out the expected topological path of the last block device.
    std::string expected_path = ramdisk.path() + "/part-000/block";

    zx_handle_t handle;
    ASSERT_EQ(fdio_get_service_handle(fd.release(), &handle), ZX_OK);
    zx::channel channel(handle);
    // Get the actual topological path of the block device.
    auto result = llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(channel.borrow());
    ASSERT_EQ(result.status(), ZX_OK);
    ASSERT_FALSE(result->result.is_err());

    auto actual_path =
        std::string(result->result.response().path.begin(), result->result.response().path.size());
    // Make sure expected path matches the actual path.
    ASSERT_EQ(actual_path, expected_path);
  }

  zx::channel watcher_chan_;
};

TEST_F(BlockWatcherTest, TestBlockWatcherDisable) {
  ASSERT_NO_FATAL_FAILURE(PauseWatcher());

  // Add a block device.
  isolated_devmgr::RamDisk client;
  ASSERT_NO_FATAL_FAILURE(client = CreateGptRamdisk());

  // Figure out what the next device number will be.
  int next_device_number;
  {
    files::Glob glob("/dev/class/block/*");
    ASSERT_GT(glob.size(), 0ul);
    auto iterator = glob.end();
    --iterator;
    ASSERT_EQ(sscanf(*iterator, "/dev/class/block/%d", &next_device_number), 1);
  }
  next_device_number++;

  ASSERT_NO_FATAL_FAILURE(ResumeWatcher());

  isolated_devmgr::RamDisk client2;
  ASSERT_NO_FATAL_FAILURE(CheckEventsDropped(next_device_number, client2));
}

TEST_F(BlockWatcherTest, TestBlockWatcherAdd) {
  // Add a block device.
  isolated_devmgr::RamDisk client;
  ASSERT_NO_FATAL_FAILURE(client = CreateGptRamdisk());

  // Wait for fshost to bind the gpt driver.
  EXPECT_EQ(wait_for_device((client.path() + "/part-000/block").c_str(), ZX_TIME_INFINITE), ZX_OK);
}

TEST_F(BlockWatcherTest, TestBlockWatcherUnmatchedResume) {
  auto result = llcpp::fuchsia::fshost::BlockWatcher::Call::Resume(watcher_chan_.borrow());
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_EQ(result->status, ZX_ERR_BAD_STATE);
}

TEST_F(BlockWatcherTest, TestMultiplePause) {
  ASSERT_NO_FATAL_FAILURE(PauseWatcher());
  ASSERT_NO_FATAL_FAILURE(PauseWatcher());
  int next_device_number = 0;

  // Add a block device.
  isolated_devmgr::RamDisk client;
  ASSERT_NO_FATAL_FAILURE(client = CreateGptRamdisk());

  // Figure out what the next device number will be.
  {
    files::Glob glob("/dev/class/block/*");
    ASSERT_GT(glob.size(), 0ul);
    auto iterator = glob.end();
    --iterator;
    ASSERT_EQ(sscanf(*iterator, "/dev/class/block/%d", &next_device_number), 1);
  }
  next_device_number++;

  // Resume once.
  ASSERT_NO_FATAL_FAILURE(ResumeWatcher());

  isolated_devmgr::RamDisk client2;
  ASSERT_NO_FATAL_FAILURE(client2 = CreateGptRamdisk());
  ASSERT_NO_FATAL_FAILURE(WaitForBlockDevice(next_device_number));
  next_device_number++;

  ASSERT_EQ(wait_for_device(client2.path().c_str(), ZX_TIME_INFINITE), ZX_OK);
  // Resume again. The block watcher should be running again.
  ASSERT_NO_FATAL_FAILURE(ResumeWatcher());

  // Make sure neither device was seen by the watcher.
  isolated_devmgr::RamDisk client3;
  ASSERT_NO_FATAL_FAILURE(CheckEventsDropped(next_device_number, client3));

  // Pause again.
  ASSERT_NO_FATAL_FAILURE(PauseWatcher());
  isolated_devmgr::RamDisk client4;
  client4 = CreateGptRamdisk();
  ASSERT_NO_FATAL_FAILURE(WaitForBlockDevice(next_device_number));
  next_device_number++;
  // Resume again.
  ASSERT_NO_FATAL_FAILURE(ResumeWatcher());

  // Make sure the last device wasn't added.
  isolated_devmgr::RamDisk client5;
  ASSERT_NO_FATAL_FAILURE(CheckEventsDropped(next_device_number, client5));
}

TEST_F(BlockWatcherTest, TestResumeThenImmediatelyPause) {
  ASSERT_NO_FATAL_FAILURE(PauseWatcher());
  int next_device_number = 0;

  // Add a block device, which should be ignored.
  isolated_devmgr::RamDisk client;
  ASSERT_NO_FATAL_FAILURE(client = CreateGptRamdisk());

  // Figure out what the next device number will be.
  {
    files::Glob glob("/dev/class/block/*");
    ASSERT_GT(glob.size(), 0ul);
    auto iterator = glob.end();
    --iterator;
    ASSERT_EQ(sscanf(*iterator, "/dev/class/block/%d", &next_device_number), 1);
  }
  next_device_number++;

  // Resume.
  ASSERT_NO_FATAL_FAILURE(ResumeWatcher());
  // Pause immediately.
  ASSERT_NO_FATAL_FAILURE(PauseWatcher());

  // Add another block device, which should also be ignored.
  isolated_devmgr::RamDisk client2;
  ASSERT_NO_FATAL_FAILURE(client2 = CreateGptRamdisk());
  ASSERT_NO_FATAL_FAILURE(WaitForBlockDevice(next_device_number));
  next_device_number++;

  // Resume again.
  ASSERT_NO_FATAL_FAILURE(ResumeWatcher());

  // Make sure the block watcher correctly resumed.
  isolated_devmgr::RamDisk client3;
  ASSERT_NO_FATAL_FAILURE(CheckEventsDropped(next_device_number, client3));
}

}  // namespace
}  // namespace devmgr

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/watcher.h>
#include <zircon/assert.h>
#include <zircon/device/block.h>
#include <zircon/hw/gpt.h>

#include <string_view>

#include <fbl/string_printf.h>
#include <gtest/gtest.h>
#include <ramdevice-client/ramdisk.h>

#include "src/lib/files/glob.h"
#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/storage/fshost/block-device-interface.h"
#include "src/storage/fshost/block-device-manager.h"
#include "src/storage/fshost/block-watcher-test-data.h"
#include "src/storage/fshost/config.h"
#include "src/storage/fshost/constants.h"
#include "src/storage/fshost/encrypted-volume-interface.h"
#include "src/storage/fshost/testing/fshost_integration_test.h"
#include "src/storage/fshost/testing/mock-block-device.h"
#include "src/storage/testing/ram_disk.h"

namespace fshost {
namespace {

using ::fshost::testing::MockBlobfsDevice;
using ::fshost::testing::MockBlockDevice;
using ::fshost::testing::MockBlockVerityDevice;
using ::fshost::testing::MockFactoryfsDevice;
using ::fshost::testing::MockMinfsDevice;
using ::fshost::testing::MockSealedBlockVerityDevice;
using ::fshost::testing::MockZxcryptDevice;

fshost_config::Config FactoryOptions() {
  auto options = fshost::DefaultConfig();
  options.factory() = true;
  return options;
}
fshost_config::Config NandOptions() {
  auto options = fshost::DefaultConfig();
  options.nand() = true;
  return options;
}

TEST(AddDeviceTestCase, AddNandDeviceUseBroker) {
  auto config = NandOptions();
  BlockDeviceManager manager(&config);
  MockBlockDevice device(MockBlockDevice::NandOptions());

  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.attached());
}

TEST(AddDeviceTestCase, AddNandDeviceNoBroker) {
  auto config = fshost::DefaultConfig();
  BlockDeviceManager manager(&config);
  MockBlockDevice device(MockBlockDevice::NandOptions());

  EXPECT_EQ(manager.AddDevice(device), ZX_ERR_NOT_SUPPORTED);
  EXPECT_FALSE(device.attached());
}

// Tests adding a device which has an unknown format.
TEST(AddDeviceTestCase, AddUnknownDevice) {
  MockBlockDevice device;
  auto config = fshost::DefaultConfig();
  BlockDeviceManager manager(&config);
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, manager.AddDevice(device));
}

// Tests adding a device which is smaller than the expected header size
TEST(AddDeviceTestCase, AddSmallDevice) {
  class SmallDevice : public MockBlockDevice {
   public:
    zx::result<fuchsia_hardware_block::wire::BlockInfo> GetInfo() const override {
      fuchsia_hardware_block::wire::BlockInfo info = {};
      info.flags = 0;
      info.block_size = 512;
      info.block_count = 1;
      return zx::ok(info);
    }
  };
  SmallDevice device;
  auto config = fshost::DefaultConfig();
  BlockDeviceManager manager(&config);
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, manager.AddDevice(device));
}

// Tests adding a device with a GPT format.
TEST(AddDeviceTestCase, AddGPTDevice) {
  MockBlockDevice device(MockBlockDevice::GptOptions());
  auto config = fshost::DefaultConfig();
  BlockDeviceManager manager(&config);
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.attached());
}

// Tests adding a device with an FVM format.
TEST(AddDeviceTestCase, AddFVMDevice) {
  auto config = fshost::DefaultConfig();
  BlockDeviceManager manager(&config);
  MockBlockDevice device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.attached());
}

// Tests adding a device with an MBR format.
TEST(AddDeviceTestCase, AddMBRDevice) {
  auto config = fshost::DefaultConfig();
  config.mbr() = true;
  BlockDeviceManager manager(&config);
  MockBlockDevice device(MockBlockDevice::Options{
      .content_format = fs_management::kDiskFormatMbr,
      .driver_path = kMBRDriverPath,
  });
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.attached());
}

TEST(AddDeviceTestCase, AddBlockVerityDevice) {
  auto config = FactoryOptions();
  BlockDeviceManager manager(&config);
  MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
  EXPECT_EQ(manager.AddDevice(gpt_device), ZX_OK);
  MockBlockVerityDevice device(/*allow_authoring=*/true);
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.attached());
}

TEST(AddDeviceTestCase, NonFactoryBlockVerityDeviceNotAttached) {
  auto config = FactoryOptions();
  BlockDeviceManager manager(&config);
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
  auto config = FactoryOptions();
  BlockDeviceManager manager(&config);
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

    zx::result<std::string> VeritySeal() final {
      seal_read_ = true;
      return zx::error_result(ZX_ERR_NOT_FOUND);
    }
    zx_status_t OpenBlockVerityForVerifiedRead(std::string seal_hex) final {
      ADD_FAILURE() << "Should not call OpenBlockVerityForVerifiedRead";
      return ZX_OK;
    }
    bool seal_read() const { return seal_read_; }

   private:
    bool seal_read_ = false;
  };
  auto config = FactoryOptions();
  BlockDeviceManager manager(&config);
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

    zx::result<std::string> VeritySeal() final {
      ADD_FAILURE() << "Should not call VeritySeal";
      return zx::error_result(ZX_ERR_NOT_FOUND);
    }
    zx_status_t OpenBlockVerityForVerifiedRead(std::string seal_hex) final {
      ADD_FAILURE() << "Should not call OpenBlockVerityForVerifiedRead";
      return ZX_OK;
    }
  };
  auto config = FactoryOptions();
  BlockDeviceManager manager(&config);
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
    const fuchsia_hardware_block_partition::wire::Guid& GetTypeGuid() const final {
      static fuchsia_hardware_block_partition::wire::Guid guid = GUID_TEST_VALUE;
      return guid;
    }
  };

  auto config = fshost::DefaultConfig();
  BlockDeviceManager manager(&config);
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
  auto config = fshost::DefaultConfig();
  BlockDeviceManager manager(&config);
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
  auto config = fshost::DefaultConfig();
  BlockDeviceManager manager(&config);
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
  MockBlobfsDevice device;
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.checked());
  EXPECT_FALSE(device.formatted());
  EXPECT_TRUE(device.mounted());
}

TEST(AddDeviceTestCase, NetbootingDoesNotMountBlobfs) {
  auto config = fshost::DefaultConfig();
  config.netboot() = true;
  BlockDeviceManager manager(&config);
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

    const fuchsia_hardware_block_partition::wire::Guid& GetTypeGuid() const final {
      static fuchsia_hardware_block_partition::wire::Guid guid = GUID_TEST_VALUE;
      return guid;
    }
  };
  auto config = fshost::DefaultConfig();
  BlockDeviceManager manager(&config);
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
    fs_management::DiskFormat content_format() const final {
      return fs_management::kDiskFormatMinfs;
    }
  };
  auto config = fshost::DefaultConfig();
  EXPECT_TRUE(config.format_data_on_corruption());
  BlockDeviceManager manager(&config);
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
    fs_management::DiskFormat content_format() const final {
      return fs_management::kDiskFormatMinfs;
    }
  };
  auto config = fshost::DefaultConfig();
  config.format_data_on_corruption() = false;
  BlockDeviceManager manager(&config);
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
  auto config = fshost::DefaultConfig();
  BlockDeviceManager manager(&config);
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
  MockBlockDevice::Options options = MockZxcryptDevice::ZxcryptOptions();
  options.content_format = fs_management::kDiskFormatUnknown;
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
  auto config = fshost::DefaultConfig();
  BlockDeviceManager manager(&config);
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
  MockBlockDevice::Options options = MockZxcryptDevice::ZxcryptOptions();
  options.content_format = fs_management::kDiskFormatMinfs;
  MockZxcryptDevice zxcrypt_device(options);
  EXPECT_EQ(manager.AddDevice(zxcrypt_device), ZX_OK);
  MockMinfsDevice device;
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(zxcrypt_device.formatted_zxcrypt());
  EXPECT_TRUE(device.formatted());
  EXPECT_TRUE(device.mounted());
}

TEST(AddDeviceTestCase, MinfsWithNoZxcryptOptionMountsWithoutZxcrypt) {
  auto config = fshost::DefaultConfig();
  config.no_zxcrypt() = true;
  BlockDeviceManager manager(&config);
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
  auto minfs_options = MockMinfsDevice::MinfsOptions();
  minfs_options.topological_path = MockBlockDevice::BaseTopologicalPath() + "/fvm/minfs-p-2/block";
  minfs_options.partition_name = kDataPartitionLabel;
  MockMinfsDevice device(minfs_options);
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.mounted());
}

TEST(AddDeviceTestCase, MinfsRamdiskMounts) {
  // The fvm-ramdisk option will check that the topological path actually has an expected ramdisk
  // prefix.
  auto config = fshost::DefaultConfig();
  config.fvm_ramdisk() = true;
  BlockDeviceManager manager(&config);
  auto options = MockBlockDevice::FvmOptions();
  options.is_ramdisk = true;
  MockBlockDevice fvm_device(options);
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
  options = MockMinfsDevice::MinfsOptions();
  options.topological_path = fvm_device.topological_path() + "/fvm/minfs-p-2/block";
  options.partition_name = kDataPartitionLabel;
  MockMinfsDevice device(options);
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.mounted());
}

TEST(AddDeviceTestCase, MinfsRamdiskDeviceNotRamdiskDoesNotMount) {
  auto config = fshost::DefaultConfig();
  config.fvm_ramdisk() = true;
  BlockDeviceManager manager(&config);
  auto fvm_options = MockBlockDevice::FvmOptions();
  fvm_options.topological_path = "/dev/sys/platform/00:00:2d/ramctl/mock_device/block";
  fvm_options.is_ramdisk = true;
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

TEST(AddDeviceTestCase, MinfsWithAlternateNameMounts) {
  for (int pass = 0; pass < 2; ++pass) {
    auto config = fshost::DefaultConfig();
    if (pass == 1)
      config.allow_legacy_data_partition_names() = true;
    for (const auto& name :
         {kDataPartitionLabel, std::string_view("minfs"), std::string_view("fuchsia-data")}) {
      BlockDeviceManager manager(&config);
      MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
      EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
      MockZxcryptDevice zxcrypt_device;
      EXPECT_EQ(manager.AddDevice(zxcrypt_device), ZX_OK);
      auto minfs_options = MockMinfsDevice::MinfsOptions();
      minfs_options.partition_name = name;
      MockMinfsDevice device(minfs_options);
      EXPECT_EQ(manager.AddDevice(device), ZX_OK);
      EXPECT_TRUE(device.mounted());
      if (pass == 0)
        break;
    }
  }
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
    zx::result<fuchsia_hardware_block::wire::BlockInfo> GetInfo() const override {
      fuchsia_hardware_block::wire::BlockInfo info = {};
      info.flags = BLOCK_FLAG_BOOTPART;
      info.block_size = 512;
      info.block_count = 1024;
      return zx::ok(info);
    }
  };
  BootPartDevice device;
  auto config = fshost::DefaultConfig();
  BlockDeviceManager manager(&config);
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.attached());
}

TEST(AddDeviceTestCase, AddPermanentlyMiskeyedZxcryptVolume) {
  class ZxcryptVolume : public EncryptedVolumeInterface {
   public:
    zx_status_t Unseal() final {
      // Simulate a device where we've lost the key -- can't unlock until we
      // format the device with a new key, but can afterwards.
      if (formatted) {
        postformat_unseal_attempt_count++;
        return ZX_OK;
      }
      preformat_unseal_attempt_count++;
      return ZX_ERR_ACCESS_DENIED;
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
  class ZxcryptVolume : public EncryptedVolumeInterface {
   public:
    zx_status_t Unseal() final {
      // Simulate a transient error -- fail the first time we try to unseal the
      // volume, but succeed on a retry or any subsequent attempt.
      unseal_attempt_count++;
      if (unseal_attempt_count > 1) {
        return ZX_OK;
      }
      return ZX_ERR_ACCESS_DENIED;
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
  class ZxcryptVolume : public EncryptedVolumeInterface {
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
  auto config = FactoryOptions();
  BlockDeviceManager manager(&config);
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
  auto config = FactoryOptions();
  BlockDeviceManager manager(&config);
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
  auto config = fshost::DefaultConfig();
  BlockDeviceManager manager(&config);
  MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
  EXPECT_EQ(manager.AddDevice(gpt_device), ZX_OK);
  MockFactoryfsDevice device;
  EXPECT_EQ(manager.AddDevice(device), ZX_ERR_NOT_SUPPORTED);
  EXPECT_FALSE(device.checked());
  EXPECT_FALSE(device.formatted());
  EXPECT_FALSE(device.mounted());
}

TEST(AddDeviceTestCase, MultipleFvmDevicesDoNotMatch) {
  auto config = fshost::DefaultConfig();
  BlockDeviceManager manager(&config);
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
  auto config = fshost::DefaultConfig();
  BlockDeviceManager manager(&config);
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
  auto config = fshost::DefaultConfig();
  config.gpt_all() = true;
  BlockDeviceManager manager(&config);
  {
    MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
    EXPECT_EQ(manager.AddDevice(gpt_device), ZX_OK);
  }
  {
    MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
    EXPECT_EQ(manager.AddDevice(gpt_device), ZX_OK);
  }
}

class BlockWatcherTest : public testing::FshostIntegrationTest {
 protected:
  static storage::RamDisk CreateGptRamdisk() {
    zx::vmo ramdisk_vmo;
    EXPECT_EQ(zx::vmo::create(kTestDiskSectors * kBlockSize, 0, &ramdisk_vmo), ZX_OK);
    // Write the GPT into the VMO.
    EXPECT_EQ(ramdisk_vmo.write(kTestGptProtectiveMbr, 0, sizeof(kTestGptProtectiveMbr)), ZX_OK);
    EXPECT_EQ(ramdisk_vmo.write(kTestGptBlock1, kBlockSize, sizeof(kTestGptBlock1)), ZX_OK);
    EXPECT_EQ(ramdisk_vmo.write(kTestGptBlock2, 2 * kBlockSize, sizeof(kTestGptBlock2)), ZX_OK);

    return storage::RamDisk::CreateWithVmo(std::move(ramdisk_vmo), kBlockSize).value();
  }

  static fbl::unique_fd WaitForBlockDevice(int number) {
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
  static void CheckEventsDropped(int& next_device_number, storage::RamDisk& ramdisk) {
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

    fidl::ClientEnd<fuchsia_device::Controller> controller;
    ASSERT_EQ(fdio_get_service_handle(fd.release(), controller.channel().reset_and_get_address()),
              ZX_OK);
    // Get the actual topological path of the block device.
    auto result = fidl::WireSyncClient(std::move(controller))->GetTopologicalPath();
    ASSERT_EQ(result.status(), ZX_OK);
    ASSERT_FALSE(result->is_error());

    auto actual_path = std::string(result->value()->path.begin(), result->value()->path.size());
    // Make sure expected path matches the actual path.
    ASSERT_EQ(actual_path, expected_path);
  }
};

TEST_F(BlockWatcherTest, TestBlockWatcherDisable) {
  ASSERT_NO_FATAL_FAILURE(PauseWatcher());

  // Add a block device.
  storage::RamDisk client;
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

  storage::RamDisk client2;
  ASSERT_NO_FATAL_FAILURE(CheckEventsDropped(next_device_number, client2));
}

TEST_F(BlockWatcherTest, TestBlockWatcherAdd) {
  // Add a block device.
  storage::RamDisk client;
  ASSERT_NO_FATAL_FAILURE(client = CreateGptRamdisk());

  // Wait for fshost to bind the gpt driver.
  EXPECT_EQ(wait_for_device((client.path() + "/part-000/block").c_str(), ZX_TIME_INFINITE), ZX_OK);
}

TEST_F(BlockWatcherTest, TestBlockWatcherUnmatchedResume) {
  auto result = block_watcher()->Resume();
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_EQ(result.value().status, ZX_ERR_BAD_STATE);
}

TEST_F(BlockWatcherTest, TestSecondPauseFails) {
  ASSERT_NO_FATAL_FAILURE(PauseWatcher());
  auto result = block_watcher()->Pause();
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_EQ(result->status, ZX_ERR_BAD_STATE);
  ASSERT_NO_FATAL_FAILURE(ResumeWatcher());
}

TEST_F(BlockWatcherTest, TestResumeThenImmediatelyPause) {
  ASSERT_NO_FATAL_FAILURE(PauseWatcher());
  int next_device_number = 0;

  // Add a block device, which should be ignored.
  storage::RamDisk client;
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
  storage::RamDisk client2;
  ASSERT_NO_FATAL_FAILURE(client2 = CreateGptRamdisk());
  ASSERT_NO_FATAL_FAILURE(WaitForBlockDevice(next_device_number));
  next_device_number++;

  // Resume again.
  ASSERT_NO_FATAL_FAILURE(ResumeWatcher());

  // Make sure the block watcher correctly resumed.
  storage::RamDisk client3;
  ASSERT_NO_FATAL_FAILURE(CheckEventsDropped(next_device_number, client3));
}

}  // namespace
}  // namespace fshost

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

#include <string_view>

#include <ramdevice-client/ramdisk.h>
#include <zxtest/zxtest.h>

#include "block-device-interface.h"
#include "block-device-manager.h"
#include "block-watcher-test-data.h"
#include "encrypted-volume-interface.h"

namespace devmgr {
namespace {

using ::devmgr_integration_test::RecursiveWaitForFile;
using ::driver_integration_test::IsolatedDevmgr;

class MockBlockDevice : public devmgr::BlockDeviceInterface {
 public:
  static const std::string& BaseTopologicalPath() {
    static std::string* path = new std::string("/dev/mock_device/block");
    return *path;
  }

  struct Options {
    static Options Default() { return {}; }

    disk_format_t content_format = DISK_FORMAT_UNKNOWN;
    std::string_view driver_path;
    std::string topological_path = BaseTopologicalPath();
    std::string partition_name;
  };

  static Options GptOptions() {
    return {.content_format = DISK_FORMAT_GPT, .driver_path = kGPTDriverPath};
  }

  static Options FvmOptions() {
    return {.content_format = DISK_FORMAT_FVM, .driver_path = kFVMDriverPath};
  }

  static Options MinfsZxcryptOptions() {
    return {
        .content_format = DISK_FORMAT_ZXCRYPT,
        .driver_path = kZxcryptDriverPath,
        .topological_path = MockBlockDevice::BaseTopologicalPath() + "/fvm/minfs-p-2/block",
        .partition_name = "minfs",
    };
  }

  static Options DurableZxcryptOptions() {
    return {
        .content_format = DISK_FORMAT_ZXCRYPT,
        .driver_path = kZxcryptDriverPath,
        .topological_path = MockBlockDevice::BaseTopologicalPath() + "/durable-004/block",
        .partition_name = "durable",
    };
  }

  static Options DurableOptions() {
    return {
        .topological_path =
            MockBlockDevice::BaseTopologicalPath() + "/durable-004/block/zxcrypt/unsealed/block",
    };
  }

  MockBlockDevice(const Options& options = Options::Default()) : options_(options) {}

  disk_format_t content_format() const override { return options_.content_format; }
  const std::string& topological_path() const override { return options_.topological_path; }
  const std::string& partition_name() const override { return options_.partition_name; }
  disk_format_t GetFormat() final { return format_; }
  void SetFormat(disk_format_t format) final { format_ = format; }
  zx_status_t GetInfo(fuchsia_hardware_block_BlockInfo* out_info) const override {
    fuchsia_hardware_block_BlockInfo info = {};
    info.flags = 0;
    info.block_size = 512;
    info.block_count = 1024;
    *out_info = info;
    return ZX_OK;
  }
  const fuchsia_hardware_block_partition_GUID& GetTypeGuid() const override {
    ADD_FAILURE("Test should not invoke function %s\n", __FUNCTION__);
    static fuchsia_hardware_block_partition_GUID null_guid;
    return null_guid;
  }
  zx_status_t AttachDriver(const std::string_view& driver) override {
    EXPECT_EQ(driver, options_.driver_path);
    EXPECT_FALSE(attached_);
    attached_ = true;
    return ZX_OK;
  }
  zx_status_t UnsealZxcrypt() override {
    ADD_FAILURE("Test should not invoke function %s\n", __FUNCTION__);
    return ZX_ERR_INTERNAL;
  }
  zx_status_t FormatZxcrypt() override {
    ADD_FAILURE("Test should not invoke function %s\n", __FUNCTION__);
    return ZX_ERR_INTERNAL;
  }
  bool ShouldCheckFilesystems() override {
    ADD_FAILURE("Test should not invoke function %s\n", __FUNCTION__);
    return false;
  }
  zx_status_t CheckFilesystem() override {
    ADD_FAILURE("Test should not invoke function %s\n", __FUNCTION__);
    return ZX_ERR_INTERNAL;
  }
  zx_status_t FormatFilesystem() override {
    ADD_FAILURE("Test should not invoke function %s\n", __FUNCTION__);
    return ZX_ERR_INTERNAL;
  }
  zx_status_t MountFilesystem() override {
    ADD_FAILURE("Test should not invoke function %s\n", __FUNCTION__);
    return ZX_ERR_INTERNAL;
  }
  zx::status<std::string> VeritySeal() override {
    ADD_FAILURE("Test should not invoke function %s\n", __FUNCTION__);
    return zx::error(ZX_ERR_INTERNAL);
  }
  zx_status_t OpenBlockVerityForVerifiedRead(std::string seal_hex) override {
    ADD_FAILURE("Test should not invoke function %s\n", __FUNCTION__);
    return ZX_ERR_INTERNAL;
  }
  bool ShouldAllowAuthoringFactory() override {
    ADD_FAILURE("Test should not invoke function %s\n", __FUNCTION__);
    return false;
  }

  bool attached() const { return attached_; }

 private:
  const Options options_;
  disk_format_t format_ = DISK_FORMAT_UNKNOWN;
  bool attached_ = false;
};

class BlockVerityDevice : public MockBlockDevice {
 public:
  static Options VerityOptions() {
    return Options{.driver_path = kBlockVerityDriverPath,
                   .topological_path = BaseTopologicalPath() + "/factory-001/block",
                   .partition_name = "factory"};
  }

  BlockVerityDevice(bool allow_authoring, const Options& options = VerityOptions())
      : MockBlockDevice(options), allow_authoring_(allow_authoring) {}
  const fuchsia_hardware_block_partition_GUID& GetTypeGuid() const final {
    static fuchsia_hardware_block_partition_GUID guid = GPT_FACTORY_TYPE_GUID;
    return guid;
  }
  bool ShouldAllowAuthoringFactory() override { return allow_authoring_; }

 private:
  const bool allow_authoring_;
};

static constexpr char kFakeSeal[] =
    "0000000000000000000000000000000000000000000000000000000000000000";

class SealedBlockVerityDevice : public BlockVerityDevice {
 public:
  SealedBlockVerityDevice() : BlockVerityDevice(/*allow_authoring=*/false) {}

  zx::status<std::string> VeritySeal() final { return zx::ok(std::string(kFakeSeal)); }
  zx_status_t OpenBlockVerityForVerifiedRead(std::string seal_hex) final {
    EXPECT_STR_EQ(kFakeSeal, seal_hex);
    opened_ = true;
    return ZX_OK;
  }
  bool opened() const { return opened_; }

 private:
  bool opened_ = false;
};

class FactoryfsDevice : public MockBlockDevice {
 public:
  static Options FactoryfsOptions() {
    return Options{
        .topological_path = BaseTopologicalPath() + "/factory-001/block/verity/verified/block",
    };
  }

  FactoryfsDevice() : MockBlockDevice(FactoryfsOptions()) {}

  const fuchsia_hardware_block_partition_GUID& GetTypeGuid() const final {
    static fuchsia_hardware_block_partition_GUID guid = GPT_FACTORY_TYPE_GUID;
    return guid;
  }
  zx_status_t CheckFilesystem() override {
    checked_ = true;
    return ZX_OK;
  }
  zx_status_t FormatFilesystem() override {
    formatted_ = true;
    return ZX_OK;
  }
  zx_status_t MountFilesystem() override {
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

class BlobDevice : public MockBlockDevice {
 public:
  static Options BlobfsOptions() {
    return {
        .topological_path = MockBlockDevice::BaseTopologicalPath() + "/fvm/blobfs-p-1/block",
        .partition_name = "blobfs",
    };
  }

  BlobDevice() : MockBlockDevice(BlobfsOptions()) {}

  const fuchsia_hardware_block_partition_GUID& GetTypeGuid() const override {
    static fuchsia_hardware_block_partition_GUID guid = GUID_BLOB_VALUE;
    return guid;
  }
  zx_status_t CheckFilesystem() override {
    checked_ = true;
    return ZX_OK;
  }
  zx_status_t FormatFilesystem() override {
    formatted_ = true;
    return ZX_OK;
  }
  zx_status_t MountFilesystem() override {
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

class MinfsDevice : public MockBlockDevice {
 public:
  static Options MinfsOptions() {
    return {
        .topological_path =
            MockBlockDevice::BaseTopologicalPath() + "/fvm/minfs-p-2/block/zxcrypt/unsealed/block",
    };
  }

  MinfsDevice(Options options = MinfsOptions()) : MockBlockDevice(options) {}

  const fuchsia_hardware_block_partition_GUID& GetTypeGuid() const final {
    static fuchsia_hardware_block_partition_GUID guid = GUID_DATA_VALUE;
    return guid;
  }
  zx_status_t CheckFilesystem() override {
    checked_ = true;
    return ZX_OK;
  }
  zx_status_t FormatFilesystem() override {
    formatted_ = true;
    return ZX_OK;
  }
  zx_status_t MountFilesystem() override {
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

BlockDeviceManager::Options TestOptions() { return BlockDeviceManager::DefaultOptions(); }

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
  EXPECT_OK(manager.AddDevice(device));
  EXPECT_TRUE(device.attached());
}

// Tests adding a device with an FVM format.
TEST(AddDeviceTestCase, AddFVMDevice) {
  BlockDeviceManager manager(TestOptions());
  MockBlockDevice device(MockBlockDevice::FvmOptions());
  EXPECT_OK(manager.AddDevice(device));
  EXPECT_TRUE(device.attached());
}

// Tests adding a device with an MBR format.
TEST(AddDeviceTestCase, AddMBRDevice) {
  auto options = TestOptions();
  options.options.emplace(BlockDeviceManager::Options::kMbr);
  BlockDeviceManager manager(options);
  MockBlockDevice device(MockBlockDevice::Options{
      .content_format = DISK_FORMAT_MBR,
      .driver_path = kMBRDriverPath,
  });
  EXPECT_OK(manager.AddDevice(device));
  EXPECT_TRUE(device.attached());
}

TEST(AddDeviceTestCase, AddBlockVerityDevice) {
  BlockDeviceManager manager(TestOptions());
  MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
  EXPECT_OK(manager.AddDevice(gpt_device));
  BlockVerityDevice device(/*allow_authoring=*/true);
  EXPECT_OK(manager.AddDevice(device));
  EXPECT_TRUE(device.attached());
}

TEST(AddDeviceTestCase, NonFactoryBlockVerityDeviceNotAttached) {
  BlockDeviceManager manager(TestOptions());
  MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
  EXPECT_OK(manager.AddDevice(gpt_device));
  MockBlockDevice::Options options = BlockVerityDevice::VerityOptions();
  options.partition_name = "not-factory";
  BlockVerityDevice device(/*allow_authoring=*/true, options);
  EXPECT_EQ(manager.AddDevice(device), ZX_ERR_NOT_SUPPORTED);
  EXPECT_FALSE(device.attached());
}

// Tests adding a device with the block-verity disk format
TEST(AddDeviceTestCase, AddFormattedBlockVerityDevice) {
  BlockDeviceManager manager(TestOptions());
  MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
  EXPECT_OK(manager.AddDevice(gpt_device));
  SealedBlockVerityDevice device;
  EXPECT_OK(manager.AddDevice(device));
  EXPECT_TRUE(device.attached());
  EXPECT_TRUE(device.opened());
}

// Tests adding a device with block-verity format but no seal provided by
// bootloader
TEST(AddDeviceTestCase, AddFormattedBlockVerityDeviceWithoutSeal) {
  class BlockVerityDeviceWithNoSeal : public BlockVerityDevice {
   public:
    BlockVerityDeviceWithNoSeal() : BlockVerityDevice(/*allow_authoring=*/false) {}

    zx::status<std::string> VeritySeal() final {
      seal_read_ = true;
      return zx::error_status(ZX_ERR_NOT_FOUND);
    }
    zx_status_t OpenBlockVerityForVerifiedRead(std::string seal_hex) final {
      ADD_FATAL_FAILURE("Should not call OpenBlockVerityForVerifiedRead");
      return ZX_OK;
    }
    bool seal_read() const { return seal_read_; }

   private:
    bool seal_read_ = false;
  };
  BlockDeviceManager manager(TestOptions());
  MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
  EXPECT_OK(manager.AddDevice(gpt_device));
  BlockVerityDeviceWithNoSeal device;
  EXPECT_EQ(ZX_ERR_NOT_FOUND, manager.AddDevice(device));
  EXPECT_TRUE(device.attached());
  EXPECT_TRUE(device.seal_read());
}

// Tests adding a device with block-verity format while in factory authoring mode
TEST(AddDeviceTestCase, AddFormattedBlockVerityDeviceInAuthoringMode) {
  class BlockVerityDeviceInAuthoringMode : public BlockVerityDevice {
   public:
    BlockVerityDeviceInAuthoringMode() : BlockVerityDevice(/*allow_authoring=*/true) {}

    zx::status<std::string> VeritySeal() final {
      ADD_FATAL_FAILURE("Should not call VeritySeal");
      return zx::error_status(ZX_ERR_NOT_FOUND);
    }
    zx_status_t OpenBlockVerityForVerifiedRead(std::string seal_hex) final {
      ADD_FATAL_FAILURE("Should not call OpenBlockVerityForVerifiedRead");
      return ZX_OK;
    }
  };
  BlockDeviceManager manager(TestOptions());
  MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
  EXPECT_OK(manager.AddDevice(gpt_device));
  BlockVerityDeviceInAuthoringMode device;
  EXPECT_OK(manager.AddDevice(device));
  EXPECT_TRUE(device.attached());
}

// Tests adding blobfs which does not not have a valid type GUID.
TEST(AddDeviceTestCase, AddNoGUIDBlobDevice) {
  class BlobDeviceWithInvalidTypeGuid : public BlobDevice {
   public:
    const fuchsia_hardware_block_partition_GUID& GetTypeGuid() const final {
      static fuchsia_hardware_block_partition_GUID guid = GUID_TEST_VALUE;
      return guid;
    }
  };

  BlockDeviceManager manager(TestOptions());
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_OK(manager.AddDevice(fvm_device));
  BlobDeviceWithInvalidTypeGuid device;
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, manager.AddDevice(device));
  EXPECT_FALSE(device.mounted());
}

// Tests adding blobfs with a valid type GUID, but invalid metadata.
TEST(AddDeviceTestCase, AddInvalidBlobDevice) {
  class BlobDeviceWithInvalidMetadata : public BlobDevice {
   public:
    zx_status_t CheckFilesystem() final {
      BlobDevice::CheckFilesystem();
      return ZX_ERR_BAD_STATE;
    }
  };
  BlockDeviceManager manager(TestOptions());
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_OK(manager.AddDevice(fvm_device));
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
  EXPECT_OK(manager.AddDevice(fvm_device));
  BlobDevice device;
  EXPECT_OK(manager.AddDevice(device));
  EXPECT_TRUE(device.checked());
  EXPECT_FALSE(device.formatted());
  EXPECT_TRUE(device.mounted());
}

TEST(AddDeviceTestCase, NetbootingDoesNotMountBlobfs) {
  auto options = TestOptions();
  options.options.emplace(BlockDeviceManager::Options::kNetboot);
  BlockDeviceManager manager(options);
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_OK(manager.AddDevice(fvm_device));
  BlobDevice device;
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
  EXPECT_OK(manager.AddDevice(fvm_device));
  MinfsDeviceWithInvalidGuid device(MockBlockDevice::MinfsZxcryptOptions());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, manager.AddDevice(device));
  EXPECT_FALSE(device.attached());
}

// Tests adding minfs with a valid type GUID and invalid metadata. Observe that
// the filesystem reformats itself.
TEST(AddDeviceTestCase, AddInvalidMinfsDevice) {
  class ZxcryptDevice : public MockBlockDevice {
   public:
    using MockBlockDevice::MockBlockDevice;

    const fuchsia_hardware_block_partition_GUID& GetTypeGuid() const final {
      static fuchsia_hardware_block_partition_GUID guid = GUID_DATA_VALUE;
      return guid;
    }
    zx_status_t UnsealZxcrypt() final { return ZX_OK; }
  };
  class MinfsDeviceWithInvalidMetadata : public MinfsDevice {
   public:
    zx_status_t CheckFilesystem() final {
      MinfsDevice::CheckFilesystem();
      return ZX_ERR_BAD_STATE;
    }
  };
  BlockDeviceManager manager(TestOptions());
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_OK(manager.AddDevice(fvm_device));
  ZxcryptDevice zxcrypt_device(MockBlockDevice::MinfsZxcryptOptions());
  EXPECT_OK(manager.AddDevice(zxcrypt_device));
  MinfsDeviceWithInvalidMetadata device;
  EXPECT_OK(manager.AddDevice(device));
  EXPECT_TRUE(device.checked());
  EXPECT_TRUE(device.formatted());
  EXPECT_TRUE(device.mounted());
}

// Tests adding zxcrypt with a valid type GUID and invalid format. Observe that
// the partition reformats itself.
TEST(AddDeviceTestCase, FormatZxcryptDevice) {
  class ZxcryptDevice : public MockBlockDevice {
   public:
    using MockBlockDevice::MockBlockDevice;

    const fuchsia_hardware_block_partition_GUID& GetTypeGuid() const final {
      static fuchsia_hardware_block_partition_GUID guid = GUID_DATA_VALUE;
      return guid;
    }
    zx_status_t FormatZxcrypt() final {
      formatted_zxcrypt_ = true;
      return ZX_OK;
    }
    zx_status_t UnsealZxcrypt() final { return ZX_OK; }
    bool formatted_zxcrypt() const { return formatted_zxcrypt_; }

   private:
    bool formatted_zxcrypt_ = false;
  };

  BlockDeviceManager manager(TestOptions());
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_OK(manager.AddDevice(fvm_device));
  MockBlockDevice::Options options = MockBlockDevice::MinfsZxcryptOptions();
  options.content_format = DISK_FORMAT_UNKNOWN;
  ZxcryptDevice zxcrypt_device(options);
  EXPECT_OK(manager.AddDevice(zxcrypt_device));
  MinfsDevice device;
  EXPECT_OK(manager.AddDevice(device));
  EXPECT_TRUE(zxcrypt_device.formatted_zxcrypt());
  EXPECT_TRUE(device.formatted());
  EXPECT_TRUE(device.mounted());
}

// Tests adding zxcrypt with a valid type GUID and minfs format i.e. it's a minfs partition without
// zxcrypt. Observe that the partition reformats itself.
TEST(AddDeviceTestCase, FormatMinfsDeviceWithZxcrypt) {
  class ZxcryptDevice : public MockBlockDevice {
   public:
    using MockBlockDevice::MockBlockDevice;

    const fuchsia_hardware_block_partition_GUID& GetTypeGuid() const final {
      static fuchsia_hardware_block_partition_GUID guid = GUID_DATA_VALUE;
      return guid;
    }
    zx_status_t FormatZxcrypt() final {
      formatted_zxcrypt_ = true;
      return ZX_OK;
    }
    zx_status_t UnsealZxcrypt() final { return ZX_OK; }
    bool formatted_zxcrypt() const { return formatted_zxcrypt_; }

   private:
    bool formatted_zxcrypt_ = false;
  };

  BlockDeviceManager manager(TestOptions());
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_OK(manager.AddDevice(fvm_device));
  MockBlockDevice::Options options = MockBlockDevice::MinfsZxcryptOptions();
  options.content_format = DISK_FORMAT_MINFS;
  ZxcryptDevice zxcrypt_device(options);
  EXPECT_OK(manager.AddDevice(zxcrypt_device));
  MinfsDevice device;
  EXPECT_OK(manager.AddDevice(device));
  EXPECT_TRUE(zxcrypt_device.formatted_zxcrypt());
  EXPECT_TRUE(device.formatted());
  EXPECT_TRUE(device.mounted());
}

TEST(AddDeviceTestCase, MinfsWithNoZxcryptOptionMountsWithoutZxcrypt) {
  auto options = TestOptions();
  options.options.emplace("no-zxcrypt");
  BlockDeviceManager manager(options);
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_OK(manager.AddDevice(fvm_device));
  auto minfs_options = MinfsDevice::MinfsOptions();
  minfs_options.topological_path = MockBlockDevice::BaseTopologicalPath() + "/fvm/minfs-p-2/block";
  minfs_options.partition_name = "minfs";
  MinfsDevice device(minfs_options);
  EXPECT_OK(manager.AddDevice(device));
  EXPECT_TRUE(device.mounted());
}

TEST(AddDeviceTestCase, MinfsRamdiskMounts) {
  // The minfs-ramdisk option will check that the topological path actually has an expected ramdisk
  // prefix.
  auto manager_options = TestOptions();
  manager_options.options.emplace("minfs-ramdisk");
  BlockDeviceManager manager(manager_options);
  auto options = MockBlockDevice::FvmOptions();
  constexpr std::string_view kBasePath = "/dev/misc/ramctl/mock_device/block";
  options.topological_path = kBasePath;
  MockBlockDevice fvm_device(options);
  EXPECT_OK(manager.AddDevice(fvm_device));
  options = MinfsDevice::MinfsOptions();
  options.topological_path = std::string(kBasePath) + "/fvm/minfs-p-2/block";
  options.partition_name = "minfs";
  MinfsDevice device(options);
  EXPECT_OK(manager.AddDevice(device));
  EXPECT_TRUE(device.mounted());
}

TEST(AddDeviceTestCase, MinfsRamdiskDeviceNotRamdiskDoesNotMount) {
  auto options = TestOptions();
  options.options.emplace("minfs-ramdisk");
  BlockDeviceManager manager(options);
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_OK(manager.AddDevice(fvm_device));
  auto minfs_options = MinfsDevice::MinfsOptions();
  minfs_options.topological_path = MockBlockDevice::BaseTopologicalPath() + "/fvm/minfs-p-2/block";
  minfs_options.partition_name = "minfs";
  MinfsDevice device(minfs_options);
  EXPECT_EQ(manager.AddDevice(device), ZX_ERR_NOT_SUPPORTED);
  EXPECT_FALSE(device.mounted());
}

TEST(AddDeviceTestCase, MinfsWithAlternateNameMounts) {
  class ZxcryptDevice : public MockBlockDevice {
   public:
    using MockBlockDevice::MockBlockDevice;

    const fuchsia_hardware_block_partition_GUID& GetTypeGuid() const final {
      static fuchsia_hardware_block_partition_GUID guid = GUID_DATA_VALUE;
      return guid;
    }
    zx_status_t UnsealZxcrypt() final { return ZX_OK; }
  };
  BlockDeviceManager manager(TestOptions());
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_OK(manager.AddDevice(fvm_device));
  ZxcryptDevice zxcrypt_device(MockBlockDevice::MinfsZxcryptOptions());
  EXPECT_OK(manager.AddDevice(zxcrypt_device));
  auto minfs_options = MinfsDevice::MinfsOptions();
  minfs_options.partition_name = GUID_DATA_NAME;
  MinfsDevice device(minfs_options);
  EXPECT_EQ(manager.AddDevice(device), ZX_OK);
  EXPECT_TRUE(device.mounted());
}

// Durable partition tests
// Tests adding minfs on durable partition with a valid type GUID and valid metadata.
TEST(AddDeviceTestCase, AddValidDurableDevice) {
  class ZxcryptDevice : public MockBlockDevice {
   public:
    using MockBlockDevice::MockBlockDevice;

    const fuchsia_hardware_block_partition_GUID& GetTypeGuid() const final {
      static fuchsia_hardware_block_partition_GUID guid = GPT_DURABLE_TYPE_GUID;
      return guid;
    }
    zx_status_t UnsealZxcrypt() final { return ZX_OK; }
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
  BlockDeviceManager manager(TestOptions());
  MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
  EXPECT_OK(manager.AddDevice(gpt_device));
  ZxcryptDevice zxcrypt_device(MockBlockDevice::DurableZxcryptOptions());
  EXPECT_OK(manager.AddDevice(zxcrypt_device));
  DurableDevice device(MockBlockDevice::DurableOptions());
  EXPECT_OK(manager.AddDevice(device));
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
  EXPECT_OK(manager.AddDevice(device));
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

// Tests adding factoryfs with valid factoryfs magic, as a verified child of a
// block-verity device, but with invalid metadata.
TEST(AddDeviceTestCase, AddInvalidFactoryfsDevice) {
  class FactoryfsWithInvalidMetadata : public FactoryfsDevice {
    zx_status_t CheckFilesystem() override {
      FactoryfsDevice::CheckFilesystem();
      return ZX_ERR_BAD_STATE;
    }
  };
  BlockDeviceManager manager(TestOptions());
  MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
  EXPECT_OK(manager.AddDevice(gpt_device));
  SealedBlockVerityDevice verity_device;
  EXPECT_OK(manager.AddDevice(verity_device));
  FactoryfsWithInvalidMetadata device;
  EXPECT_EQ(ZX_ERR_BAD_STATE, manager.AddDevice(device));
  EXPECT_TRUE(device.checked());
  EXPECT_FALSE(device.formatted());
  EXPECT_FALSE(device.mounted());
}

// Tests adding factoryfs with valid fasctoryfs magic, as a verified child of a
// block-verity device, and valid metadata.
TEST(AddDeviceTestCase, AddValidFactoryfsDevice) {
  BlockDeviceManager manager(TestOptions());
  MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
  EXPECT_OK(manager.AddDevice(gpt_device));
  SealedBlockVerityDevice verity_device;
  EXPECT_OK(manager.AddDevice(verity_device));
  FactoryfsDevice device;
  EXPECT_OK(manager.AddDevice(device));
  EXPECT_TRUE(device.checked());
  EXPECT_FALSE(device.formatted());
  EXPECT_TRUE(device.mounted());
}

// Tests adding factoryfs with a valid superblock, as a device which is not a
// verified child of a block-verity device.
TEST(AddDeviceTestCase, AddUnverifiedFactoryFsDevice) {
  BlockDeviceManager manager(TestOptions());
  MockBlockDevice gpt_device(MockBlockDevice::GptOptions());
  EXPECT_OK(manager.AddDevice(gpt_device));
  FactoryfsDevice device;
  EXPECT_EQ(manager.AddDevice(device), ZX_ERR_NOT_SUPPORTED);
  EXPECT_FALSE(device.checked());
  EXPECT_FALSE(device.formatted());
  EXPECT_FALSE(device.mounted());
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
}  // namespace devmgr

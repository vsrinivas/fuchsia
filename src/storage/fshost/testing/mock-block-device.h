// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_TESTING_MOCK_BLOCK_DEVICE_H_
#define SRC_STORAGE_FSHOST_TESTING_MOCK_BLOCK_DEVICE_H_

#include <zircon/hw/gpt.h>

#include <optional>

#include <gtest/gtest.h>

#include "src/storage/fshost/block-device-interface.h"
#include "src/storage/fshost/constants.h"
#include "zircon/errors.h"

namespace fshost::testing {

class MockBlockDevice : public BlockDeviceInterface {
 public:
  static const std::string& BaseTopologicalPath() {
    static std::string* path = new std::string("/dev/mock_device/block");
    return *path;
  }

  struct Options {
    static Options Default() { return {}; }

    fs_management::DiskFormat content_format = fs_management::kDiskFormatUnknown;
    std::string_view driver_path;
    std::string topological_path = MockBlockDevice::BaseTopologicalPath();
    std::string partition_name;
    bool is_nand = false;
    bool is_ramdisk = false;
  };

  static Options GptOptions() {
    return {.content_format = fs_management::kDiskFormatGpt, .driver_path = kGPTDriverPath};
  }

  static Options FvmOptions() {
    return {.content_format = fs_management::kDiskFormatFvm, .driver_path = kFVMDriverPath};
  }

  static Options DurableOptions() {
    return {
        .topological_path = MockBlockDevice::BaseTopologicalPath() +
                            "/" GPT_DURABLE_NAME "-004/block/zxcrypt/unsealed/block",
    };
  }
  static Options NandOptions() {
    return {
        .driver_path = kNandBrokerDriverPath,
        .is_nand = true,
    };
  }

  explicit MockBlockDevice(const Options& options = Options::Default())
      : options_(options), partition_name_(options_.partition_name) {}

  // Returns the value SetPartitionMaxSize() was called with. Will be a nullopt if uncalled.
  const std::optional<uint64_t>& max_size() const { return max_size_; }

  zx::result<std::unique_ptr<BlockDeviceInterface>> OpenBlockDevice(
      const char* topological_path) const override {
    return zx::error(ZX_ERR_INTERNAL);
  }
  zx::result<std::unique_ptr<BlockDeviceInterface>> OpenBlockDeviceByFd(
      fbl::unique_fd fd) const override {
    return zx::error(ZX_ERR_INTERNAL);
  }

  void AddData(Copier) override {}
  zx::result<Copier> ExtractData() override { return zx::error(ZX_ERR_NOT_SUPPORTED); }

  fs_management::DiskFormat content_format() const override { return options_.content_format; }
  const std::string& topological_path() const override { return options_.topological_path; }
  const std::string& partition_name() const override { return partition_name_; }
  fs_management::DiskFormat GetFormat() final { return format_; }
  void SetFormat(fs_management::DiskFormat format) final { format_ = format; }
  zx::result<fuchsia_hardware_block::wire::BlockInfo> GetInfo() const override {
    fuchsia_hardware_block::wire::BlockInfo info = {};
    info.flags = 0;
    info.block_size = 512;
    info.block_count = 1024;
    return zx::ok(info);
  }
  const fuchsia_hardware_block_partition::wire::Guid& GetInstanceGuid() const override {
    ADD_FAILURE() << "Test should not invoke function " << __FUNCTION__;
    static fuchsia_hardware_block_partition::wire::Guid null_guid{};
    return null_guid;
  }
  const fuchsia_hardware_block_partition::wire::Guid& GetTypeGuid() const override {
    ADD_FAILURE() << "Test should not invoke function " << __FUNCTION__;
    static fuchsia_hardware_block_partition::wire::Guid null_guid;
    return null_guid;
  }
  zx_status_t AttachDriver(const std::string_view& driver) override {
    EXPECT_EQ(driver, options_.driver_path);
    EXPECT_FALSE(attached_);
    attached_ = true;
    return ZX_OK;
  }
  zx_status_t UnsealZxcrypt() override {
    ADD_FAILURE() << "Test should not invoke function " << __FUNCTION__;
    return ZX_ERR_INTERNAL;
  }
  zx_status_t FormatZxcrypt() override {
    ADD_FAILURE() << "Test should not invoke function " << __FUNCTION__;
    return ZX_ERR_INTERNAL;
  }
  bool ShouldCheckFilesystems() override {
    ADD_FAILURE() << "Test should not invoke function " << __FUNCTION__;
    return false;
  }
  zx_status_t CheckFilesystem() override {
    ADD_FAILURE() << "Test should not invoke function " << __FUNCTION__;
    return ZX_ERR_INTERNAL;
  }
  zx_status_t FormatFilesystem() override {
    ADD_FAILURE() << "Test should not invoke function " << __FUNCTION__;
    return ZX_ERR_INTERNAL;
  }
  zx_status_t MountFilesystem() override {
    ADD_FAILURE() << "Test should not invoke function " << __FUNCTION__;
    return ZX_ERR_INTERNAL;
  }
  zx::result<std::string> VeritySeal() override {
    ADD_FAILURE() << "Test should not invoke function " << __FUNCTION__;
    return zx::error(ZX_ERR_INTERNAL);
  }
  zx_status_t OpenBlockVerityForVerifiedRead(std::string seal_hex) override {
    ADD_FAILURE() << "Test should not invoke function " << __FUNCTION__;
    return ZX_ERR_INTERNAL;
  }
  bool ShouldAllowAuthoringFactory() override {
    ADD_FAILURE() << "Test should not invoke function " << __FUNCTION__;
    return false;
  }
  zx_status_t SetPartitionMaxSize(const std::string& fvm_path, uint64_t max_size) override {
    max_size_ = max_size;
    return ZX_OK;
  }
  zx_status_t SetPartitionName(const std::string& fvm_path, std::string_view name) override {
    partition_name_ = name;
    return ZX_OK;
  }
  bool IsNand() const override { return options_.is_nand; }
  bool IsRamDisk() const override { return options_.is_ramdisk; }

  bool attached() const { return attached_; }

 private:
  const Options options_;
  fs_management::DiskFormat format_ = fs_management::kDiskFormatUnknown;
  bool attached_ = false;

  std::optional<uint64_t> max_size_;
  std::string partition_name_;
};

class MockBlockVerityDevice : public MockBlockDevice {
 public:
  static Options VerityOptions() {
    return Options{.driver_path = kBlockVerityDriverPath,
                   .topological_path = BaseTopologicalPath() + "/factory-001/block",
                   .partition_name = "factory"};
  }

  MockBlockVerityDevice(bool allow_authoring, const Options& options = VerityOptions())
      : MockBlockDevice(options), allow_authoring_(allow_authoring) {}
  const fuchsia_hardware_block_partition::wire::Guid& GetTypeGuid() const final {
    static fuchsia_hardware_block_partition::wire::Guid guid = GPT_FACTORY_TYPE_GUID;
    return guid;
  }
  bool ShouldAllowAuthoringFactory() override { return allow_authoring_; }

 private:
  const bool allow_authoring_;
};

static constexpr char kFakeSeal[] =
    "0000000000000000000000000000000000000000000000000000000000000000";

class MockSealedBlockVerityDevice : public MockBlockVerityDevice {
 public:
  MockSealedBlockVerityDevice() : MockBlockVerityDevice(/*allow_authoring=*/false) {}

  zx::result<std::string> VeritySeal() final { return zx::ok(std::string(kFakeSeal)); }
  zx_status_t OpenBlockVerityForVerifiedRead(std::string seal_hex) final {
    EXPECT_EQ(std::string_view(kFakeSeal), seal_hex);
    opened_ = true;
    return ZX_OK;
  }
  bool opened() const { return opened_; }

 private:
  bool opened_ = false;
};

class MockFactoryfsDevice : public MockBlockDevice {
 public:
  static Options FactoryfsOptions() {
    return Options{
        .topological_path = BaseTopologicalPath() + "/factory-001/block/verity/verified/block",
    };
  }

  MockFactoryfsDevice() : MockBlockDevice(FactoryfsOptions()) {}

  const fuchsia_hardware_block_partition::wire::Guid& GetTypeGuid() const final {
    static fuchsia_hardware_block_partition::wire::Guid guid = GPT_FACTORY_TYPE_GUID;
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

class MockBlobfsDevice : public MockBlockDevice {
 public:
  static Options BlobfsOptions() {
    return {
        .topological_path = MockBlockDevice::BaseTopologicalPath() + "/fvm/blobfs-p-1/block",
        .partition_name = std::string(kBlobfsPartitionLabel),
    };
  }

  MockBlobfsDevice() : MockBlockDevice(BlobfsOptions()) {}

  const fuchsia_hardware_block_partition::wire::Guid& GetTypeGuid() const override {
    static fuchsia_hardware_block_partition::wire::Guid guid = GUID_BLOB_VALUE;
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

class MockZxcryptDevice : public MockBlockDevice {
 public:
  static Options ZxcryptOptions() {
    return {
        .content_format = fs_management::kDiskFormatZxcrypt,
        .driver_path = kZxcryptDriverPath,
        .topological_path = MockBlockDevice::BaseTopologicalPath() + "/fvm/minfs-p-2/block",
        .partition_name = std::string(kDataPartitionLabel),
    };
  }

  MockZxcryptDevice(const Options& options = ZxcryptOptions()) : MockBlockDevice(options) {}

  const fuchsia_hardware_block_partition::wire::Guid& GetTypeGuid() const override {
    static fuchsia_hardware_block_partition::wire::Guid guid = GUID_DATA_VALUE;
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

class MockMinfsDevice : public MockBlockDevice {
 public:
  static Options MinfsOptions() {
    return {
        .topological_path =
            MockBlockDevice::BaseTopologicalPath() + "/fvm/minfs-p-2/block/zxcrypt/unsealed/block",
    };
  }

  MockMinfsDevice(Options options = MinfsOptions()) : MockBlockDevice(options) {}

  const fuchsia_hardware_block_partition::wire::Guid& GetTypeGuid() const final {
    static fuchsia_hardware_block_partition::wire::Guid guid = GUID_DATA_VALUE;
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

}  // namespace fshost::testing

#endif  // SRC_STORAGE_FSHOST_TESTING_MOCK_BLOCK_DEVICE_H_

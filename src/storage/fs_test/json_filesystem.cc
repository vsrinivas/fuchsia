// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fs_test/json_filesystem.h"

namespace fs_test {

bool GetBoolOrDefault(const rapidjson::Document& config, const char* member, bool default_value) {
  auto iter = config.FindMember(member);
  return iter == config.MemberEnd() ? default_value : iter->value.GetBool();
}

zx::status<std::unique_ptr<JsonFilesystem>> JsonFilesystem::NewFilesystem(
    const rapidjson::Document& config) {
  auto name = config["name"].GetString();

  disk_format_t format = fs_management::CustomDiskFormat::Register(
      std::make_unique<fs_management::CustomDiskFormat>(name, config["binary_path"].GetString()));

  return zx::ok(std::make_unique<JsonFilesystem>(
      Traits{
          .name = config["name"].GetString(),
          .can_unmount = true,
          .timestamp_granularity = zx::nsec(config["timestamp_granularity"].GetInt64()),
          .supports_hard_links = GetBoolOrDefault(config, "supports_hard_links", false),
          .supports_mmap = GetBoolOrDefault(config, "supports_mmap", false),
          .supports_resize = GetBoolOrDefault(config, "supports_resize", false),
          .max_file_size = config["max_file_size"].GetInt64(),
          .in_memory = false,
          .is_case_sensitive = true,
          .supports_sparse_files = true,
          .supports_fsck_after_every_transaction = false,
      },
      format));
}

class JsonInstance : public FilesystemInstance {
 public:
  JsonInstance(disk_format_t format, RamDevice device, std::string device_path)
      : format_(format), device_(std::move(device)), device_path_(std::move(device_path)) {}

  virtual zx::status<> Format(const TestFilesystemOptions& options) override {
    return FsFormat(device_path_, format_, default_mkfs_options);
  }

  zx::status<> Mount(const std::string& mount_path, const mount_options_t& options) override {
    // For now, don't support admin.  TODO(csuter): figure out why this doesn't work. fs-management
    // seems to not support the new layout.
    mount_options_t new_options = options;
    new_options.admin = false;
    return FsMount(device_path_, mount_path, format_, new_options, &outgoing_directory_);
  }

  zx::status<> Unmount(const std::string& mount_path) override {
    zx::status<> status = FsAdminUnmount(mount_path, outgoing_directory_);
    if (status.is_error()) {
      return status;
    }
    outgoing_directory_.reset();
    return zx::ok();
  }

  zx::status<> Fsck() override {
    fsck_options_t options{
        .verbose = false,
        .never_modify = true,
        .always_modify = false,
        .force = true,
    };
    return zx::make_status(fsck(device_path_.c_str(), format_, &options, launch_stdio_sync));
  }

  zx::status<std::string> DevicePath() const override { return zx::ok(std::string(device_path_)); }

  storage::RamDisk* GetRamDisk() override { return std::get_if<storage::RamDisk>(&device_); }

  ramdevice_client::RamNand* GetRamNand() override {
    return std::get_if<ramdevice_client::RamNand>(&device_);
  }

 private:
  disk_format_t format_;
  RamDevice device_;
  std::string device_path_;
  zx::channel outgoing_directory_;
};

std::unique_ptr<FilesystemInstance> JsonFilesystem::Create(RamDevice device,
                                                           std::string device_path) const {
  return std::make_unique<JsonInstance>(format_, std::move(device), std::move(device_path));
}

zx::status<std::unique_ptr<FilesystemInstance>> JsonFilesystem::Open(
    const TestFilesystemOptions& options) const {
  auto result = OpenRamDevice(options);
  if (result.is_error()) {
    return result.take_error();
  }
  auto [ram_device, device_path] = std::move(result).value();
  return zx::ok(
      std::make_unique<JsonInstance>(format_, std::move(ram_device), std::move(device_path)));
}

}  // namespace fs_test

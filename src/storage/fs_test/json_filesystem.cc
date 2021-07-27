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

  auto iter = config.FindMember("binary_path");
  disk_format_t format;
  if (iter == config.MemberEnd()) {
    format = static_cast<disk_format_t>(config["disk_format"].GetInt64());
  } else {
    format = fs_management::CustomDiskFormat::Register(
        std::make_unique<fs_management::CustomDiskFormat>(name, config["binary_path"].GetString()));
  }
  iter = config.FindMember("sectors_per_cluster");
  const int sectors_per_cluster = iter == config.MemberEnd() ? 0 : iter->value.GetInt64();
  return zx::ok(std::make_unique<JsonFilesystem>(
      Traits{
          .name = config["name"].GetString(),
          .timestamp_granularity = zx::nsec(config["timestamp_granularity"].GetInt64()),
          .supports_hard_links = GetBoolOrDefault(config, "supports_hard_links", false),
          .supports_mmap = GetBoolOrDefault(config, "supports_mmap", false),
          .supports_resize = GetBoolOrDefault(config, "supports_resize", false),
          .max_file_size = config["max_file_size"].GetInt64(),
          .in_memory = GetBoolOrDefault(config, "in_memory", false),
          .is_case_sensitive = GetBoolOrDefault(config, "is_case_sensitive", true),
          .supports_sparse_files = GetBoolOrDefault(config, "supports_sparse_files", true),
          .is_slow = GetBoolOrDefault(config, "is_slow", false),
          .supports_fsck_after_every_transaction =
              GetBoolOrDefault(config, "supports_fsck_after_every_transaction", false),
          .has_directory_size_limit = GetBoolOrDefault(config, "has_directory_size_limit", false),
          .is_journaled = GetBoolOrDefault(config, "is_journaled", true),
          .supports_fs_query = GetBoolOrDefault(config, "supports_fs_query", true),
          .supports_watch_event_deleted =
              GetBoolOrDefault(config, "supports_watch_event_deleted", true),
      },
      format, GetBoolOrDefault(config, "use_directory_admin_to_unmount", false),
      sectors_per_cluster));
}

class JsonInstance : public FilesystemInstance {
 public:
  JsonInstance(const JsonFilesystem* filesystem, RamDevice device, std::string device_path)
      : filesystem_(*filesystem),
        device_(std::move(device)),
        device_path_(std::move(device_path)) {}

  virtual zx::status<> Format(const TestFilesystemOptions& options) override {
    MkfsOptions mkfs_options;
    mkfs_options.sectors_per_cluster = filesystem_.sectors_per_cluster();
    return FsFormat(device_path_, filesystem_.format(), mkfs_options);
  }

  zx::status<> Mount(const std::string& mount_path, const MountOptions& options) override {
    MountOptions new_options = options;
    new_options.admin = filesystem_.use_directory_admin_to_unmount();
    return FsMount(device_path_, mount_path, filesystem_.format(), new_options,
                   &outgoing_directory_);
  }

  zx::status<> Unmount(const std::string& mount_path) override {
    if (filesystem_.use_directory_admin_to_unmount()) {
      return FilesystemInstance::Unmount(mount_path);
    } else {
      zx::status<> status = FsAdminUnmount(mount_path, outgoing_directory_);
      if (status.is_error()) {
        return status;
      }
      outgoing_directory_.reset();
      return zx::ok();
    }
  }

  zx::status<> Fsck() override {
    FsckOptions options{
        .verbose = false,
        .never_modify = true,
        .always_modify = false,
        .force = true,
    };
    return zx::make_status(
        fsck(device_path_.c_str(), filesystem_.format(), options, launch_stdio_sync));
  }

  zx::status<std::string> DevicePath() const override { return zx::ok(std::string(device_path_)); }

  storage::RamDisk* GetRamDisk() override { return std::get_if<storage::RamDisk>(&device_); }

  ramdevice_client::RamNand* GetRamNand() override {
    return std::get_if<ramdevice_client::RamNand>(&device_);
  }

  zx::unowned_channel GetOutgoingDirectory() const override { return outgoing_directory_.borrow(); }

 private:
  const JsonFilesystem& filesystem_;
  RamDevice device_;
  std::string device_path_;
  zx::channel outgoing_directory_;
};

std::unique_ptr<FilesystemInstance> JsonFilesystem::Create(RamDevice device,
                                                           std::string device_path) const {
  return std::make_unique<JsonInstance>(this, std::move(device), std::move(device_path));
}

zx::status<std::unique_ptr<FilesystemInstance>> JsonFilesystem::Open(
    const TestFilesystemOptions& options) const {
  auto result = OpenRamDevice(options);
  if (result.is_error()) {
    return result.take_error();
  }
  auto [ram_device, device_path] = std::move(result).value();
  return zx::ok(
      std::make_unique<JsonInstance>(this, std::move(ram_device), std::move(device_path)));
}

}  // namespace fs_test

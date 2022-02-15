// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fs_test/json_filesystem.h"

namespace fs_test {

zx::status<std::unique_ptr<JsonFilesystem>> JsonFilesystem::NewFilesystem(
    const rapidjson::Document& config) {
  auto name = config["name"].GetString();

  auto iter = config.FindMember("binary_path");
  fs_management::DiskFormat format;
  if (iter == config.MemberEnd()) {
    format = static_cast<fs_management::DiskFormat>(config["disk_format"].GetInt64());
  } else {
    format = fs_management::CustomDiskFormat::Register(
        std::make_unique<fs_management::CustomDiskFormat>(name, config["binary_path"].GetString()));
  }
  iter = config.FindMember("sectors_per_cluster");
  const int sectors_per_cluster = iter == config.MemberEnd() ? 0 : iter->value.GetInt();
  return zx::ok(std::make_unique<JsonFilesystem>(
      Traits{
          .name = config["name"].GetString(),
          .timestamp_granularity = zx::nsec(config["timestamp_granularity"].GetInt64()),
          .supports_hard_links = ConfigGetOrDefault<bool>(config, "supports_hard_links", false),
          .supports_mmap = ConfigGetOrDefault<bool>(config, "supports_mmap", false),
          .supports_mmap_shared_write =
              ConfigGetOrDefault<bool>(config, "supports_mmap_shared_write", false),
          .supports_resize = ConfigGetOrDefault<bool>(config, "supports_resize", false),
          .max_file_size = ConfigGetOrDefault<int64_t>(config, "max_file_size",
                                                       std::numeric_limits<int64_t>::max()),
          .in_memory = ConfigGetOrDefault<bool>(config, "in_memory", false),
          .is_case_sensitive = ConfigGetOrDefault<bool>(config, "is_case_sensitive", true),
          .supports_sparse_files = ConfigGetOrDefault<bool>(config, "supports_sparse_files", true),
          .is_slow = ConfigGetOrDefault<bool>(config, "is_slow", false),
          .supports_fsck_after_every_transaction =
              ConfigGetOrDefault<bool>(config, "supports_fsck_after_every_transaction", false),
          .has_directory_size_limit =
              ConfigGetOrDefault<bool>(config, "has_directory_size_limit", false),
          .is_journaled = ConfigGetOrDefault<bool>(config, "is_journaled", true),
          .supports_watch_event_deleted =
              ConfigGetOrDefault<bool>(config, "supports_watch_event_deleted", true),
          .supports_inspect = ConfigGetOrDefault<bool>(config, "supports_inspect", false),
      },
      format, sectors_per_cluster));
}

class JsonInstance : public FilesystemInstance {
 public:
  JsonInstance(const JsonFilesystem* filesystem, RamDevice device, std::string device_path)
      : filesystem_(*filesystem),
        device_(std::move(device)),
        device_path_(std::move(device_path)) {}

  virtual zx::status<> Format(const TestFilesystemOptions& options) override {
    fs_management::MkfsOptions mkfs_options;
    mkfs_options.sectors_per_cluster = filesystem_.sectors_per_cluster();
    return FsFormat(device_path_, filesystem_.format(), mkfs_options);
  }

  zx::status<> Mount(const std::string& mount_path,
                     const fs_management::MountOptions& options) override {
    return FsMount(device_path_, mount_path, filesystem_.format(), options, &outgoing_directory_);
  }

  zx::status<> Fsck() override {
    fs_management::FsckOptions options{
        .verbose = false,
        .never_modify = true,
        .always_modify = false,
        .force = true,
    };
    return zx::make_status(fs_management::Fsck(device_path_.c_str(), filesystem_.format(), options,
                                               launch_stdio_sync));
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

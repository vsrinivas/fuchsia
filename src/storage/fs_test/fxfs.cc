// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fs_test/fxfs.h"

namespace fs_test {

TestFilesystemOptions DefaultFxfsTestOptions() {
  return TestFilesystemOptions{.description = "Fxfs",
                               .use_fvm = false,
                               .device_block_size = 512,
                               .device_block_count = 131'072,
                               .filesystem = &FxfsFilesystem::SharedInstance()};
}

class FxfsInstance : public FilesystemInstance {
 public:
  FxfsInstance(RamDevice device, std::string device_path)
      : device_(std::move(device)), device_path_(std::move(device_path)) {}

  virtual zx::status<> Format(const TestFilesystemOptions& options) override {
    return FsFormat(device_path_, DISK_FORMAT_FXFS, default_mkfs_options);
  }

  zx::status<> Mount(const std::string& mount_path, const mount_options_t& options) override {
    // For now, don't support admin.  TODO(csuter): figure out why this doesn't work. fs-management
    // seems to not support the new layout.
    mount_options_t new_options = options;
    new_options.admin = false;
    return FsMount(device_path_, mount_path, DISK_FORMAT_FXFS, new_options, &outgoing_directory_);
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
    return zx::make_status(
        fsck(device_path_.c_str(), DISK_FORMAT_FXFS, &options, launch_stdio_sync));
  }

  zx::status<std::string> DevicePath() const override { return zx::ok(std::string(device_path_)); }

  storage::RamDisk* GetRamDisk() override { return std::get_if<storage::RamDisk>(&device_); }

  ramdevice_client::RamNand* GetRamNand() override {
    return std::get_if<ramdevice_client::RamNand>(&device_);
  }

 private:
  RamDevice device_;
  std::string device_path_;
  zx::channel outgoing_directory_;
};

std::unique_ptr<FilesystemInstance> FxfsFilesystem::Create(RamDevice device,
                                                           std::string device_path) const {
  return std::make_unique<FxfsInstance>(std::move(device), std::move(device_path));
}

zx::status<std::unique_ptr<FilesystemInstance>> FxfsFilesystem::Open(
    const TestFilesystemOptions& options) const {
  auto result = OpenRamDevice(options);
  if (result.is_error()) {
    return result.take_error();
  }
  auto [ram_device, device_path] = std::move(result).value();
  return zx::ok(std::make_unique<FxfsInstance>(std::move(ram_device), std::move(device_path)));
}

}  // namespace fs_test

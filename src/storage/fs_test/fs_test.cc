// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fs_test.h"

#include <lib/syslog/cpp/macros.h>

#include <fbl/unique_fd.h>
#include <fs-management/admin.h>
#include <fs-management/format.h>
#include <fs-management/launch.h>
#include <fs-management/mount.h>

#include "src/lib/isolated_devmgr/v2_component/fvm.h"

namespace fs_test {

zx::status<> MinfsFileSystem::Format(const std::string& device_path) const {
  auto status = zx::make_status(
      mkfs(device_path.c_str(), DISK_FORMAT_MINFS, launch_stdio_sync, &default_mkfs_options));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Could not format minfs file system" << status.status_string();
    return status;
  }
  return zx::ok();
}

zx::status<> MinfsFileSystem::Mount(const std::string& device_path,
                                    const std::string& mount_path) const {
  auto fd = fbl::unique_fd(open(device_path.c_str(), O_RDWR));
  if (!fd) {
    FX_LOGS(ERROR) << "Could not open device: " << device_path << ": errno=" << errno;
    return zx::error(ZX_ERR_BAD_STATE);
  }

  // fd consumed by mount. By default, mount waits until the filesystem is ready to accept commands.
  mount_options_t options = default_mount_options;
  options.register_fs = false;
  // Uncomment the following line to force an fsck at the end of every transaction (where
  // supported).
  // options.fsck_after_every_transaction = true;
  auto status = zx::make_status(
      mount(fd.release(), mount_path.c_str(), DISK_FORMAT_MINFS, &options, launch_stdio_async));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Could not mount minfs file system: " << status.status_string();
    return status;
  }
  return zx::ok();
}

zx::status<TestFileSystem> TestFileSystem::Create(const Options& options) {
  // Create a ram-disk.
  auto ram_disk_or =
      isolated_devmgr::RamDisk::Create(options.device_block_size, options.device_block_count);
  if (ram_disk_or.is_error()) {
    return ram_disk_or.take_error();
  }

  // Create an FVM partition if requested.
  std::string device_path;
  if (options.use_fvm) {
    auto fvm_partition_or =
        isolated_devmgr::CreateFvmPartition(ram_disk_or.value().path(), options.fvm_slice_size);
    if (fvm_partition_or.is_error()) {
      return fvm_partition_or.take_error();
    }
    device_path = fvm_partition_or.value();
  } else {
    device_path = ram_disk_or.value().path();
  }

  // Format a file system.
  auto status = options.file_system->Format(device_path);
  if (status.is_error()) {
    return status.take_error();
  }

  // Mount the file system.
  char mount_path_c_str[] = "/tmp/fs_test.XXXXXX";
  if (mkdtemp(mount_path_c_str) == nullptr) {
    FX_LOGS(ERROR) << "Unable to create mount point: " << errno;
    return zx::error(ZX_ERR_BAD_STATE);
  }
  std::string mount_path(mount_path_c_str);
  status = options.file_system->Mount(device_path, mount_path);
  if (status.is_error()) {
    return status.take_error();
  }

  return zx::ok(TestFileSystem(std::move(ram_disk_or).value(), mount_path));
}

TestFileSystem::~TestFileSystem() {
  zx_status_t status = umount(mount_path_.c_str());
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to unmount";
  }
  rmdir(mount_path_.c_str());
}

}  // namespace fs_test

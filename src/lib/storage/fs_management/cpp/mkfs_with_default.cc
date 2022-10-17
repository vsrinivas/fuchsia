// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/fs_management/cpp/mkfs_with_default.h"

#include <fcntl.h>

#include <iostream>

#include <fbl/unique_fd.h>

#include "src/lib/storage/fs_management/cpp/admin.h"
#include "src/lib/storage/fs_management/cpp/mount.h"

namespace fs_management {

zx::result<> MkfsWithDefault(const char* device_path, DiskFormat df, LaunchCallback cb,
                             const MkfsOptions& options, zx::channel crypt_client) {
  auto status = zx::make_result(Mkfs(device_path, df, cb, options));
  if (status.is_error())
    return status.take_error();

  MountOptions mount_options = {
      .component_child_name = options.component_child_name,
      .component_collection_name = options.component_collection_name,
      .component_url = options.component_url,
  };

  auto fs = MountMultiVolume(fbl::unique_fd(open(device_path, O_RDWR)), df, mount_options,
                             fs_management::LaunchStdioAsync);
  if (fs.is_error()) {
    std::cerr << "Could not mount to create default volume: " << fs.status_string() << std::endl;
    return fs.take_error();
  }
  auto volume = fs->CreateVolume("default", std::move(crypt_client));
  if (volume.is_error()) {
    std::cerr << "Failed to create default volume: " << volume.status_string() << std::endl;
    return volume.take_error();
  }
  return zx::ok();
}

}  // namespace fs_management

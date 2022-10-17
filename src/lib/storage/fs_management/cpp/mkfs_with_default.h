// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_MKFS_WITH_DEFAULT_H_
#define SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_MKFS_WITH_DEFAULT_H_

#include <lib/zx/channel.h>
#include <lib/zx/status.h>

#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/fs_management/cpp/launch.h"
#include "src/lib/storage/fs_management/cpp/options.h"

namespace fs_management {

// Like Mkfs but creates a "default" volume (which will only work on multi-volume filesystems)
// and will be encrypted using `crypt_client`, if set.
// This should only be used for testing.
zx::result<> MkfsWithDefault(const char* device_path, DiskFormat df, LaunchCallback cb,
                             const MkfsOptions& options, zx::channel crypt_client);

}  // namespace fs_management

#endif  // SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_MKFS_WITH_DEFAULT_H_

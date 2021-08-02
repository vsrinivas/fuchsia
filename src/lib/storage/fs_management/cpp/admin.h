// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_ADMIN_H_
#define SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_ADMIN_H_

#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <zircon/types.h>

#include <fs-management/admin.h>
#include <fs-management/format.h>

namespace fs_management {

struct OutgoingDirectory {
  zx::unowned_channel client;
  zx::channel server;
};

// Given the handle to the outgoing directory for the filesystem, returns the handle for the root
// directory. |flags| are passed to the Directory/Open call.
zx::status<zx::channel> GetFsRootHandle(zx::unowned_channel export_root, uint32_t flags);

// Launches the specified filesystem.
zx::status<> FsInit(zx::channel device, disk_format_t df, const InitOptions& options,
                    OutgoingDirectory outgoing_directory, zx::channel crypt_client);

}  // namespace fs_management

#endif  // SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_ADMIN_H_

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_ADMIN_H_
#define SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_ADMIN_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fidl/llcpp/channel.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/fs_management/cpp/launch.h"
#include "src/lib/storage/fs_management/cpp/options.h"

namespace fs_management {

inline constexpr std::string_view kPathData = "/data";
inline constexpr std::string_view kPathInstall = "/install";
inline constexpr std::string_view kPathDurable = "/durable";
inline constexpr std::string_view kPathSystem = "/system";
inline constexpr std::string_view kPathBlob = "/blob";
inline constexpr std::string_view kPathFactory = "/factory";
inline constexpr std::string_view kPathVolume = "/volume";
inline constexpr std::string_view kPathDevBlock = "/dev/class/block";

// Format the provided device with a requested disk format.
zx_status_t Mkfs(const char* device_path, DiskFormat df, LaunchCallback cb,
                 const MkfsOptions& options);

// Check and repair a device with a requested disk format.
zx_status_t Fsck(std::string_view device_path, DiskFormat df, const FsckOptions& options,
                 LaunchCallback cb);

// Get a connection to the root of the filesystem, given a filesystem outgoing directory.
zx::status<fidl::ClientEnd<fuchsia_io::Directory>> FsRootHandle(
    fidl::UnownedClientEnd<fuchsia_io::Directory> export_root,
    uint32_t flags = fuchsia_io::wire::kOpenRightReadable |
                     fuchsia_io::wire::kOpenFlagPosixWritable |
                     fuchsia_io::wire::kOpenFlagPosixExecutable);

}  // namespace fs_management

#endif  // SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_ADMIN_H_

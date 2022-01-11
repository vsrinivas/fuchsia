// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_MOUNT_H_
#define SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_MOUNT_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fidl/llcpp/channel.h>
#include <zircon/compiler.h>

#include <fbl/unique_fd.h>

#include "src/lib/storage/fs_management/cpp/admin.h"
#include "src/lib/storage/fs_management/cpp/launch.h"

namespace fs_management {

struct MountOptions {
  bool readonly = false;
  bool verbose_mount = false;
  bool collect_metrics = false;

  // Ensures that requests to the mountpoint will be propagated to the underlying FS
  bool wait_until_ready = true;

  // An optional compression algorithm specifier for the filesystem to use when storing files (if
  // the filesystem supports it).
  const char* write_compression_algorithm = nullptr;

  // An optional cache eviction policy specifier for the filesystem to use for in-memory data (if
  // the filesystem supports it).
  const char* cache_eviction_policy = nullptr;

  // If set, run fsck after every transaction.
  bool fsck_after_every_transaction = false;

  // If true, puts decompression in a sandboxed process.
  bool sandbox_decompression = false;

  // If set, handle to the crypt client. The handle is *always* consumed, even on error.
  zx_handle_t crypt_client = ZX_HANDLE_INVALID;
};

class __EXPORT MountedFilesystem {
 public:
  MountedFilesystem(fidl::ClientEnd<fuchsia_io::Directory> export_root, std::string_view mount_path)
      : export_root_(std::move(export_root)), mount_path_(mount_path) {}
  MountedFilesystem(MountedFilesystem&&) = default;

  ~MountedFilesystem();

  const fidl::ClientEnd<fuchsia_io::Directory>& export_root() const { return export_root_; }
  const std::string& mount_path() const { return mount_path_; }

  zx::status<> Unmount() && { return UnmountImpl(); }
  fidl::ClientEnd<fuchsia_io::Directory> TakeExportRoot() && { return std::move(export_root_); }

 private:
  zx::status<> UnmountImpl();

  fidl::ClientEnd<fuchsia_io::Directory> export_root_;
  std::string mount_path_;
};

// Mounts a filesystem.
//
//   device_fd  : the device containing the filesystem.
//   mount_path : an optional path where the root will be bound into the local namespace.
//   df         : the format of the filesystem.
//   options    : mount options.
//   cb         : a callback used to actually launch the binary. This can be one of the
//                functions declared in launch.h.
zx::status<MountedFilesystem> Mount(fbl::unique_fd device_fd, const char* mount_path, DiskFormat df,
                                    const MountOptions& options, LaunchCallback cb);

// Shuts down a filesystem (using fuchsia.fs/Admin).
__EXPORT
zx::status<> Shutdown(fidl::UnownedClientEnd<fuchsia_io::Directory> export_root);

}  // namespace fs_management

#endif  // SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_MOUNT_H_

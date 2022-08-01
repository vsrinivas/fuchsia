// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_MOUNT_H_
#define SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_MOUNT_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <zircon/compiler.h>

#include <fbl/unique_fd.h>

#include "src/lib/storage/fs_management/cpp/admin.h"
#include "src/lib/storage/fs_management/cpp/launch.h"
#include "src/lib/storage/fs_management/cpp/options.h"

namespace fs_management {

class __EXPORT MountedFilesystem {
 public:
  MountedFilesystem(fidl::ClientEnd<fuchsia_io::Directory> export_root, std::string_view mount_path)
      : export_root_(std::move(export_root)), mount_path_(mount_path) {}
  MountedFilesystem(MountedFilesystem&&) = default;

  ~MountedFilesystem();

  const fidl::ClientEnd<fuchsia_io::Directory>& export_root() const { return export_root_; }
  const std::string& mount_path() const { return mount_path_; }
  void set_mount_path(std::string_view mount_path) { mount_path_ = std::string(mount_path); }

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
//
// See //src/storage/docs/launching.md for more information.
zx::status<MountedFilesystem> Mount(fbl::unique_fd device_fd, const char* mount_path, DiskFormat df,
                                    const MountOptions& options, LaunchCallback cb);

// Shuts down a filesystem.
//
// This method takes a directory protocol to the service directory and assumes that we
// can find the fuchsia.fs.Admin protocol there.
__EXPORT
zx::status<> Shutdown(fidl::UnownedClientEnd<fuchsia_io::Directory> svc_dir);

}  // namespace fs_management

#endif  // SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_MOUNT_H_

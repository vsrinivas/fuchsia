// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_MOUNT_H_
#define SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_MOUNT_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <zircon/compiler.h>

#include <map>
#include <ostream>
#include <utility>
#include <variant>

#include <fbl/unique_fd.h>

#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/fs_management/cpp/fvm.h"
#include "src/lib/storage/fs_management/cpp/launch.h"
#include "src/lib/storage/fs_management/cpp/options.h"

namespace fs_management {

// RAII wrapper for a binding of a `fuchsia.io.Directory` into the local namespace.
class NamespaceBinding {
 public:
  static zx::result<NamespaceBinding> Create(const char* path,
                                             fidl::ClientEnd<fuchsia_io::Directory> dir);
  NamespaceBinding() = default;
  ~NamespaceBinding();

  NamespaceBinding(const NamespaceBinding&) = delete;
  NamespaceBinding& operator=(const NamespaceBinding&) = delete;
  NamespaceBinding(NamespaceBinding&& o) : path_(std::move(o.path_)) { o.path_.clear(); }
  NamespaceBinding& operator=(NamespaceBinding&& o) {
    path_ = std::move(o.path_);
    o.path_.clear();
    return *this;
  }

  // Unbinds the path from the local namespace and resets the internal state of this object to a
  // default uninitialized state.
  void Reset();

  // Resets this object without unbinding the path from the local namespace.  Returns the path of
  // the binding (if it was set).
  std::string Release();

  std::string_view path() { return path_; }

 private:
  explicit NamespaceBinding(std::string path) : path_(std::move(path)) {}
  std::string path_;
};

// A filesystem with a single logical volume has a few additional pieces of functionality.
class __EXPORT SingleVolumeFilesystemInterface {
 public:
  virtual ~SingleVolumeFilesystemInterface() = 0;

  // Returns a connection to the data root (i.e. the directory which contains user data).
  virtual zx::result<fidl::ClientEnd<fuchsia_io::Directory>> DataRoot() const = 0;

  // Returns the connection to the export root of the filesystem.
  virtual const fidl::ClientEnd<fuchsia_io::Directory>& ExportRoot() const = 0;

  // Unmounts and shuts down the filesystem.  Leaves this object in an indeterminate state.
  virtual zx::result<> Unmount() = 0;
};

/// Manages a started filesystem instance (i.e. one started by [`fuchsia.fs.startup.Start`]).
class __EXPORT StartedSingleVolumeFilesystem : public SingleVolumeFilesystemInterface {
 public:
  StartedSingleVolumeFilesystem() = default;
  explicit StartedSingleVolumeFilesystem(fidl::ClientEnd<fuchsia_io::Directory> export_root)
      : export_root_(std::move(export_root)) {}
  StartedSingleVolumeFilesystem(StartedSingleVolumeFilesystem&& o)
      : export_root_(std::move(o.export_root_)) {
    o.Release();
  }
  StartedSingleVolumeFilesystem& operator=(StartedSingleVolumeFilesystem&& o) {
    export_root_ = std::move(o.export_root_);
    o.Release();
    return *this;
  }
  ~StartedSingleVolumeFilesystem() override;

  // Unmounts and shuts down the filesystem.  Leaves this object in an indeterminate state.
  zx::result<> Unmount() override;

  // Takes the filesystem connection, so the filesystem won't automatically be shut down when this
  // object goes out of scope.  Some filesystems will automatically shut down when the last
  // connection to goes out of scope; others will never shut down.
  fidl::ClientEnd<fuchsia_io::Directory> Release();

  // Returns a connection to the data root (i.e. the directory which contains user data).
  zx::result<fidl::ClientEnd<fuchsia_io::Directory>> DataRoot() const override;

  // Returns the connection to the service directory offered by the filesystem.
  const fidl::ClientEnd<fuchsia_io::Directory>& ExportRoot() const override { return export_root_; }

 private:
  fidl::ClientEnd<fuchsia_io::Directory> export_root_;
};

/// Manages a started volume within a filesystem instance (i.e. one opened or created by
/// [`fuchsia.fxfs.Volumes`]).
class MountedVolume {
 public:
  MountedVolume() = default;
  explicit MountedVolume(fidl::ClientEnd<fuchsia_io::Directory> export_root)
      : export_root_(std::move(export_root)) {}
  MountedVolume(MountedVolume&& o) : export_root_(std::move(o.export_root_)) { o.Release(); }
  MountedVolume& operator=(MountedVolume&& o) {
    export_root_ = std::move(o.export_root_);
    o.Release();
    return *this;
  }
  // Returns a connection to the data root (i.e. the directory which contains user data).
  zx::result<fidl::ClientEnd<fuchsia_io::Directory>> DataRoot() const;

  // Returns the connection to the export root of the volume.
  const fidl::ClientEnd<fuchsia_io::Directory>& ExportRoot() const { return export_root_; }

  // Takes the volume connection, so the volume won't automatically be unmounted when this
  // object goes out of scope.  Some volumes will unmount down when the last connection to goes out
  // of scope; others will never unmount.
  fidl::ClientEnd<fuchsia_io::Directory> Release();

 private:
  fidl::ClientEnd<fuchsia_io::Directory> export_root_;
};

/// Manages a started multi-volume filesystem instance (i.e. one started by
/// [`fuchsia.fs.startup.Start`]).
class __EXPORT StartedMultiVolumeFilesystem {
 public:
  StartedMultiVolumeFilesystem() = default;
  explicit StartedMultiVolumeFilesystem(fidl::ClientEnd<fuchsia_io::Directory> exposed_dir)
      : exposed_dir_(std::move(exposed_dir)) {}
  StartedMultiVolumeFilesystem(StartedMultiVolumeFilesystem&& o)
      : exposed_dir_(std::move(o.exposed_dir_)), volumes_(std::move(o.volumes_)) {
    o.Release();
  }
  StartedMultiVolumeFilesystem& operator=(StartedMultiVolumeFilesystem&& o) {
    exposed_dir_ = std::move(o.exposed_dir_);
    volumes_ = std::move(o.volumes_);
    o.Release();
    return *this;
  }

  ~StartedMultiVolumeFilesystem();

  // Takes the filesystem connection and all volume connections, so the filesystem won't
  // automatically be shut down when this object goes out of scope.  Some filesystems will
  // automatically shut down when the last connection to goes out of scope; others will never shut
  // down.
  std::pair<fidl::ClientEnd<fuchsia_io::Directory>,
            std::map<std::string, fidl::ClientEnd<fuchsia_io::Directory>>>
  Release();

  // Unmounts and shuts down the filesystem.  Leaves this object in an indeterminate state.
  zx::result<> Unmount();

  // Returns the connection to the service directory offered by the filesystem.
  const fidl::ClientEnd<fuchsia_io::Directory>& ServiceDirectory() const { return exposed_dir_; }

  // Opens the volume if present.  |crypt_client| is an optional connection to a crypt service used
  // to unlock the volume; if unset, the volume is assumed to be unencrypted.
  //
  // Returns a pointer to the volume if it was opened.  The lifetime of the pointer is less than
  // this object.
  zx::result<MountedVolume*> OpenVolume(std::string_view name, zx::channel crypt_client);

  // Creates a volume.  |crypt_client| is an optional connection to a crypt service used
  // to unlock the volume; if unset, the volume is assumed to be unencrypted.
  //
  // Returns a pointer to the volume if it was created.  The lifetime of the pointer is less than
  // this object.
  zx::result<MountedVolume*> CreateVolume(std::string_view name, zx::channel crypt_client);

  // Verifies the integrity of a volume.  |crypt_client| is an optional connection to a crypt
  // service used to unlock the volume; if unset, the volume is assumed to be unencrypted.
  zx::result<> CheckVolume(std::string_view name, zx::channel crypt_client);

  // Returns whether the given volume name exists.
  bool HasVolume(std::string_view name);

  // Returns a pointer to the given volume, if it is already open.  The lifetime of the pointer is
  // less than this object.
  __EXPORT const MountedVolume* GetVolume(const std::string& volume) const {
    auto iter = volumes_.find(volume);
    if (iter == volumes_.end()) {
      return nullptr;
    }
    return &iter->second;
  }

 private:
  fidl::ClientEnd<fuchsia_io::Directory> exposed_dir_;
  std::map<std::string, MountedVolume, std::less<>> volumes_;
};

// A special case of a multi-volume filesystem where we only ever operate on one volume.  Implements
// the `SingleVolumeFilessystemInterface` interface.  Useful for testing.
class __EXPORT StartedSingleVolumeMultiVolumeFilesystem : public SingleVolumeFilesystemInterface {
 public:
  StartedSingleVolumeMultiVolumeFilesystem() = default;
  StartedSingleVolumeMultiVolumeFilesystem(fidl::ClientEnd<fuchsia_io::Directory> exposed_dir,
                                           MountedVolume volume)
      : exposed_dir_(std::move(exposed_dir)), volume_(std::move(volume)) {}
  StartedSingleVolumeMultiVolumeFilesystem(StartedSingleVolumeMultiVolumeFilesystem&& o)
      : exposed_dir_(std::move(o.exposed_dir_)), volume_(std::move(o.volume_)) {
    o.Release();
  }
  StartedSingleVolumeMultiVolumeFilesystem& operator=(
      StartedSingleVolumeMultiVolumeFilesystem&& o) {
    exposed_dir_ = std::move(o.exposed_dir_);
    volume_ = std::move(o.volume_);
    o.Release();
    return *this;
  }

  ~StartedSingleVolumeMultiVolumeFilesystem() override;

  // Takes the filesystem connection, so the filesystem won't automatically be shut down when this
  // object goes out of scope.  Some filesystems will automatically shut down when the last
  // connection to goes out of scope; others will never shut down.
  fidl::ClientEnd<fuchsia_io::Directory> Release();

  // Unmounts and shuts down the filesystem.  Leaves this object in an indeterminate state.
  zx::result<> Unmount() override;

  // Returns a connection to the data root (i.e. the directory which contains user data).
  zx::result<fidl::ClientEnd<fuchsia_io::Directory>> DataRoot() const override {
    return volume_ ? volume_->DataRoot() : zx::error(ZX_ERR_BAD_STATE);
  }
  // Returns the connection to the export root of the filesystem.
  const fidl::ClientEnd<fuchsia_io::Directory>& ExportRoot() const override { return exposed_dir_; }

  __EXPORT const std::optional<MountedVolume>& volume() const { return volume_; }

 private:
  fidl::ClientEnd<fuchsia_io::Directory> exposed_dir_;
  std::optional<MountedVolume> volume_;
};

// Mounts a filesystem.
//
//   device_fd  : the device containing the filesystem.
//   df         : the format of the filesystem.
//   options    : mount options.
//   cb         : a callback used to actually launch the binary (which is only used for native
//                filesystems). This can be one of the functions declared in launch.h.
//
// See //src/storage/docs/launching.md for more information.
zx::result<StartedSingleVolumeFilesystem> Mount(fbl::unique_fd device_fd, DiskFormat df,
                                                const MountOptions& options, LaunchCallback cb);

// Mounts a multi-volume filesystem.
//
//   device_fd  : the device containing the filesystem.
//   df         : the format of the filesystem.
//   options    : mount options.
//   cb         : a callback used to actually launch the binary (which is only used for native
//                filesystems). This can be one of the functions declared in launch.h.
//
// See //src/storage/docs/launching.md for more information.
zx::result<StartedMultiVolumeFilesystem> MountMultiVolume(fbl::unique_fd device_fd, DiskFormat df,
                                                          const MountOptions& options,
                                                          LaunchCallback cb);

// Mounts a multi-volume filesystem using a default singular volume.  Generally this is used for
// testing and production use should favour |MountMultiVolume|.
//
//   device_fd   : the device containing the filesystem.
//   df          : the format of the filesystem.
//   options     : mount options.
//   cb          : a callback used to actually launch the binary (which is only used for native
//                 filesystems). This can be one of the functions declared in launch.h.
//   volume_name : the volume to open.
//
// See //src/storage/docs/launching.md for more information.
zx::result<StartedSingleVolumeMultiVolumeFilesystem> MountMultiVolumeWithDefault(
    fbl::unique_fd device_fd, DiskFormat df, const MountOptions& options, LaunchCallback cb,
    const char* volume_name = "default");

// Shuts down a filesystem.
//
// This method takes a directory protocol to the service directory and assumes that we
// can find the fuchsia.fs.Admin protocol there.
zx::result<> Shutdown(fidl::UnownedClientEnd<fuchsia_io::Directory> svc_dir);

}  // namespace fs_management

#endif  // SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_MOUNT_H_

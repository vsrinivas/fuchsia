// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_FILESYSTEM_MOUNTER_H_
#define SRC_STORAGE_FSHOST_FILESYSTEM_MOUNTER_H_

#include <fidl/fuchsia.fs.startup/cpp/wire.h>
#include <fidl/fuchsia.fxfs/cpp/wire.h>
#include <lib/zx/channel.h>
#include <lib/zx/result.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/unique_fd.h>

#include "fidl/fuchsia.fxfs/cpp/markers.h"
#include "fidl/fuchsia.io/cpp/markers.h"
#include "lib/fidl/cpp/wire/internal/transport_channel.h"
#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/lib/storage/fs_management/cpp/options.h"
#include "src/security/fcrypto/bytes.h"
#include "src/storage/fshost/copier.h"
#include "src/storage/fshost/fs-manager.h"
#include "src/storage/fshost/fshost-boot-args.h"
#include "src/storage/fshost/fshost_config.h"
#include "src/storage/fshost/inspect-manager.h"

namespace fshost {

class StartedFilesystem {
 public:
  explicit StartedFilesystem(fs_management::StartedSingleVolumeFilesystem&&);
  explicit StartedFilesystem(fs_management::StartedMultiVolumeFilesystem&&);

  // Detaches from the filesystem, so that when this object goes out of scope it is not shut down.
  void Detach();

  friend class FilesystemMounter;

 private:
  std::variant<fs_management::StartedSingleVolumeFilesystem,
               fs_management::StartedMultiVolumeFilesystem>
      fs_;
};

zx::result<StartedFilesystem> LaunchFilesystem(zx::channel block_device,
                                               const fs_management::MountOptions& options,
                                               fs_management::DiskFormat format);

// FilesystemMounter is a utility class which wraps the FsManager
// and helps clients mount filesystems within the fshost namespace.
class FilesystemMounter {
 public:
  FilesystemMounter(FsManager& fshost, const fshost_config::Config* config)
      : fshost_(fshost), config_(*config) {}

  virtual ~FilesystemMounter() = default;

  bool Netbooting() const { return config_.netboot(); }
  bool ShouldCheckFilesystems() const { return config_.check_filesystems(); }

  // Attempts to mount a block device to "/data".
  // Fails if already mounted.
  // If |copier| is set, its data will be copied into the data filesystem before exposing the
  // filesystem to clients.
  //
  zx_status_t MountData(zx::channel block_device_client, std::optional<Copier> copier,
                        fs_management::MountOptions options, fs_management::DiskFormat format);

  // Attempts to mount a block device to "/durable".
  // Fails if already mounted.
  zx_status_t MountDurable(zx::channel block_device_client,
                           const fs_management::MountOptions& options);

  // Attempts to mount a block device to "/blob".
  // Fails if already mounted.
  zx_status_t MountBlob(zx::channel block_device_client,
                        const fs_management::MountOptions& options);

  // Attempts to mount a block device to "/factory".
  // Fails if already mounted.
  zx_status_t MountFactoryFs(zx::channel block_device_client,
                             const fs_management::MountOptions& options);

  std::shared_ptr<FshostBootArgs> boot_args() { return fshost_.boot_args(); }
  void ReportPartitionCorrupted(fs_management::DiskFormat format);

  bool BlobMounted() const { return blob_mounted_; }
  bool DataMounted() const { return data_mounted_; }
  bool FactoryMounted() const { return factory_mounted_; }
  bool DurableMounted() const { return durable_mounted_; }

  FsManager& manager() { return fshost_; }
  FshostInspectManager& inspect_manager() { return fshost_.inspect_manager(); }

 protected:
  // Routes a given mounted filesystem or volume to /data and updates device path.
  // This can be overridden for testing.
  virtual zx_status_t RouteData(fidl::UnownedClientEnd<fuchsia_io::Directory> export_root,
                                std::string_view device_path);

 private:
  // Mounts a filesystem in the legacy mode (i.e. launching as a process directly).
  // Componentized filesystems should use LaunchFsComponent and the fuchsia.fs.startup.Startup
  // protocol.
  // Performs the mechanical action of mounting a filesystem, without validating the type of
  // filesystem being mounted.
  zx::result<> MountLegacyFilesystem(FsManager::MountPoint point, fs_management::DiskFormat df,
                                     const char* binary_path,
                                     const fs_management::MountOptions& options,
                                     zx::channel block_device) const;

  // Actually launches the filesystem component.  Note that for non-componentized filesystems there
  // is the LaunchFsNative variant which allows control over where the endpoint is bound to.
  //
  // Virtualized to enable testing.
  virtual zx::result<StartedFilesystem> LaunchFs(zx::channel block_device,
                                                 const fs_management::MountOptions& options,
                                                 fs_management::DiskFormat format) const;

  // Actually launches the filesystem.
  //
  // Virtualized to enable testing.
  virtual zx::result<> LaunchFsNative(fidl::ServerEnd<fuchsia_io::Directory> server,
                                      const char* binary, zx::channel block_device,
                                      const fs_management::MountOptions& options) const;

  zx_status_t CopyDataToLegacyFilesystem(fs_management::DiskFormat df, zx::channel device,
                                         const Copier& copier) const;

  FsManager& fshost_;
  const fshost_config::Config& config_;
  bool data_mounted_ = false;
  bool durable_mounted_ = false;
  bool blob_mounted_ = false;
  bool factory_mounted_ = false;
  fidl::ClientEnd<fuchsia_io::Directory> crypt_outgoing_directory_;
};

std::string_view BinaryPathForFormat(fs_management::DiskFormat format);

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_FILESYSTEM_MOUNTER_H_

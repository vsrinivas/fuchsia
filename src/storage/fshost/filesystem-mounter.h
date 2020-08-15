// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_FILESYSTEM_MOUNTER_H_
#define SRC_STORAGE_FSHOST_FILESYSTEM_MOUNTER_H_

#include <lib/zx/channel.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/unique_fd.h>
#include <fs-management/mount.h>

#include "block-watcher.h"
#include "fs-manager.h"
#include "fshost-boot-args.h"
#include "metrics.h"

namespace devmgr {

// FilesystemMounter is a utility class which wraps the FsManager
// and helps clients mount filesystems within the fshost namespace.
class FilesystemMounter {
 public:
  FilesystemMounter(std::unique_ptr<FsManager> fshost, BlockWatcherOptions options)
      : fshost_(std::move(fshost)), options_(options) {}

  virtual ~FilesystemMounter() = default;

  void FuchsiaStart() const { fshost_->FuchsiaStart(); }

  zx_status_t InstallFs(const char* path, zx::channel h) {
    return fshost_->InstallFs(path, std::move(h));
  }

  bool Netbooting() const { return options_.netboot; }
  bool ShouldCheckFilesystems() const { return options_.check_filesystems; }

  // Attempts to mount a block device to "/data".
  // Fails if already mounted.
  zx_status_t MountData(zx::channel block_device_client, const mount_options_t& options);

  // Attempts to mount a block device to "/install".
  // Fails if already mounted.
  zx_status_t MountInstall(zx::channel block_device_client, const mount_options_t& options);

  // Attempts to mount a block device to "/blob".
  // Fails if already mounted.
  zx_status_t MountBlob(zx::channel block_device_client, const mount_options_t& options);

  // Attempts to mount a block device to "/factory".
  // Fails if already mounted.
  zx_status_t MountFactoryFs(zx::channel block_device_client, const mount_options_t& options);

  // Attempts to mount pkgfs if all preconditions have been met:
  // - Pkgfs has not previously been mounted
  // - Blobfs has been mounted
  // - The data partition has been mounted
  void TryMountPkgfs();

  // Returns a pointer to the |FsHostMetrics| instance.
  FsHostMetrics* mutable_metrics() { return fshost_->mutable_metrics(); }

  devmgr::FshostBootArgs* boot_args() { return fshost_->boot_args(); }

  void FlushMetrics() { fshost_->FlushMetrics(); }

  bool BlobMounted() const { return blob_mounted_; }
  bool DataMounted() const { return data_mounted_; }
  bool PkgfsMounted() const { return pkgfs_mounted_; }
  bool FactoryMounted() const { return factory_mounted_; }

 private:
  // Performs the mechanical action of mounting a filesystem, without
  // validating the type of filesystem being mounted.
  zx_status_t MountFilesystem(const char* mount_path, const char* binary,
                              const mount_options_t& options, zx::channel block_device_client,
                              zx::channel diagnostics_dir, uint32_t fs_flags);

  bool WaitForData() const { return options_.wait_for_data; }

  // Actually launches the filesystem process.
  //
  // Virtualized to enable testing.
  virtual zx_status_t LaunchFs(int argc, const char** argv, zx_handle_t* hnd, uint32_t* ids,
                               size_t len, uint32_t fs_flags);

  std::unique_ptr<FsManager> fshost_;
  const BlockWatcherOptions options_;
  bool data_mounted_ = false;
  bool install_mounted_ = false;
  bool blob_mounted_ = false;
  bool pkgfs_mounted_ = false;
  bool factory_mounted_ = false;
};

}  // namespace devmgr

#endif  // SRC_STORAGE_FSHOST_FILESYSTEM_MOUNTER_H_

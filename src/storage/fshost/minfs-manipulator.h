// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_MINFS_MANIPULATOR_H_
#define SRC_STORAGE_FSHOST_MINFS_MANIPULATOR_H_

#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <fidl/fuchsia.io.admin/cpp/wire.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>

#include <cstdint>

#include <fbl/unique_fd.h>

#include "src/storage/fshost/copier.h"
#include "src/storage/fshost/fs-manager.h"

namespace fshost {

constexpr char kGracefulRebootReasonFilePath[] =
    "cache/de3e6f95d17b22bcc6b8bb5389301dc9fa60a16e2c5a4d35eb4a35dd9ac571c3/"
    "graceful_reboot_reason.txt";

// Gets the BlockInfo from |device|.
zx::status<fuchsia_hardware_block::wire::BlockInfo> GetBlockDeviceInfo(
    const zx::unowned_channel& device);

// Parses |excluded_paths| into a list of paths. White space and empty paths are removed.
std::vector<std::filesystem::path> ParseExcludedPaths(std::string_view excluded_paths);

enum class MaybeResizeMinfsResult {
  // Minfs was either not resized or successfully resized and can be mounted as it normally would
  // be.
  kMinfsMountable,
  // Minfs was likely corrupted while resizing and the device should be rebooted to trigger a
  // factory reset.
  kRebootRequired,
};

// For a given block |device| formatted with minfs, resizes minfs if it's not the correct size.
//
// "correct size" is defined as: the size of the minfs partition is less than or equal to
// |partition_size_limit| and the number of inodes in minfs is equal to |required_inodes|.
//
// The resize won't be done if the amount of data in minfs is greater than |data_size_limit| after
// filtering out all of the files and directories that match |excluded_paths|.
//
// This method is slow and may destroy files or corrupt minfs. Not tolerant to power interruptions.
[[nodiscard]] MaybeResizeMinfsResult MaybeResizeMinfs(
    zx::channel device, uint64_t partition_size_limit, uint64_t required_inodes,
    uint64_t data_size_limit, const std::vector<std::filesystem::path>& excluded_paths,
    FsManager& manager);

// RAII wrapper around a mounted minfs that unmounts minfs when destroyed.
class MountedMinfs {
 public:
  MountedMinfs(MountedMinfs&&) = default;
  MountedMinfs(const MountedMinfs&) = delete;
  MountedMinfs& operator=(MountedMinfs&&) = default;
  MountedMinfs& operator=(const MountedMinfs&) = delete;
  ~MountedMinfs();

  // Mounts minfs on the given block |device|.
  static zx::status<MountedMinfs> Mount(zx::channel device);

  // Explicitly unmounts minfs and returns any errors instead of swallowing the errors in the
  // destructor.
  static zx::status<> Unmount(MountedMinfs fs);

  // Calls DirectoryAdmin::QueryFilesystem.
  zx::status<fuchsia_io_admin::wire::FilesystemInfo> GetFilesystemInfo() const;

  // Copies the contents of minfs into ram.
  zx::status<Copier> ReadFilesystem(const std::vector<std::filesystem::path>& excluded_paths) const;

  // Populates minfs with the contents of |copier|.
  zx::status<> PopulateFilesystem(Copier copier) const;

  // Creates a file at the root of minfs to indicate that |PopulateFilesystem| was started.
  zx::status<> SetResizeInProgress() const;
  // Removes the file created by |SetResizeInProgress|.
  zx::status<> ClearResizeInProgress() const;
  // Returns true if the file created by |SetResizeInProgress| exists.
  zx::status<bool> IsResizeInProgress() const;

  // Gets a file descriptor to the root directory of minfs.
  zx::status<fbl::unique_fd> GetRootFd() const;

 private:
  explicit MountedMinfs(zx::channel root);

  zx::status<> Unmount();

  zx::channel root_;
};

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_MINFS_MANIPULATOR_H_

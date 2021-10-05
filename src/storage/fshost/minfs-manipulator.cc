// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/minfs-manipulator.h"

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.encrypted/cpp/wire.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <fidl/fuchsia.io.admin/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/vfs.h>
#include <lib/fidl/llcpp/client_end.h>
#include <lib/fidl/llcpp/wire_messaging.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <cstdint>
#include <string_view>
#include <variant>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <fs-management/admin.h>
#include <fs-management/format.h>
#include <fs-management/launch.h>
#include <fs-management/mount.h>

#include "src/lib/files/file_descriptor.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/storage/fshost/copier.h"
#include "src/storage/minfs/format.h"

namespace fshost {
namespace {

constexpr char kMinfsResizeInProgressFilename[] = "minfs-resize-in-progress";

zx::channel CloneDeviceChannel(const zx::channel& device) {
  return zx::channel(fdio_service_clone(device.get()));
}

zx::status<> FormatMinfs(zx::channel device) {
  const char* argv[] = {"/pkg/bin/minfs", "mkfs", nullptr};
  zx_handle_t handles[] = {device.release()};
  uint32_t ids[] = {FS_HANDLE_BLOCK_DEVICE_ID};
  return zx::make_status(launch_stdio_sync(/*argc=*/2, argv, handles, ids, /*len=*/1));
}

zx::status<> ShredZxcrypt(const zx::unowned_channel& device) {
  fidl::UnownedClientEnd<fuchsia_device::Controller> controller_client(device);
  auto x = fidl::WireCall(controller_client).GetTopologicalPath();
  if (x.status() != ZX_OK) {
    return zx::error(x.status());
  }
  if (x->result.is_err()) {
    return zx::error(x->result.err());
  }

  std::filesystem::path path(x->result.response().path.get());
  // |path| should look like
  // "/dev/<device-drivers>/block/fvm/<data-partition>/block/zxcrypt/unsealed/block" and the zxcrypt
  // DeviceManager will be served from the zxcrypt directory.
  if (path.filename() != "block") {
    FX_LOGS(ERROR) << "Failed to find zxcrypt in: " << path;
    return zx::error(ZX_ERR_BAD_STATE);
  }
  path = path.parent_path();
  if (path.filename() != "unsealed") {
    FX_LOGS(ERROR) << "Failed to find zxcrypt in: " << path;
    return zx::error(ZX_ERR_BAD_STATE);
  }
  path = path.parent_path();
  if (path.filename() != "zxcrypt") {
    FX_LOGS(ERROR) << "Failed to find zxcrypt in: " << path;
    return zx::error(ZX_ERR_BAD_STATE);
  }

  fbl::unique_fd zxcrypt_fd(open(path.c_str(), O_RDWR));
  if (!zxcrypt_fd.is_valid()) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  fdio_cpp::UnownedFdioCaller zxcrypt_caller(zxcrypt_fd);
  fidl::UnownedClientEnd<fuchsia_hardware_block_encrypted::DeviceManager> zxcrypt_client(
      zxcrypt_caller.channel());
  auto result = fidl::WireCall(zxcrypt_client).Shred();
  if (result.status() != ZX_OK) {
    return zx::error(result.status());
  }
  return zx::make_status(result->status);
}

uint64_t EstimateMinfsRequiredSpace(const Copier& copier) {
  std::vector<const Copier::DirectoryEntries*> pending;
  pending.push_back(&copier.entries());
  uint64_t estimate = 0;
  while (!pending.empty()) {
    const Copier::DirectoryEntries* entries = pending.back();
    pending.pop_back();
    // Each directory will typically only use a single block for storing directory entries. A single
    // block can hold at least 30 entries and in practice will hold significantly more. Most
    // directories don't contain 30 entries so this should rarely under estimate.
    estimate += minfs::kMinfsBlockSize;

    // Fail to compile if extra types are added.
    static_assert(std::variant_size_v<Copier::DirectoryEntry> == 2);
    for (const auto& entry : *entries) {
      if (std::holds_alternative<Copier::File>(entry)) {
        estimate +=
            fbl::round_up(std::get<Copier::File>(entry).contents.size(), minfs::kMinfsBlockSize);
      } else if (std::holds_alternative<Copier::Directory>(entry)) {
        pending.push_back(&std::get<Copier::Directory>(entry).entries);
      }
    }
  }
  return estimate;
}

}  // namespace

zx::status<fuchsia_hardware_block::wire::BlockInfo> GetBlockDeviceInfo(
    const zx::unowned_channel& device) {
  fidl::UnownedClientEnd<fuchsia_hardware_block::Block> client(device);
  auto result = fidl::WireCall(client).GetInfo();
  if (result.status() != ZX_OK) {
    return zx::error(result.status());
  }
  if (result->status != ZX_OK) {
    return zx::error(result->status);
  }
  return zx::ok(*result->info);
}

std::vector<std::filesystem::path> ParseExcludedPaths(std::string_view excluded_paths) {
  std::vector<std::string> strings =
      fxl::SplitStringCopy(excluded_paths, ",", fxl::WhiteSpaceHandling::kTrimWhitespace,
                           fxl::SplitResult::kSplitWantNonEmpty);
  std::vector<std::filesystem::path> paths;
  paths.reserve(strings.size());
  for (std::string& str : strings) {
    paths.emplace_back(std::move(str));
  }
  return paths;
}

MaybeResizeMinfsResult MaybeResizeMinfs(zx::channel device, uint64_t partition_size_limit,
                                        uint64_t required_inodes, uint64_t data_size_limit,
                                        const std::vector<std::filesystem::path>& excluded_paths,
                                        InspectManager& inspect) {
  zx::status<MountedMinfs> minfs = MountedMinfs::Mount(CloneDeviceChannel(device));
  if (minfs.is_error()) {
    FX_LOGS(ERROR) << "Failed to mount minfs: " << minfs.status_string();
    // Hopefully the caller will have better luck.
    return MaybeResizeMinfsResult::kMinfsMountable;
  }

  // Check if minfs was already resized but failed while writing the data to the new minfs instance.
  auto previously_failed_while_writing = minfs->IsResizeInProgress();
  // If the check for "resize in progress" fails then continue on as if the file didn't exist. This
  // check happens before checking if minfs is mis-sized and will run at every boot. We don't want a
  // transient error to cause a device to get wiped.
  if (!previously_failed_while_writing.is_error() && *previously_failed_while_writing) {
    inspect.LogMinfsUpgradeProgress(InspectManager::MinfsUpgradeState::kDetectedFailedUpgrade);
    FX_LOGS(INFO) << "Minfs was previously resized and failed while writing data";
    // Shred zxcrypt then reboot. Although we lose data it's safer to start from scratch than to
    // have partially written data and potentially put components in unknown and untested states.
    if (zx::status<> status = ShredZxcrypt(device.borrow()); status.is_error()) {
      FX_LOGS(ERROR) << "Failed to shred zxcrypt: " << status.status_string();
      // Reboot to try again.
      return MaybeResizeMinfsResult::kRebootRequired;
    }
    // Technically we could Seal and Format the zxcrypt partition from here which would destroy the
    // current block |device| and create a new one. The new block device would get picked up by
    // fshost and formatted with minfs then the system could continue to boot. Rebooting the device
    // achieves the same thing though and is simpler.
    return MaybeResizeMinfsResult::kRebootRequired;
  }

  auto minfs_info = minfs->GetFilesystemInfo();
  if (minfs_info.is_error()) {
    FX_LOGS(ERROR) << "Failed to get minfs filesystem info: " << minfs_info.status_string();
    // Minfs hasn't been modified. Continue as normal and try again at next reboot.
    return MaybeResizeMinfsResult::kMinfsMountable;
  }

  auto block_device_info = GetBlockDeviceInfo(device.borrow());
  if (block_device_info.is_error()) {
    FX_LOGS(ERROR) << "Failed to get block device info: " << block_device_info.status_string();
    // Minfs hasn't been modified. Continue as normal and try again at next reboot.
    return MaybeResizeMinfsResult::kMinfsMountable;
  }
  uint64_t block_device_size = block_device_info->block_size * block_device_info->block_count;

  bool is_within_partition_size_limit = block_device_size <= partition_size_limit;
  bool has_correct_inode_count = minfs_info->total_nodes == required_inodes;
  if (is_within_partition_size_limit && has_correct_inode_count) {
    FX_LOGS(INFO) << "minfs already has " << required_inodes << " inodes and is only using "
                  << block_device_size << " bytes of its " << partition_size_limit << " byte limit";
    inspect.LogMinfsUpgradeProgress(InspectManager::MinfsUpgradeState::kSkipped);
    // Minfs is already sized correctly. Continue as normal.
    return MaybeResizeMinfsResult::kMinfsMountable;
  }
  if (!has_correct_inode_count) {
    FX_LOGS(INFO) << "minfs has " << minfs_info->total_nodes << " inodes when it requires exactly "
                  << required_inodes << " inodes and needs to be resized";
  }
  if (!is_within_partition_size_limit) {
    FX_LOGS(INFO) << "minfs is using " << block_device_size << " bytes of its "
                  << partition_size_limit << " byte limit and needs to be resized";
  }

  // Copy all of minfs into ram.
  inspect.LogMinfsUpgradeProgress(InspectManager::MinfsUpgradeState::kReadOldPartition);
  zx::status<Copier> copier = minfs->ReadFilesystem(excluded_paths);
  if (copier.is_error()) {
    FX_LOGS(ERROR) << "Failed to read the contents of minfs into memory: "
                   << copier.status_string();
    // Minfs hasn't been modified. Continue as normal and try again at next reboot.
    return MaybeResizeMinfsResult::kMinfsMountable;
  }

  uint64_t required_space_estimate = EstimateMinfsRequiredSpace(*copier);
  if (required_space_estimate > data_size_limit) {
    FX_LOGS(INFO)
        << "minfs will likely require " << required_space_estimate
        << " bytes to hold all of the data after resizing which is greater than the limit of "
        << data_size_limit << " bytes";
    inspect.LogMinfsUpgradeProgress(InspectManager::MinfsUpgradeState::kSkipped);
    // Minfs hasn't been modified. Continue as normal and try again at next reboot.
    return MaybeResizeMinfsResult::kMinfsMountable;
  }
  FX_LOGS(INFO)
      << "minfs will likely require " << required_space_estimate
      << " bytes to hold all of the data after resizing which should fit within the limit of "
      << data_size_limit << " bytes in the new minfs";

  if (zx::status<> status = MountedMinfs::Unmount(*std::move(minfs)); status.is_error()) {
    FX_LOGS(ERROR) << "Failed to unmount minfs: " << status.status_string();
    // Minfs hasn't been modified but we don't want 2 minfs instances mounted on the same block
    // device so recommend a reboot.
    return MaybeResizeMinfsResult::kRebootRequired;
  }

  // No turning back point.
  inspect.LogMinfsUpgradeProgress(InspectManager::MinfsUpgradeState::kWriteNewPartition);

  // Recreate minfs. During mkfs, minfs deallocates all fvm slices from the partition before
  // re-allocating which will correctly resize the partition.
  if (zx::status<> status = FormatMinfs(CloneDeviceChannel(device)); status.is_error()) {
    FX_LOGS(ERROR) << "Failed to format minfs: " << status.status_string();
    // fsck should fail on the next boot and formatting will be attempted again provided
    // format-minfs-on-corruption is set. All files are lost.
    return MaybeResizeMinfsResult::kRebootRequired;
  }

  // Mount the new minfs and copy the files back to it.
  minfs = MountedMinfs::Mount(CloneDeviceChannel(device));
  if (minfs.is_error()) {
    FX_LOGS(ERROR) << "Failed to mount minfs: " << minfs.status_string();
    // If minfs was corrupt then fsck should fail on next boot and minfs will be reformated provided
    // format-minfs-on-corruption is set. All files are lost.
    return MaybeResizeMinfsResult::kRebootRequired;
  }
  if (zx::status<> status = minfs->PopulateFilesystem(*std::move(copier)); status.is_error()) {
    FX_LOGS(ERROR) << "Failed to write data back to minfs: " << status.status_string();
    // Triggering a reboot here will land the device back at the top of this function which handles
    // incomplete writes. All files are lost.
    return MaybeResizeMinfsResult::kRebootRequired;
  }

  inspect.LogMinfsUpgradeProgress(InspectManager::MinfsUpgradeState::kFinished);
  FX_LOGS(INFO) << "Minfs was successfully resized";
  return MaybeResizeMinfsResult::kMinfsMountable;
}

MountedMinfs::~MountedMinfs() {
  if (root_.is_valid()) {
    if (zx::status<> status = Unmount(); status.is_error()) {
      FX_LOGS(ERROR) << "Failed to unmount minfs: " << status.status_string();
    }
  }
}

zx::status<MountedMinfs> MountedMinfs::Mount(zx::channel device) {
  // Convert the device channel to a file descriptor which is needed by |mount|.
  fbl::unique_fd device_fd;
  if (zx_status_t status = fdio_fd_create(device.release(), device_fd.reset_and_get_address());
      status != ZX_OK) {
    return zx::error(status);
  }

  // Mount minfs
  zx::channel outgoing_dir_client;
  zx::channel outgoing_dir_server;
  if (zx_status_t status =
          zx::channel::create(/*flags=*/0, &outgoing_dir_client, &outgoing_dir_server);
      status != ZX_OK) {
    return zx::error(status);
  }
  MountOptions options;
  options.outgoing_directory.client = outgoing_dir_client.get();
  options.outgoing_directory.server = outgoing_dir_server.release();
  if (zx_status_t status = mount(device_fd.release(), /*mount_path=*/nullptr, DISK_FORMAT_MINFS,
                                 options, launch_logs_async);
      status != ZX_OK) {
    return zx::error(status);
  }

  // Open a channel to the root of the filesystem and drop the channel to the outgoing directory as
  // it's no longer needed.
  zx::channel root;
  if (zx_status_t status = fs_root_handle(outgoing_dir_client.get(), root.reset_and_get_address());
      status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(MountedMinfs(std::move(root)));
}

zx::status<> MountedMinfs::Unmount(MountedMinfs fs) { return fs.Unmount(); }

zx::status<fuchsia_io_admin::wire::FilesystemInfo> MountedMinfs::GetFilesystemInfo() const {
  fidl::UnownedClientEnd<fuchsia_io_admin::DirectoryAdmin> directory_admin(root_.borrow());
  auto query_result = fidl::WireCall(directory_admin).QueryFilesystem();
  if (query_result.status() != ZX_OK) {
    return zx::error(query_result.status());
  }
  auto query_response = query_result.Unwrap();
  if (query_response->s != ZX_OK) {
    return zx::error(query_response->s);
  }
  return zx::ok(*query_response->info);
}

zx::status<> MountedMinfs::PopulateFilesystem(Copier copier) const {
  if (zx::status<> status = SetResizeInProgress(); status.is_error()) {
    return status;
  }
  zx::status<fbl::unique_fd> root = GetRootFd();
  if (root.is_error()) {
    return root.take_error();
  }
  if (zx_status_t status = copier.Write(*std::move(root)); status != ZX_OK) {
    return zx::error(status);
  }
  root = GetRootFd();
  if (root.is_error()) {
    return root.take_error();
  }
  if (syncfs(root->get()) != 0) {
    return zx::error(ZX_ERR_IO);
  }
  return ClearResizeInProgress();
}

zx::status<Copier> MountedMinfs::ReadFilesystem(
    const std::vector<std::filesystem::path>& excluded_paths) const {
  zx::status<fbl::unique_fd> root = GetRootFd();
  if (root.is_error()) {
    return root.take_error();
  }
  return Copier::Read(*std::move(root), excluded_paths);
}

MountedMinfs::MountedMinfs(zx::channel root) : root_(std::move(root)) {}

zx::status<> MountedMinfs::Unmount() {
  // Take |root_| so the destructor doesn't try to unmount again.
  fidl::ClientEnd<fuchsia_io_admin::DirectoryAdmin> directory_admin(std::move(root_));
  auto result = fidl::WireCall(directory_admin).Unmount();
  if (result.status() != ZX_OK) {
    return zx::error(result.status());
  }
  return zx::make_status(result->s);
}

zx::status<fbl::unique_fd> MountedMinfs::GetRootFd() const {
  zx_handle_t clone = fdio_service_clone(root_.get());
  if (clone == ZX_HANDLE_INVALID) {
    return zx::error(ZX_ERR_BAD_HANDLE);
  }
  fbl::unique_fd root_fd;
  if (zx_status_t status = fdio_fd_create(clone, root_fd.reset_and_get_address());
      status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(root_fd));
}

zx::status<> MountedMinfs::SetResizeInProgress() const {
  auto root_fd = GetRootFd();
  if (!root_fd->is_valid()) {
    return root_fd.take_error();
  }
  fbl::unique_fd fd(openat(root_fd->get(), kMinfsResizeInProgressFilename, O_CREAT, 0666));
  if (!fd.is_valid()) {
    return zx::error(ZX_ERR_IO);
  }
  if (fsync(fd.get()) != 0) {
    return zx::error(ZX_ERR_IO);
  }
  return zx::ok();
}

zx::status<> MountedMinfs::ClearResizeInProgress() const {
  auto root_fd = GetRootFd();
  if (!root_fd->is_valid()) {
    return root_fd.take_error();
  }
  if (unlinkat(root_fd->get(), kMinfsResizeInProgressFilename, /*flags=*/0) != 0) {
    return zx::error(ZX_ERR_IO);
  }
  if (syncfs(root_fd->get()) != 0) {
    return zx::error(ZX_ERR_IO);
  }
  return zx::ok();
}

zx::status<bool> MountedMinfs::IsResizeInProgress() const {
  auto root_fd = GetRootFd();
  if (!root_fd->is_valid()) {
    return root_fd.take_error();
  }
  if (faccessat(root_fd->get(), kMinfsResizeInProgressFilename, F_OK, /*flags=*/0) != 0) {
    if (errno == ENOENT) {
      return zx::ok(false);
    }
    return zx::error(ZX_ERR_IO);
  }
  return zx::ok(true);
}

}  // namespace fshost

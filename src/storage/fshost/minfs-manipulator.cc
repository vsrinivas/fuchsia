// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/minfs-manipulator.h"

#include <fcntl.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <fidl/fuchsia.io.admin/cpp/wire.h>
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

#include <block-client/cpp/remote-block-device.h>
#include <fbl/unique_fd.h>
#include <fs-management/admin.h>
#include <fs-management/format.h>
#include <fs-management/launch.h>
#include <fs-management/mount.h>

#include "src/lib/files/file_descriptor.h"
#include "src/storage/fshost/copier.h"
#include "src/storage/fvm/client.h"

namespace fshost {
namespace {

constexpr char kMinfsResizeInProgressFilename[] = "minfs-resize-in-progress";

zx::channel CloneDeviceChannel(const zx::channel& device) {
  return zx::channel(fdio_service_clone(device.get()));
}

zx::status<std::unique_ptr<block_client::RemoteBlockDevice>> GetRemoteBlockDevice(
    zx::channel device) {
  std::unique_ptr<block_client::RemoteBlockDevice> block_device;
  if (zx_status_t status =
          block_client::RemoteBlockDevice::Create(std::move(device), &block_device);
      status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(block_device));
}

zx::status<> FormatMinfs(zx::channel device) {
  const char* argv[] = {"/pkg/bin/minfs", "mkfs", nullptr};
  zx_handle_t handles[] = {device.release()};
  uint32_t ids[] = {FS_HANDLE_BLOCK_DEVICE_ID};
  return zx::make_status(launch_stdio_sync(/*argc=*/2, argv, handles, ids, /*len=*/1));
}

zx::status<> ClearPartition(zx::channel device, uint32_t device_block_size) {
  // Overwrite the start of the block device which should contain the minfs magic. If freeing the
  // slices partially fails or mkfs fails after this then without the correct magic minfs won't be
  // mounted in a corrupted state.
  fbl::unique_fd device_fd;
  if (zx_status_t status =
          fdio_fd_create(CloneDeviceChannel(device).release(), device_fd.reset_and_get_address());
      status != ZX_OK) {
    return zx::error(status);
  }
  if (lseek(device_fd.get(), 0, SEEK_SET) != 0) {
    return zx::error(ZX_ERR_IO);
  }
  std::vector<char> zeros(device_block_size, 0);
  if (!fxl::WriteFileDescriptor(device_fd.get(), zeros.data(), device_block_size)) {
    return zx::error(ZX_ERR_IO);
  }

  auto remoteblock_device = GetRemoteBlockDevice(std::move(device));
  if (remoteblock_device.is_error()) {
    return remoteblock_device.take_error();
  }
  FX_LOGS(INFO) << "Resizing minfs";
  if (zx_status_t status = fvm::ResetAllSlices((*remoteblock_device).get()); status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to reset all of the slices in minfs: "
                   << zx_status_get_string(status);
    return zx::error(status);
  }
  return zx::ok();
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

zx::status<> MaybeResizeMinfs(zx::channel device, uint64_t size_limit, uint64_t required_inodes) {
  zx::status<MountedMinfs> minfs = MountedMinfs::Mount(CloneDeviceChannel(device));
  if (minfs.is_error()) {
    FX_LOGS(ERROR) << "Failed to mount minfs: " << minfs.status_string();
    return minfs.take_error();
  }

  // Check if minfs was already resized but failed while writing the data to the new minfs instance.
  auto previously_failed_while_writing = minfs->IsResizeInProgress();
  // If the check for "resize in progress" fails then continue on as if the file didn't exist. This
  // check happens before checking if minfs is mis-sized and will run at every boot. We don't want a
  // transient error to cause a device to get wiped.
  if (!previously_failed_while_writing.is_error() && *previously_failed_while_writing) {
    FX_LOGS(INFO)
        << "Minfs was previously resized and failed while writing data, reformatting minfs now";
    // Unmount the currently mounted minfs and reformat minfs again. Although we lose data it's
    // safer to start from an empty minfs than to have partially written data and potentially put
    // components in unknown and untested states.
    if (zx::status<> status = MountedMinfs::Unmount(*std::move(minfs)); status.is_error()) {
      FX_LOGS(ERROR) << "Failed to unmount minfs: " << status.status_string();
      return status;
    }
    return FormatMinfs(std::move(device));
  }

  auto minfs_info = minfs->GetFilesystemInfo();
  if (minfs_info.is_error()) {
    FX_LOGS(ERROR) << "Failed to get minfs filesystem info: " << minfs_info.status_string();
    return minfs_info.take_error();
  }

  auto block_device_info = GetBlockDeviceInfo(device.borrow());
  if (block_device_info.is_error()) {
    return block_device_info.take_error();
  }
  uint64_t block_device_size = block_device_info->block_size * block_device_info->block_count;

  if (block_device_size <= size_limit && minfs_info->total_nodes == required_inodes) {
    // Minfs is already sized correctly.
    return zx::ok();
  }

  // Copy all of minfs into ram.
  // TODO(fxbug.dev/84885): Filter out files that shouldn't be copied and check that the copied
  // files will fit in the new minfs before destroying minfs.
  zx::status<Copier> copier = minfs->ReadFilesystem();
  if (copier.is_error()) {
    FX_LOGS(ERROR) << "Failed to read the contents minfs into memory: " << copier.status_string();
    // Minfs wasn't modified yet. Try again on next boot.
    return copier.take_error();
  }

  if (zx::status<> status = MountedMinfs::Unmount(*std::move(minfs)); status.is_error()) {
    FX_LOGS(ERROR) << "Failed to unmount minfs: " << status.status_string();
    // Minfs wasn't modified yet. Try again on next boot.
    return status;
  }

  // No turning back point.
  if (zx::status<> status =
          ClearPartition(CloneDeviceChannel(device), block_device_info->block_size);
      status.is_error()) {
    // All files are likely lost.
    return status;
  }

  // Recreate minfs.
  if (zx::status<> status = FormatMinfs(CloneDeviceChannel(device)); status.is_error()) {
    FX_LOGS(ERROR) << "Failed to format minfs: " << status.status_string();
    // fsck should fail on the next boot and formatting will be attempted again. All files are lost.
    return status;
  }

  // Mount the new minfs and copy the files back to it.
  minfs = MountedMinfs::Mount(CloneDeviceChannel(device));
  if (minfs.is_error()) {
    FX_LOGS(ERROR) << "Failed to mount minfs: " << minfs.status_string();
    // If minfs was corrupt then fsck should fail on next boot and minfs will be reformated again.
    // All files are lost.
    return minfs.take_error();
  }
  if (zx::status<> status = minfs->PopulateFilesystem(*std::move(copier)); status.is_error()) {
    FX_LOGS(ERROR) << "Failed to write data back to minfs: " << status.status_string();
    // TODO(fxbug.dev/84885): Recover from failed writes.
    return status;
  }
  FX_LOGS(INFO) << "Minfs was successfully resized";

  return zx::ok();
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

zx::status<Copier> MountedMinfs::ReadFilesystem() const {
  zx::status<fbl::unique_fd> root = GetRootFd();
  if (root.is_error()) {
    return root.take_error();
  }
  return Copier::Read(*std::move(root));
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

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/block/llcpp/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/vfs.h>
#include <lib/zx/channel.h>
#include <string.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <utility>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <fs-management/mount.h>
#include <fs/vfs.h>
#include <pretty/hexdump.h>

#include "admin.h"

namespace fblock = ::llcpp::fuchsia::hardware::block;
namespace fio = ::llcpp::fuchsia::io;

namespace fs_management {
namespace {

void UnmountHandle(zx_handle_t root, bool wait_until_ready) {
  // We've entered a failure case where the filesystem process (which may or may not be alive)
  // had a *chance* to be spawned, but cannot be attached to a vnode (for whatever reason).
  // Rather than abandoning the filesystem process (maybe causing dirty bits to be set), give it a
  // chance to shutdown properly.
  //
  // The unmount process is a little atypical, since we're just sending a signal over a handle,
  // rather than detaching the mounted filesystem from the "parent" filesystem.
  fs::Vfs::UnmountHandle(zx::channel(root),
                         wait_until_ready ? zx::time::infinite() : zx::time::infinite_past());
}

zx_status_t MakeDirAndRemoteMount(const char* path, zx::channel root) {
  // Open the parent path as O_ADMIN, and sent the mkdir+mount command
  // to that directory.
  char parent_path[PATH_MAX];
  const char* name;
  strcpy(parent_path, path);
  char* last_slash = strrchr(parent_path, '/');
  if (last_slash == NULL) {
    strcpy(parent_path, ".");
    name = path;
  } else {
    *last_slash = '\0';
    name = last_slash + 1;
    if (*name == '\0') {
      return ZX_ERR_INVALID_ARGS;
    }
  }

  zx_status_t status;
  zx::channel parent, parent_server;
  if ((status = zx::channel::create(0, &parent, &parent_server)) != ZX_OK) {
    return status;
  }
  if ((status = fdio_open(parent_path, O_RDONLY | O_DIRECTORY | O_ADMIN,
                          parent_server.release())) != ZX_OK) {
    return status;
  }
  fio::DirectoryAdmin::SyncClient parent_client(std::move(parent));
  auto resp =
      parent_client.MountAndCreate(std::move(root), fidl::unowned_str(name, strlen(name)), 0);
  if (!resp.ok()) {
    return resp.status();
  }
  return resp.value().s;
}

zx_status_t StartFilesystem(fbl::unique_fd device_fd, disk_format_t df,
                            const mount_options_t* options, LaunchCallback cb,
                            OutgoingDirectory outgoing_directory, zx::channel* out_data_root) {
  // get the device handle from the device_fd
  zx_status_t status;
  zx::channel device;
  status = fdio_get_service_handle(device_fd.release(), device.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  // convert mount options to init options
  init_options_t init_options = {
      .readonly = options->readonly,
      .verbose_mount = options->verbose_mount,
      .collect_metrics = options->collect_metrics,
      .wait_until_ready = options->wait_until_ready,
      .enable_journal = options->enable_journal,
      .enable_pager = options->enable_pager,
      .write_compression_algorithm = options->write_compression_algorithm,
      // TODO(jfsulliv): This is currently only used in tests. Plumb through mount options if
      // needed.
      .write_compression_level = -1,
      .fsck_after_every_transaction = options->fsck_after_every_transaction,
      .callback = cb,
  };

  // launch the filesystem process
  zx::unowned_channel export_root(outgoing_directory.client);
  status =
      FsInit(std::move(device), df, init_options, std::move(outgoing_directory)).status_value();
  if (status != ZX_OK) {
    return status;
  }

  // register the export root with the fshost registry
  if (options->register_fs && ((status = fs_register(export_root->get())) != ZX_OK)) {
    return status;
  }

  // Extract the handle to the root of the filesystem from the export root. The POSIX flag will
  // cause the writable and executable rights to be inherited (if present).
  uint32_t flags = fio::OPEN_RIGHT_READABLE | fio::OPEN_FLAG_POSIX;
  if (options->admin)
    flags |= fio::OPEN_RIGHT_ADMIN;
  auto handle_or = GetFsRootHandle(zx::unowned_channel(export_root), flags);
  if (handle_or.is_error()) {
    return handle_or.status_value();
  }
  *out_data_root = std::move(handle_or).value();
  return ZX_OK;
}

}  // namespace
}  // namespace fs_management

const mount_options_t default_mount_options = {
    .readonly = false,
    .verbose_mount = false,
    .collect_metrics = false,
    .wait_until_ready = true,
    .create_mountpoint = false,
    .enable_journal = true,
    .enable_pager = false,
    .write_compression_algorithm = nullptr,
    .register_fs = true,
    .fsck_after_every_transaction = false,
    .admin = true,
    .outgoing_directory = {ZX_HANDLE_INVALID, ZX_HANDLE_INVALID},
};

const mkfs_options_t default_mkfs_options = {
    .fvm_data_slices = 1,
    .verbose = false,
    .sectors_per_cluster = 0,
};

const fsck_options_t default_fsck_options = {
    .verbose = false,
    .never_modify = false,
    .always_modify = false,
    .force = false,
    .apply_journal = false,
};

enum DiskFormatLogVerbosity {
  Silent,
  Verbose,
};

disk_format_t detect_disk_format_impl(int fd, DiskFormatLogVerbosity verbosity) {
  if (lseek(fd, 0, SEEK_SET) != 0) {
    fprintf(stderr, "detect_disk_format: Cannot seek to start of device.\n");
    return DISK_FORMAT_UNKNOWN;
  }

  fdio_cpp::UnownedFdioCaller caller(fd);
  auto resp = fblock::Block::Call::GetInfo(caller.channel());
  if (!resp.ok() || resp.value().status != ZX_OK) {
    fprintf(stderr, "detect_disk_format: Could not acquire block device info\n");
    return DISK_FORMAT_UNKNOWN;
  }

  // We need to read at least two blocks, because the GPT magic is located inside the second block
  // of the disk.
  size_t header_size =
      (HEADER_SIZE > (2 * resp->info->block_size)) ? HEADER_SIZE : (2 * resp->info->block_size);
  // check if the partition is big enough to hold the header in the first place
  if (header_size > resp.value().info->block_size * resp.value().info->block_count) {
    return DISK_FORMAT_UNKNOWN;
  }

  // We expect to read HEADER_SIZE bytes, but we may need to read
  // extra to read a multiple of the underlying block size.
  const size_t buffer_size =
      fbl::round_up(header_size, static_cast<size_t>(resp.value().info->block_size));

  uint8_t data[buffer_size];
  if (read(fd, data, buffer_size) != static_cast<ssize_t>(buffer_size)) {
    fprintf(stderr, "detect_disk_format: Error reading block device.\n");
    return DISK_FORMAT_UNKNOWN;
  }

  if (!memcmp(data, fvm_magic, sizeof(fvm_magic))) {
    return DISK_FORMAT_FVM;
  }

  if (!memcmp(data, zxcrypt_magic, sizeof(zxcrypt_magic))) {
    return DISK_FORMAT_ZXCRYPT;
  }

  if (!memcmp(data, block_verity_magic, sizeof(block_verity_magic))) {
    return DISK_FORMAT_BLOCK_VERITY;
  }

  if (!memcmp(data + resp->info->block_size, gpt_magic, sizeof(gpt_magic))) {
    return DISK_FORMAT_GPT;
  }

  if (!memcmp(data, minfs_magic, sizeof(minfs_magic))) {
    return DISK_FORMAT_MINFS;
  }

  if (!memcmp(data, blobfs_magic, sizeof(blobfs_magic))) {
    return DISK_FORMAT_BLOBFS;
  }

  if (!memcmp(data, factoryfs_magic, sizeof(factoryfs_magic))) {
    return DISK_FORMAT_FACTORYFS;
  }

  if ((data[510] == 0x55 && data[511] == 0xAA)) {
    if ((data[38] == 0x29 || data[66] == 0x29)) {
      // 0x55AA are always placed at offset 510 and 511 for FAT filesystems.
      // 0x29 is the Boot Signature, but it is placed at either offset 38 or
      // 66 (depending on FAT type).
      return DISK_FORMAT_FAT;
    }
    return DISK_FORMAT_MBR;
  }

  if (verbosity == DiskFormatLogVerbosity::Verbose) {
    // Log a hexdump of the bytes we looked at and didn't find any magic in.
    fprintf(stderr, "detect_disk_format: did not recognize format.  Looked at:\n");
    // fvm, zxcrypt, minfs, and blobfs have their magic bytes at the start
    // of the block.
    hexdump_very_ex(data, 16, 0, hexdump_stdio_printf, stderr);
    // MBR is two bytes at offset 0x1fe, but print 16 just for consistency
    hexdump_very_ex(data + 0x1f0, 16, 0x1f0, hexdump_stdio_printf, stderr);
    // GPT magic is stored one block in, so it can coexist with MBR.
    hexdump_very_ex(data + resp->info->block_size, 16, resp->info->block_size, hexdump_stdio_printf,
                    stderr);
  }

  return DISK_FORMAT_UNKNOWN;
}

__EXPORT
disk_format_t detect_disk_format(int fd) {
  return detect_disk_format_impl(fd, DiskFormatLogVerbosity::Silent);
}

__EXPORT
disk_format_t detect_disk_format_log_unknown(int fd) {
  return detect_disk_format_impl(fd, DiskFormatLogVerbosity::Verbose);
}

__EXPORT
zx_status_t fmount(int dev_fd, int mount_fd, disk_format_t df, const mount_options_t* options,
                   LaunchCallback cb) {
  zx_status_t status;
  zx::channel data_root;
  fbl::unique_fd device_fd(dev_fd);

  fs_management::OutgoingDirectory handles{zx::unowned_channel(options->outgoing_directory.client),
                                           zx::channel(options->outgoing_directory.server)};
  zx::channel client;
  if (!*handles.client) {
    zx_status_t status = zx::channel::create(0, &client, &handles.server);
    if (status != ZX_OK)
      return status;
    handles.client = zx::unowned_channel(client);
  }

  if ((status = fs_management::StartFilesystem(std::move(device_fd), df, options, cb,
                                               std::move(handles), &data_root)) != ZX_OK) {
    return status;
  }

  fdio_cpp::FdioCaller caller{fbl::unique_fd(mount_fd)};
  auto resp = fio::DirectoryAdmin::Call::Mount(caller.channel(), std::move(data_root));
  caller.release().release();
  if (!resp.ok()) {
    return resp.status();
  }
  return resp.value().s;
}

__EXPORT
zx_status_t mount_root_handle(zx_handle_t root_handle, const char* mount_path) {
  zx_status_t status;
  zx::channel mount_point, mount_point_server;
  if ((status = zx::channel::create(0, &mount_point, &mount_point_server)) != ZX_OK) {
    return status;
  }
  if ((status = fdio_open(mount_path, O_RDONLY | O_DIRECTORY | O_ADMIN,
                          mount_point_server.release())) != ZX_OK) {
    return status;
  }
  fio::DirectoryAdmin::SyncClient mount_client(std::move(mount_point));
  auto resp = mount_client.Mount(zx::channel(root_handle));
  if (!resp.ok()) {
    return resp.status();
  }
  return resp.value().s;
}

__EXPORT
zx_status_t mount(int dev_fd, const char* mount_path, disk_format_t df,
                  const mount_options_t* options, LaunchCallback cb) {
  zx_status_t status;
  zx::channel data_root;
  fbl::unique_fd device_fd(dev_fd);

  fs_management::OutgoingDirectory handles{zx::unowned_channel(options->outgoing_directory.client),
                                           zx::channel(options->outgoing_directory.server)};
  zx::channel client;
  if (!*handles.client) {
    zx_status_t status = zx::channel::create(0, &client, &handles.server);
    if (status != ZX_OK)
      return status;
    handles.client = zx::unowned_channel(client);
  }

  if ((status = fs_management::StartFilesystem(std::move(device_fd), df, options, cb,
                                               std::move(handles), &data_root)) != ZX_OK) {
    return status;
  }

  // mount the channel in the requested location
  if (options->create_mountpoint) {
    return fs_management::MakeDirAndRemoteMount(mount_path, std::move(data_root));
  }

  return mount_root_handle(data_root.release(), mount_path);
}

__EXPORT
zx_status_t fumount(int mount_fd) {
  fdio_cpp::FdioCaller caller{fbl::unique_fd(mount_fd)};
  auto resp = fio::DirectoryAdmin::Call::UnmountNode(caller.channel());
  caller.release().release();
  if (!resp.ok()) {
    return resp.status();
  }
  if (resp.value().s != ZX_OK) {
    return resp.value().s;
  }
  return fs::Vfs::UnmountHandle(std::move(resp.value().remote), zx::time::infinite());
}

__EXPORT
zx_status_t umount(const char* mount_path) {
  fprintf(stderr, "Unmounting %s\n", mount_path);
  fbl::unique_fd fd(open(mount_path, O_DIRECTORY | O_NOREMOTE | O_ADMIN));
  if (!fd) {
    fprintf(stderr, "Could not open directory: %s\n", strerror(errno));
    return ZX_ERR_BAD_STATE;
  }
  zx_status_t status = fumount(fd.get());
  return status;
}

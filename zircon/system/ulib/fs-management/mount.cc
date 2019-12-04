// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/vfs.h>
#include <lib/fzl/fdio.h>
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

namespace {

using fbl::unique_fd;

zx_status_t MountFs(int fd, zx_handle_t root) {
  zx_status_t status;
  fzl::FdioCaller caller{fbl::unique_fd(fd)};
  zx_status_t io_status = fuchsia_io_DirectoryAdminMount(caller.borrow_channel(), root, &status);
  caller.release().release();
  if (io_status != ZX_OK) {
    return io_status;
  }
  return status;
}

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

// Performs the actual work of mounting a volume.
class Mounter {
 public:
  // The mount point is either a path (to be created) or an existing fd.
  explicit Mounter(int fd) : path_(nullptr), fd_(fd) {}
  explicit Mounter(const char* path) : path_(path), fd_(-1) {}
  ~Mounter() {}

  // Mounts the given device.
  zx_status_t Mount(zx::channel device, disk_format_t format, const mount_options_t& options,
                    LaunchCallback cb);

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Mounter);

 private:
  zx_status_t PrepareHandles(zx::channel device);
  zx_status_t MakeDirAndMount(const mount_options_t& options);
  zx_status_t LaunchAndMount(LaunchCallback cb, const mount_options_t& options, const char** argv,
                             int argc);
  zx_status_t MountNativeFs(const char* binary, zx::channel device, const mount_options_t& options,
                            LaunchCallback cb);
  zx_status_t MountFat(zx::channel device, const mount_options_t& options, LaunchCallback cb);

  zx_handle_t root_ = ZX_HANDLE_INVALID;
  const char* path_;
  int fd_;
  uint32_t flags_ = 0;  // Currently not used.
  size_t num_handles_ = 0;
  zx_handle_t handles_[2];
  uint32_t ids_[2];
};

// Initializes 'handles_' and 'ids_' with the root handle and block device handle.
zx_status_t Mounter::PrepareHandles(zx::channel block_device) {
  zx::channel root_server, root_client;
  zx_status_t status = zx::channel::create(0, &root_server, &root_client);
  if (status != ZX_OK) {
    return status;
  }
  handles_[0] = root_server.release();
  ids_[0] = FS_HANDLE_ROOT_ID;
  handles_[1] = block_device.release();
  ids_[1] = FS_HANDLE_BLOCK_DEVICE_ID;
  num_handles_ = 2;

  root_ = root_client.release();
  return ZX_OK;
}

zx_status_t Mounter::MakeDirAndMount(const mount_options_t& options) {
  auto cleanup =
      fbl::MakeAutoCall([this, options]() { UnmountHandle(root_, options.wait_until_ready); });

  // Open the parent path as O_ADMIN, and sent the mkdir+mount command
  // to that directory.
  char parent_path[PATH_MAX];
  const char* name;
  strcpy(parent_path, path_);
  char* last_slash = strrchr(parent_path, '/');
  if (last_slash == NULL) {
    strcpy(parent_path, ".");
    name = path_;
  } else {
    *last_slash = '\0';
    name = last_slash + 1;
    if (*name == '\0') {
      return ZX_ERR_INVALID_ARGS;
    }
  }

  unique_fd parent(open(parent_path, O_RDONLY | O_DIRECTORY | O_ADMIN));
  if (!parent) {
    return ZX_ERR_IO;
  }

  cleanup.cancel();

  zx_status_t status;
  fzl::FdioCaller caller(std::move(parent));
  zx_status_t io_status = fuchsia_io_DirectoryAdminMountAndCreate(
      caller.borrow_channel(), root_, name, strlen(name), flags_, &status);
  if (io_status != ZX_OK) {
    return io_status;
  }
  return status;
}

// Calls the 'launch callback' and mounts the remote handle to the target vnode, if successful.
zx_status_t Mounter::LaunchAndMount(LaunchCallback cb, const mount_options_t& options,
                                    const char** argv, int argc) {
  auto cleanup =
      fbl::MakeAutoCall([this, options]() { UnmountHandle(root_, options.wait_until_ready); });

  zx_status_t status = cb(argc, argv, handles_, ids_, num_handles_);
  if (status != ZX_OK) {
    return status;
  }

  if (options.wait_until_ready) {
    // Wait until the filesystem is ready to take incoming requests
    zx_signals_t observed;
    status = zx_object_wait_one(root_, ZX_USER_SIGNAL_0 | ZX_CHANNEL_PEER_CLOSED, ZX_TIME_INFINITE,
                                &observed);
    if ((status != ZX_OK) || (observed & ZX_CHANNEL_PEER_CLOSED)) {
      status = (status != ZX_OK) ? status : ZX_ERR_BAD_STATE;
      return status;
    }
  }
  cleanup.cancel();

  // Install remote handle.
  if (options.create_mountpoint) {
    return MakeDirAndMount(options);
  }
  return MountFs(fd_, root_);
}

zx_status_t Mounter::MountNativeFs(const char* binary, zx::channel device,
                                   const mount_options_t& options, LaunchCallback cb) {
  zx_status_t status = PrepareHandles(std::move(device));
  if (status != ZX_OK) {
    return status;
  }

  if (options.verbose_mount) {
    printf("fs_mount: Launching %s\n", binary);
  }

  fbl::Vector<const char*> argv;
  argv.push_back(binary);
  if (options.readonly) {
    argv.push_back("--readonly");
  }
  if (options.verbose_mount) {
    argv.push_back("--verbose");
  }
  if (options.collect_metrics) {
    argv.push_back("--metrics");
  }
  if (options.enable_journal) {
    argv.push_back("--journal");
  }
  if (options.enable_pager) {
    argv.push_back("--pager");
  }
  argv.push_back("mount");
  argv.push_back(nullptr);
  return LaunchAndMount(cb, options, argv.data(), static_cast<int>(argv.size() - 1));
}

zx_status_t Mounter::MountFat(zx::channel device, const mount_options_t& options,
                              LaunchCallback cb) {
  zx_status_t status = PrepareHandles(std::move(device));
  if (status != ZX_OK) {
    return status;
  }

  if (options.verbose_mount) {
    printf("fs_mount: FAT not presently supported\n");
  }

  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Mounter::Mount(zx::channel device, disk_format_t format, const mount_options_t& options,
                           LaunchCallback cb) {
  switch (format) {
    case DISK_FORMAT_MINFS:
      return MountNativeFs("/boot/bin/minfs", std::move(device), options, cb);
    case DISK_FORMAT_BLOBFS:
      return MountNativeFs("/boot/bin/blobfs", std::move(device), options, cb);
    case DISK_FORMAT_FAT:
      return MountFat(std::move(device), options, cb);
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

}  // namespace

const mount_options_t default_mount_options = {
    .readonly = false,
    .verbose_mount = false,
    .collect_metrics = false,
    .wait_until_ready = true,
    .create_mountpoint = false,
    .enable_journal = true,
    .enable_pager = false,
};

const mkfs_options_t default_mkfs_options = {
    .fvm_data_slices = 1,
    .verbose = false,
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

  fuchsia_hardware_block_BlockInfo info;
  fzl::UnownedFdioCaller caller(fd);
  zx_status_t status;
  zx_status_t io_status =
      fuchsia_hardware_block_BlockGetInfo(caller.borrow_channel(), &status, &info);
  if (io_status != ZX_OK || status != ZX_OK) {
    fprintf(stderr, "detect_disk_format: Could not acquire block device info\n");
    return DISK_FORMAT_UNKNOWN;
  }

  // check if the partition is big enough to hold the header in the first place
  if (HEADER_SIZE > info.block_size * info.block_count) {
    return DISK_FORMAT_UNKNOWN;
  }

  // We expect to read HEADER_SIZE bytes, but we may need to read
  // extra to read a multiple of the underlying block size.
  const size_t buffer_size =
      fbl::round_up(static_cast<size_t>(HEADER_SIZE), static_cast<size_t>(info.block_size));

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

  if (!memcmp(data + 0x200, gpt_magic, sizeof(gpt_magic))) {
    return DISK_FORMAT_GPT;
  }

  if (!memcmp(data, minfs_magic, sizeof(minfs_magic))) {
    return DISK_FORMAT_MINFS;
  }

  if (!memcmp(data, blobfs_magic, sizeof(blobfs_magic))) {
    return DISK_FORMAT_BLOBFS;
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
    // GPT magic is stored 512 bytes in, so it can coexist with MBR.
    hexdump_very_ex(data + 0x200, 16, 0x200, hexdump_stdio_printf, stderr);
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
zx_status_t fmount(int device_fd, int mount_fd, disk_format_t df, const mount_options_t* options,
                   LaunchCallback cb) {
  Mounter mounter(mount_fd);

  zx::channel block_device;
  zx_status_t status = fdio_get_service_handle(device_fd, block_device.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  return mounter.Mount(std::move(block_device), df, *options, cb);
}

__EXPORT
zx_status_t mount(int device_fd, const char* mount_path, disk_format_t df,
                  const mount_options_t* options, LaunchCallback cb) {
  if (!options->create_mountpoint) {
    // Open mountpoint; use it directly.
    unique_fd mount_point(open(mount_path, O_RDONLY | O_DIRECTORY | O_ADMIN));
    if (!mount_point) {
      return ZX_ERR_BAD_STATE;
    }
    return fmount(device_fd, mount_point.get(), df, options, cb);
  }

  Mounter mounter(mount_path);

  zx::channel block_device;
  zx_status_t status = fdio_get_service_handle(device_fd, block_device.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }
  return mounter.Mount(std::move(block_device), df, *options, cb);
}

__EXPORT
zx_status_t fumount(int mount_fd) {
  zx_handle_t h;
  zx_status_t status;
  fzl::FdioCaller caller{fbl::unique_fd(mount_fd)};
  zx_status_t io_status =
      fuchsia_io_DirectoryAdminUnmountNode(caller.borrow_channel(), &status, &h);
  caller.release().release();
  if (io_status != ZX_OK) {
    return io_status;
  }
  zx::channel c(h);
  if (status != ZX_OK) {
    return status;
  }
  return fs::Vfs::UnmountHandle(std::move(c), zx::time::infinite());
}

__EXPORT
zx_status_t umount(const char* mount_path) {
  fprintf(stderr, "Unmounting %s\n", mount_path);
  unique_fd fd(open(mount_path, O_DIRECTORY | O_NOREMOTE | O_ADMIN));
  if (!fd) {
    fprintf(stderr, "Could not open directory: %s\n", strerror(errno));
    return ZX_ERR_BAD_STATE;
  }
  zx_status_t status = fumount(fd.get());
  return status;
}

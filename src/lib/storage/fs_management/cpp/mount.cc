// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <fidl/fuchsia.io.admin/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/namespace.h>
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
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <fs-management/mount.h>
#include <pretty/hexdump.h>

#include "src/lib/storage/vfs/cpp/fuchsia_vfs.h"

namespace fs_management {
namespace {

namespace fblock = fuchsia_hardware_block;
namespace fio = fuchsia_io;

using fuchsia_io_admin::DirectoryAdmin;

zx_status_t MakeDirAndRemoteMount(const char* path, fidl::ClientEnd<DirectoryAdmin> root) {
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
  uint32_t flags = fio::wire::kOpenRightReadable | fio::wire::kOpenRightWritable |
                   fio::wire::kOpenFlagDirectory | fio::wire::kOpenRightAdmin;
  if ((status = fdio_open(parent_path, flags, parent_server.release())) != ZX_OK) {
    return status;
  }
  fidl::WireSyncClient<DirectoryAdmin> parent_client(std::move(parent));
  auto resp =
      parent_client->MountAndCreate(root.TakeChannel(), fidl::StringView::FromExternal(name), 0);
  if (!resp.ok()) {
    return resp.status();
  }
  return resp.value().s;
}

zx::status<std::pair<fidl::ClientEnd<DirectoryAdmin>, fidl::ClientEnd<DirectoryAdmin>>>
StartFilesystem(fbl::unique_fd device_fd, DiskFormat df, const MountOptions& options,
                LaunchCallback cb, zx::channel crypt_client) {
  // get the device handle from the device_fd
  zx_status_t status;
  zx::channel device;
  status = fdio_get_service_handle(device_fd.release(), device.reset_and_get_address());
  if (status != ZX_OK) {
    return zx::error(status);
  }

  // convert mount options to init options
  InitOptions init_options = {
      .readonly = options.readonly,
      .verbose_mount = options.verbose_mount,
      .collect_metrics = options.collect_metrics,
      .wait_until_ready = options.wait_until_ready,
      .write_compression_algorithm = options.write_compression_algorithm,
      // TODO(jfsulliv): This is currently only used in tests. Plumb through mount options if
      // needed.
      .write_compression_level = -1,
      .cache_eviction_policy = options.cache_eviction_policy,
      .fsck_after_every_transaction = options.fsck_after_every_transaction,
      .sandbox_decompression = options.sandbox_decompression,
      .callback = cb,
  };

  // launch the filesystem process
  auto export_root_or = FsInit(std::move(device), df, init_options, std::move(crypt_client));
  if (export_root_or.is_error()) {
    return export_root_or.take_error();
  }

  // Extract the handle to the root of the filesystem from the export root. The POSIX flags will
  // cause the writable and executable rights to be inherited (if present).
  uint32_t flags = fio::wire::kOpenRightReadable | fio::wire::kOpenFlagPosixWritable |
                   fio::wire::kOpenFlagPosixExecutable;
  if (options.admin)
    flags |= fio::wire::kOpenRightAdmin;
  auto root_or = FsRootHandle(fidl::UnownedClientEnd<DirectoryAdmin>(*export_root_or), flags);
  if (root_or.is_error())
    return root_or.take_error();
  return zx::ok(std::make_pair(*std::move(export_root_or), *std::move(root_or)));
}

}  // namespace

__EXPORT
zx::status<fidl::ClientEnd<DirectoryAdmin>> Mount(fbl::unique_fd device_fd, int mount_fd,
                                                  DiskFormat df, const MountOptions& options,
                                                  LaunchCallback cb) {
  zx::channel crypt_client(options.crypt_client);
  if (options.bind_to_namespace) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  auto result = StartFilesystem(std::move(device_fd), df, options, cb, std::move(crypt_client));
  if (result.is_error())
    return result.take_error();
  auto [export_root, data_root] = *std::move(result);

  fdio_cpp::FdioCaller caller{fbl::unique_fd(mount_fd)};
  auto resp = fidl::WireCall<DirectoryAdmin>(caller.channel())
                  ->Mount(fidl::ClientEnd<fio::Directory>(data_root.TakeChannel()));
  caller.release().release();
  if (!resp.ok())
    return zx::error(resp.status());
  if (resp.value().s != ZX_OK)
    return zx::error(resp.value().s);

  return zx::ok(std::move(export_root));
}

__EXPORT
zx_status_t MountRootHandle(fidl::ClientEnd<DirectoryAdmin> root, const char* mount_path) {
  zx_status_t status;
  zx::channel mount_point, mount_point_server;
  if ((status = zx::channel::create(0, &mount_point, &mount_point_server)) != ZX_OK) {
    return status;
  }
  if ((status = fdio_open(mount_path, O_RDONLY | O_DIRECTORY | O_ADMIN,
                          mount_point_server.release())) != ZX_OK) {
    return status;
  }
  fidl::WireSyncClient<DirectoryAdmin> mount_client(std::move(mount_point));
  auto resp = mount_client->Mount(root.TakeChannel());
  if (!resp.ok()) {
    return resp.status();
  }
  return resp.value().s;
}

__EXPORT
zx::status<fidl::ClientEnd<fuchsia_io_admin::DirectoryAdmin>> Mount(fbl::unique_fd device_fd,
                                                                    const char* mount_path,
                                                                    DiskFormat df,
                                                                    const MountOptions& options,
                                                                    LaunchCallback cb) {
  zx::channel crypt_client(options.crypt_client);

  auto result = StartFilesystem(std::move(device_fd), df, options, cb, std::move(crypt_client));
  if (result.is_error())
    return result.take_error();
  auto [export_root, data_root] = *std::move(result);

  // If no mount point is provided, just return success; the caller can get whatever they want from
  // the export root.
  if (mount_path) {
    if (options.bind_to_namespace) {
      fdio_ns_t* ns;
      if (zx_status_t status = fdio_ns_get_installed(&ns); status != ZX_OK)
        return zx::error(status);
      if (zx_status_t status = fdio_ns_bind(ns, mount_path, data_root.TakeChannel().release());
          status != ZX_OK)
        return zx::error(status);
    } else {
      // mount the channel in the requested location
      if (options.create_mountpoint) {
        if (zx_status_t status = MakeDirAndRemoteMount(mount_path, std::move(data_root));
            status != ZX_OK) {
          return zx::error(status);
        }
      } else {
        if (zx_status_t status = MountRootHandle(std::move(data_root), mount_path); status != ZX_OK)
          return zx::error(status);
      }
    }
  }

  return zx::ok(std::move(export_root));
}

__EXPORT
zx_status_t Unmount(int mount_fd) {
  fdio_cpp::UnownedFdioCaller caller(mount_fd);
  auto resp = fidl::WireCall<DirectoryAdmin>(caller.channel())->UnmountNode();
  if (!resp.ok()) {
    return resp.status();
  }
  if (resp.value().s != ZX_OK) {
    return resp.value().s;
  }
  // Note: we are unsafely converting from a client end of the
  // |fuchsia.io/Directory| protocol into a client end of the
  // |fuchsia.io/DirectoryAdmin| protocol.
  // This method will only work if |mount_fd| is backed by a connection
  // that actually speaks the |DirectoryAdmin| protocol.
  fidl::ClientEnd<DirectoryAdmin> directory_admin_client(std::move(resp.value().remote.channel()));
  return fs::FuchsiaVfs::UnmountHandle(std::move(directory_admin_client), zx::time::infinite());
}

__EXPORT
zx_status_t Unmount(const char* mount_path) {
  fprintf(stderr, "Unmounting %s\n", mount_path);
  fbl::unique_fd fd(open(mount_path, O_DIRECTORY | O_NOREMOTE | O_ADMIN));
  if (!fd) {
    fprintf(stderr, "Could not open directory: %s\n", strerror(errno));
    return ZX_ERR_BAD_STATE;
  }
  zx_status_t status = Unmount(fd.get());
  return status;
}

}  // namespace fs_management

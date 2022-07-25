// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/fs_management/cpp/mount.h"

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.component/cpp/wire.h>
#include <fidl/fuchsia.fs.startup/cpp/wire.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/vfs.h>
#include <lib/fit/defer.h>
#include <lib/service/llcpp/service.h>
#include <lib/zx/channel.h>
#include <string.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <iostream>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <pretty/hexdump.h>

#include "src/lib/storage/fs_management/cpp/component.h"
#include "src/lib/storage/fs_management/cpp/path.h"
#include "src/lib/storage/vfs/cpp/fuchsia_vfs.h"

namespace fs_management {
namespace {

namespace fio = fuchsia_io;

using Directory = fuchsia_io::Directory;

zx::status<fidl::ClientEnd<Directory>> InitNativeFs(const char* binary, zx::channel device,
                                                    const MountOptions& options, LaunchCallback cb,
                                                    zx::channel crypt_client) {
  zx_status_t status;
  auto outgoing_directory_or = fidl::CreateEndpoints<Directory>();
  if (outgoing_directory_or.is_error())
    return outgoing_directory_or.take_error();
  std::vector<std::pair<uint32_t, zx::handle>> handles;
  handles.push_back({FS_HANDLE_BLOCK_DEVICE_ID, std::move(device)});
  handles.push_back({PA_DIRECTORY_REQUEST, outgoing_directory_or->server.TakeChannel()});
  if (crypt_client) {
    handles.push_back({PA_HND(PA_USER0, 2), std::move(crypt_client)});
  }

  if ((status = cb(options.as_argv(binary), std::move(handles)))) {
    return zx::error(status);
  }

  auto cleanup = fit::defer([&outgoing_directory_or]() {
    [[maybe_unused]] auto result = Shutdown(outgoing_directory_or->client);
  });

  if (options.wait_until_ready) {
    // Wait until the filesystem is ready to take incoming requests
    auto result = fidl::WireCall(outgoing_directory_or->client)->Describe();
    switch (result.status()) {
      case ZX_OK:
        break;
      case ZX_ERR_PEER_CLOSED:
        return zx::error(ZX_ERR_BAD_STATE);
      default:
        return zx::error(result.status());
    }
  }

  cleanup.cancel();
  return zx::ok(std::move(outgoing_directory_or->client));
}

zx::status<> InitNativeFsComponent(fidl::UnownedClientEnd<Directory> exposed_dir,
                                   zx::channel device, const MountOptions& options) {
  auto startup_client_end = service::ConnectAt<fuchsia_fs_startup::Startup>(exposed_dir);
  if (startup_client_end.is_error())
    return startup_client_end.take_error();
  auto startup_client = fidl::BindSyncClient(std::move(*startup_client_end));

  auto start_options_or = options.as_start_options();
  if (start_options_or.is_error())
    return start_options_or.take_error();

  auto res = startup_client->Start(std::move(device), std::move(*start_options_or));
  if (!res.ok())
    return zx::error(res.status());
  if (res->is_error())
    return zx::error(res->error_value());

  return zx::ok();
}

zx::status<fidl::ClientEnd<Directory>> FsInit(zx::channel device, DiskFormat df,
                                              const MountOptions& options, LaunchCallback cb,
                                              zx::channel crypt_client) {
  if (options.component_child_name) {
    std::string_view url =
        options.component_url.empty() ? DiskFormatComponentUrl(df) : options.component_url;
    // If we don't know the url, fall back on the old launching method.
    if (!url.empty()) {
      // Otherwise, launch the component way.
      auto exposed_dir_or = ConnectNativeFsComponent(url, *options.component_child_name,
                                                     options.component_collection_name);
      if (exposed_dir_or.is_error())
        return exposed_dir_or.take_error();
      zx::status<> start_status =
          InitNativeFsComponent(*exposed_dir_or, std::move(device), options);
      if (start_status.is_error() && options.component_collection_name) {
        // If we hit an error starting, destroy the component instance. It may have been left in a
        // partially initialized state. We purposely ignore the result of destruction; it probably
        // won't fail, but if it does there is nothing we can really do, and the start error is
        // more important.
        [[maybe_unused]] auto result = DestroyNativeFsComponent(*options.component_child_name,
                                                                *options.component_collection_name);
        return start_status.take_error();
      }
      return exposed_dir_or;
    }
  }

  switch (df) {
    case kDiskFormatMinfs:
      return InitNativeFs(GetBinaryPath("minfs").c_str(), std::move(device), options, cb,
                          std::move(crypt_client));
    case kDiskFormatFxfs:
      return InitNativeFs(GetBinaryPath("fxfs").c_str(), std::move(device), options, cb,
                          std::move(crypt_client));
    case kDiskFormatBlobfs:
      return InitNativeFs(GetBinaryPath("blobfs").c_str(), std::move(device), options, cb,
                          std::move(crypt_client));
    case kDiskFormatFat:
      // For now, fatfs will only ever be in a package and never in /boot/bin, so we can hard-code
      // the path.
      return InitNativeFs("/pkg/bin/fatfs", std::move(device), options, cb,
                          std::move(crypt_client));
    case kDiskFormatFactoryfs:
      return InitNativeFs(GetBinaryPath("factoryfs").c_str(), std::move(device), options, cb,
                          std::move(crypt_client));
    case kDiskFormatF2fs:
      return InitNativeFs(GetBinaryPath("f2fs").c_str(), std::move(device), options, cb,
                          std::move(crypt_client));
    default:
      auto* format = CustomDiskFormat::Get(df);
      if (format == nullptr) {
        return zx::error(ZX_ERR_NOT_SUPPORTED);
      }
      return InitNativeFs(format->binary_path().c_str(), std::move(device), options, cb,
                          std::move(crypt_client));
  }
}

zx::status<std::pair<fidl::ClientEnd<Directory>, fidl::ClientEnd<Directory>>> StartFilesystem(
    fbl::unique_fd device_fd, DiskFormat df, const MountOptions& options, LaunchCallback cb,
    zx::channel crypt_client) {
  // get the device handle from the device_fd
  zx_status_t status;
  zx::channel device;
  status = fdio_get_service_handle(device_fd.release(), device.reset_and_get_address());
  if (status != ZX_OK) {
    return zx::error(status);
  }

  // launch the filesystem process
  auto export_root_or = FsInit(std::move(device), df, options, cb, std::move(crypt_client));
  if (export_root_or.is_error()) {
    return export_root_or.take_error();
  }

  // Extract the handle to the root of the filesystem from the export root. The POSIX flags will
  // cause the writable and executable rights to be inherited (if present).
  auto root_or =
      FsRootHandle(fidl::UnownedClientEnd<Directory>(*export_root_or),
                   fio::wire::OpenFlags::kRightReadable | fio::wire::OpenFlags::kPosixWritable |
                       fio::wire::OpenFlags::kPosixExecutable);
  if (root_or.is_error())
    return root_or.take_error();
  return zx::ok(std::make_pair(*std::move(export_root_or), *std::move(root_or)));
}

std::string StripTrailingSlash(const std::string& in) {
  if (!in.empty() && in.back() == '/') {
    return in.substr(0, in.length() - 1);
  } else {
    return in;
  }
}

}  // namespace

MountedFilesystem::~MountedFilesystem() {
  if (export_root_.is_valid()) {
    [[maybe_unused]] auto result = UnmountImpl();
  }
}

zx::status<> MountedFilesystem::UnmountImpl() {
  zx_status_t status = ZX_OK;
  if (!mount_path_.empty()) {
    // Ignore errors unbinding, since we still want to continue and try and shut down the
    // filesystem.
    fdio_ns_t* ns;
    status = fdio_ns_get_installed(&ns);
    if (status == ZX_OK)
      status = fdio_ns_unbind(ns, StripTrailingSlash(mount_path_).c_str());
  }
  auto result = Shutdown(export_root_);
  return status != ZX_OK ? zx::error(status) : result;
}

__EXPORT
zx::status<> Shutdown(fidl::UnownedClientEnd<Directory> svc_dir) {
  auto admin_or = service::ConnectAt<fuchsia_fs::Admin>(svc_dir);
  if (admin_or.is_error()) {
    return admin_or.take_error();
  }

  auto resp = fidl::WireCall(*admin_or)->Shutdown();
  if (resp.status() != ZX_OK)
    return zx::error(resp.status());

  return zx::ok();
}

__EXPORT
zx::status<MountedFilesystem> Mount(fbl::unique_fd device_fd, const char* mount_path, DiskFormat df,
                                    const MountOptions& options, LaunchCallback cb) {
  zx::channel crypt_client;
  if (options.crypt_client)
    crypt_client = options.crypt_client();

  auto result = StartFilesystem(std::move(device_fd), df, options, cb, std::move(crypt_client));
  if (result.is_error()) {
    return result.take_error();
  }
  auto [export_root, data_root] = *std::move(result);

  auto fs = MountedFilesystem(std::move(export_root), {});

  if (mount_path) {
    fdio_ns_t* ns;
    if (zx_status_t status = fdio_ns_get_installed(&ns); status != ZX_OK)
      return zx::error(status);
    if (zx_status_t status = fdio_ns_bind(ns, mount_path, data_root.TakeChannel().release());
        status != ZX_OK)
      return zx::error(status);
    fs.set_mount_path(mount_path);
  }

  return zx::ok(std::move(fs));
}

}  // namespace fs_management

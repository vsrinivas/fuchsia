// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/fs_management/cpp/mount.h"

#include <errno.h>
#include <fcntl.h>
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
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <iostream>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <pretty/hexdump.h>

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
  std::array<zx_handle_t, 3> handles = {device.release(),
                                        outgoing_directory_or->server.TakeChannel().release(),
                                        crypt_client.release()};
  std::array<uint32_t, 3> ids = {FS_HANDLE_BLOCK_DEVICE_ID, PA_DIRECTORY_REQUEST,
                                 PA_HND(PA_USER0, 2)};

  std::vector<std::string> argv_strings = options.as_argv(binary);
  int argc = static_cast<int>(argv_strings.size());
  std::vector<const char*> argv;
  argv.reserve(argv_strings.size());
  for (const std::string& arg : argv_strings) {
    argv.push_back(arg.c_str());
  }
  argv.push_back(nullptr);

  if ((status = cb(argc, argv.data(), handles.data(), ids.data(),
                   handles[2] == ZX_HANDLE_INVALID ? 2 : 3)) != ZX_OK) {
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

__EXPORT
zx::status<fidl::ClientEnd<Directory>> FsInit(zx::channel device, DiskFormat df,
                                              const MountOptions& options, LaunchCallback cb,
                                              zx::channel crypt_client) {
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
  auto root_or = FsRootHandle(fidl::UnownedClientEnd<Directory>(*export_root_or),
                              fio::wire::kOpenRightReadable | fio::wire::kOpenFlagPosixWritable |
                                  fio::wire::kOpenFlagPosixExecutable);
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
  if (export_root_.is_valid()) [[maybe_unused]]
    auto result = UnmountImpl();
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
zx::status<> Shutdown(fidl::UnownedClientEnd<Directory> export_root) {
  auto admin_or = service::ConnectAt<fuchsia_fs::Admin>(export_root);
  if (admin_or.is_error())
    return admin_or.take_error();

  auto resp = fidl::WireCall(*admin_or)->Shutdown();
  if (resp.status() != ZX_OK)
    return zx::error(resp.status());

  return zx::ok();
}

__EXPORT
zx::status<MountedFilesystem> Mount(fbl::unique_fd device_fd, const char* mount_path, DiskFormat df,
                                    const MountOptions& options, LaunchCallback cb) {
  zx::channel crypt_client(options.crypt_client);

  auto result = StartFilesystem(std::move(device_fd), df, options, cb, std::move(crypt_client));
  if (result.is_error())
    return result.take_error();
  auto [export_root, data_root] = *std::move(result);

  if (mount_path) {
    fdio_ns_t* ns;
    if (zx_status_t status = fdio_ns_get_installed(&ns); status != ZX_OK)
      return zx::error(status);
    if (zx_status_t status = fdio_ns_bind(ns, mount_path, data_root.TakeChannel().release());
        status != ZX_OK)
      return zx::error(status);
  }

  return zx::ok(MountedFilesystem(std::move(export_root), mount_path ? mount_path : ""));
}

}  // namespace fs_management

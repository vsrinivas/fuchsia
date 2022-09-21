// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/fs_management/cpp/mount.h"

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.component/cpp/wire.h>
#include <fidl/fuchsia.fs.startup/cpp/wire.h>
#include <fidl/fuchsia.fs/cpp/wire.h>
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
#include <lib/zx/status.h>
#include <string.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <iostream>
#include <memory>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <pretty/hexdump.h>

#include "fidl/fuchsia.io/cpp/markers.h"
#include "src/lib/storage/fs_management/cpp/admin.h"
#include "src/lib/storage/fs_management/cpp/component.h"
#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/fs_management/cpp/options.h"
#include "src/lib/storage/fs_management/cpp/path.h"
#include "src/lib/storage/fs_management/cpp/volumes.h"
#include "zircon/status.h"

namespace fs_management {
namespace {

namespace fio = fuchsia_io;

using Directory = fuchsia_io::Directory;

zx::status<fidl::ClientEnd<Directory>> InitNativeFs(const char* binary, zx::channel device,
                                                    const MountOptions& options,
                                                    LaunchCallback cb) {
  zx_status_t status;
  auto outgoing_directory_or = fidl::CreateEndpoints<Directory>();
  if (outgoing_directory_or.is_error())
    return outgoing_directory_or.take_error();
  std::vector<std::pair<uint32_t, zx::handle>> handles;
  handles.emplace_back(FS_HANDLE_BLOCK_DEVICE_ID, std::move(device));
  handles.emplace_back(PA_DIRECTORY_REQUEST, outgoing_directory_or->server.TakeChannel());

  if (status = cb(options.as_argv(binary), std::move(handles)); status != ZX_OK) {
    return zx::error(status);
  }

  auto cleanup = fit::defer([&outgoing_directory_or]() {
    auto admin_client = service::ConnectAt<fuchsia_fs::Admin>(outgoing_directory_or->client);
    if (!admin_client.is_ok()) {
      return;
    }
    [[maybe_unused]] auto result = fidl::WireCall(*admin_client)->Shutdown();
  });

  if (options.wait_until_ready) {
    // Wait until the filesystem is ready to take incoming requests
    auto result = fidl::WireCall(outgoing_directory_or->client)->DescribeDeprecated();
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

zx::status<> StartFsComponent(fidl::UnownedClientEnd<Directory> exposed_dir, zx::channel device,
                              const MountOptions& options) {
  auto startup_client_end = service::ConnectAt<fuchsia_fs_startup::Startup>(exposed_dir);
  if (startup_client_end.is_error())
    return startup_client_end.take_error();
  fidl::WireSyncClient startup_client{std::move(*startup_client_end)};

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

zx::status<fidl::ClientEnd<Directory>> InitFsComponent(zx::channel device, DiskFormat df,
                                                       const MountOptions& options) {
  std::string_view url =
      options.component_url.empty() ? DiskFormatComponentUrl(df) : options.component_url;
  auto exposed_dir_or =
      ConnectFsComponent(url, *options.component_child_name, options.component_collection_name);
  if (exposed_dir_or.is_error())
    return exposed_dir_or.take_error();
  zx::status<> start_status = StartFsComponent(*exposed_dir_or, std::move(device), options);
  if (start_status.is_error()) {
    if (options.component_collection_name) {
      // If we hit an error starting, destroy the component instance. It may have been left in a
      // partially initialized state. We purposely ignore the result of destruction; it probably
      // won't fail, but if it does there is nothing we can really do, and the start error is
      // more important.
      [[maybe_unused]] auto result =
          DestroyFsComponent(*options.component_child_name, *options.component_collection_name);
    }
    return start_status.take_error();
  }
  return exposed_dir_or;
}

bool IsMultiVolume(DiskFormat df) { return df == kDiskFormatFxfs; }

std::string StripTrailingSlash(const char* in) {
  if (!in)
    return std::string();
  std::string_view view(in, strlen(in));
  if (!view.empty() && view.back() == '/') {
    return std::string(view.substr(0, view.length() - 1));
  } else {
    return std::string(view);
  }
}

}  // namespace

__EXPORT zx::status<NamespaceBinding> NamespaceBinding::Create(
    const char* path, fidl::ClientEnd<fuchsia_io::Directory> dir) {
  auto stripped_path = StripTrailingSlash(path);
  if (stripped_path.empty()) {
    return zx::ok(NamespaceBinding());
  }
  fdio_ns_t* ns;
  if (zx_status_t status = fdio_ns_get_installed(&ns); status != ZX_OK)
    return zx::error(status);
  if (zx_status_t status = fdio_ns_bind(ns, stripped_path.c_str(), dir.TakeHandle().release());
      status != ZX_OK)
    return zx::error(status);
  return zx::ok(NamespaceBinding(std::move(stripped_path)));
}

__EXPORT void NamespaceBinding::Reset() {
  if (!path_.empty()) {
    fdio_ns_t* ns;
    if (fdio_ns_get_installed(&ns) == ZX_OK)
      fdio_ns_unbind(ns, path_.c_str());
    path_.clear();
  }
}

__EXPORT NamespaceBinding::~NamespaceBinding() { Reset(); }

__EXPORT
StartedSingleVolumeFilesystem::~StartedSingleVolumeFilesystem() {
  [[maybe_unused]] auto res = Unmount();
}

__EXPORT
fidl::ClientEnd<fuchsia_io::Directory> StartedSingleVolumeFilesystem::Release() {
  return fidl::ClientEnd<fuchsia_io::Directory>(export_root_.TakeChannel());
}

__EXPORT
zx::status<> StartedSingleVolumeFilesystem::Unmount() {
  auto res = Shutdown(ExportRoot());
  export_root_.reset();
  return res;
}

__EXPORT
zx::status<fidl::ClientEnd<fuchsia_io::Directory>> StartedSingleVolumeFilesystem::DataRoot() const {
  return FsRootHandle(export_root_);
}

__EXPORT
fidl::ClientEnd<fuchsia_io::Directory> MountedVolume::Release() {
  return fidl::ClientEnd<fuchsia_io::Directory>(export_root_.TakeChannel());
}

__EXPORT
zx::status<fidl::ClientEnd<fuchsia_io::Directory>> MountedVolume::DataRoot() const {
  return FsRootHandle(export_root_);
}

__EXPORT
StartedMultiVolumeFilesystem::~StartedMultiVolumeFilesystem() {
  [[maybe_unused]] auto res = Unmount();
}

__EXPORT
std::pair<fidl::ClientEnd<fuchsia_io::Directory>,
          std::map<std::string, fidl::ClientEnd<fuchsia_io::Directory>>>
StartedMultiVolumeFilesystem::Release() {
  std::map<std::string, fidl::ClientEnd<fuchsia_io::Directory>> volumes;
  for (auto&& [k, v] : std::move(volumes_)) {
    volumes.insert(std::make_pair(k, v.Release()));
  }
  return std::make_pair(fidl::ClientEnd<fuchsia_io::Directory>(exposed_dir_.TakeChannel()),
                        std::move(volumes));
}

__EXPORT
zx::status<> StartedMultiVolumeFilesystem::Unmount() {
  volumes_.clear();
  auto res = Shutdown(exposed_dir_);
  exposed_dir_.reset();
  return res;
}

__EXPORT
zx::status<MountedVolume*> StartedMultiVolumeFilesystem::OpenVolume(std::string_view name,
                                                                    zx::channel crypt_client) {
  if (volumes_.find(name) != volumes_.end()) {
    return zx::error(ZX_ERR_ALREADY_BOUND);
  }
  auto endpoints_or = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints_or.is_error())
    return endpoints_or.take_error();
  auto [client, server] = std::move(*endpoints_or);
  auto res =
      fs_management::OpenVolume(exposed_dir_, name, std::move(server), std::move(crypt_client));
  if (res.is_error()) {
    return res.take_error();
  }
  auto [iter, inserted] = volumes_.emplace(std::string(name), MountedVolume(std::move(client)));
  return zx::ok(&iter->second);
}

__EXPORT
zx::status<MountedVolume*> StartedMultiVolumeFilesystem::CreateVolume(std::string_view name,
                                                                      zx::channel crypt_client) {
  if (volumes_.find(name) != volumes_.end()) {
    return zx::error(ZX_ERR_ALREADY_BOUND);
  }
  auto endpoints_or = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints_or.is_error())
    return endpoints_or.take_error();
  auto [client, server] = std::move(*endpoints_or);
  auto res =
      fs_management::CreateVolume(exposed_dir_, name, std::move(server), std::move(crypt_client));
  if (res.is_error()) {
    return res.take_error();
  }
  auto [iter, inserted] = volumes_.emplace(std::string(name), MountedVolume(std::move(client)));
  return zx::ok(&iter->second);
}

__EXPORT zx::status<> StartedMultiVolumeFilesystem::CheckVolume(std::string_view volume_name,
                                                                zx::channel crypt_client) {
  return fs_management::CheckVolume(exposed_dir_, volume_name, std::move(crypt_client));
}

__EXPORT
StartedSingleVolumeMultiVolumeFilesystem::~StartedSingleVolumeMultiVolumeFilesystem() {
  [[maybe_unused]] auto res = Unmount();
}

__EXPORT
fidl::ClientEnd<fuchsia_io::Directory> StartedSingleVolumeMultiVolumeFilesystem::Release() {
  volume_.reset();
  return fidl::ClientEnd<fuchsia_io::Directory>(exposed_dir_.TakeChannel());
}

__EXPORT
zx::status<> StartedSingleVolumeMultiVolumeFilesystem::Unmount() {
  volume_.reset();
  auto res = Shutdown(exposed_dir_);
  exposed_dir_.reset();
  return res;
}

__EXPORT SingleVolumeFilesystemInterface::~SingleVolumeFilesystemInterface() = default;

__EXPORT
zx::status<StartedSingleVolumeFilesystem> Mount(fbl::unique_fd device_fd, DiskFormat df,
                                                const MountOptions& options, LaunchCallback cb) {
  if (IsMultiVolume(df)) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // get the device handle from the device_fd
  zx_status_t status;
  zx::channel device;
  status = fdio_get_service_handle(device_fd.release(), device.reset_and_get_address());
  if (status != ZX_OK) {
    return zx::error(status);
  }

  StartedSingleVolumeFilesystem fs;
  if (options.component_child_name) {
    // Componentized filesystem
    auto exposed_dir = InitFsComponent(std::move(device), df, options);
    if (exposed_dir.is_error()) {
      return exposed_dir.take_error();
    }
    fs = StartedSingleVolumeFilesystem(std::move(*exposed_dir));
  } else {
    // Native filesystem
    std::string binary = DiskFormatBinaryPath(df);
    if (binary.empty()) {
      return zx::error(ZX_ERR_NOT_SUPPORTED);
    }
    auto export_root = InitNativeFs(binary.c_str(), std::move(device), options, cb);
    if (export_root.is_error()) {
      return export_root.take_error();
    }
    fs = StartedSingleVolumeFilesystem(std::move(*export_root));
  }

  return zx::ok(std::move(fs));
}

__EXPORT
zx::status<StartedMultiVolumeFilesystem> MountMultiVolume(fbl::unique_fd device_fd, DiskFormat df,
                                                          const MountOptions& options,
                                                          LaunchCallback cb) {
  if (!IsMultiVolume(df)) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // get the device handle from the device_fd
  zx_status_t status;
  zx::channel device;
  status = fdio_get_service_handle(device_fd.release(), device.reset_and_get_address());
  if (status != ZX_OK) {
    return zx::error(status);
  }

  auto outgoing_dir = InitFsComponent(std::move(device), df, options);
  if (outgoing_dir.is_error()) {
    return outgoing_dir.take_error();
  }
  return zx::ok(StartedMultiVolumeFilesystem(std::move(*outgoing_dir)));
}

__EXPORT
zx::status<StartedSingleVolumeMultiVolumeFilesystem> MountMultiVolumeWithDefault(
    fbl::unique_fd device_fd, DiskFormat df, const MountOptions& options, LaunchCallback cb,
    const char* volume_name) {
  if (!IsMultiVolume(df)) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // get the device handle from the device_fd
  zx_status_t status;
  zx::channel device;
  status = fdio_get_service_handle(device_fd.release(), device.reset_and_get_address());
  if (status != ZX_OK) {
    return zx::error(status);
  }

  auto outgoing_dir_or = InitFsComponent(std::move(device), df, options);
  if (outgoing_dir_or.is_error()) {
    return outgoing_dir_or.take_error();
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }
  auto [client, server] = std::move(*endpoints);

  auto volume = OpenVolume(*outgoing_dir_or, volume_name, std::move(server),
                           options.crypt_client ? options.crypt_client() : zx::channel{});

  if (volume.is_error()) {
    std::cerr << "Volume status " << volume.status_string() << std::endl;
    return volume.take_error();
  }

  return zx::ok(StartedSingleVolumeMultiVolumeFilesystem(std::move(*outgoing_dir_or),
                                                         MountedVolume(std::move(client))));
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

}  // namespace fs_management

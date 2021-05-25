// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/package_resolver.h"

#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/pkg/llcpp/fidl.h>
#include <lib/fdio/directory.h>

#include <fbl/string_printf.h>

#include "src/devices/lib/log/log.h"
#include "src/lib/storage/vfs/cpp/vfs.h"

namespace fio = fuchsia_io;

namespace internal {

zx::status<std::unique_ptr<Driver>> PackageResolver::FetchDriver(const std::string& package_url) {
  component::FuchsiaPkgUrl parsed_url;
  if (!parsed_url.Parse(std::string(package_url))) {
    LOGF(ERROR, "Failed to parse package url: %s", package_url.data());
    return zx::error(ZX_ERR_INTERNAL);
  }

  auto package_dir_result = Resolve(parsed_url);
  if (!package_dir_result.is_ok()) {
    LOGF(ERROR, "Failed to resolve package url %s, err %d", parsed_url.ToString().c_str(),
         package_dir_result.status_value());
    return package_dir_result.take_error();
  }

  auto driver_vmo_result = LoadDriver(&package_dir_result.value(), parsed_url);
  if (driver_vmo_result.status_value()) {
    return driver_vmo_result.take_error();
  }

  Driver* driver = nullptr;
  DriverLoadCallback callback = [&driver](Driver* d, const char* version) mutable { driver = d; };

  zx_status_t status = load_driver_vmo(boot_args_, std::string_view(parsed_url.resource_path()),
                                       std::move(driver_vmo_result.value()), std::move(callback));
  if (status != ZX_OK) {
    return zx::error(status);
  }

  fbl::unique_fd package_dir_fd;
  status = fdio_fd_create(package_dir_result.value().client_end().TakeChannel().release(),
                          package_dir_fd.reset_and_get_address());
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to create package_dir_fd: %sd", zx_status_get_string(status));
    return zx::error(status);
  }
  driver->package_dir = std::move(package_dir_fd);
  return zx::ok(std::unique_ptr<Driver>(driver));
}

zx_status_t PackageResolver::ConnectToResolverService() {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0u, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  const auto path =
      fbl::StringPrintf("/svc/%s", fidl::DiscoverableProtocolName<fuchsia_pkg::PackageResolver>);
  status = fdio_service_connect(path.c_str(), remote.release());
  if (status != ZX_OK) {
    return status;
  }
  resolver_client_ = fidl::WireSyncClient<fuchsia_pkg::PackageResolver>(std::move(local));
  return ZX_OK;
}

zx::status<fidl::WireSyncClient<fio::Directory>> PackageResolver::Resolve(
    const component::FuchsiaPkgUrl& package_url) {
  if (!resolver_client_.channel().is_valid()) {
    zx_status_t status = ConnectToResolverService();
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to connect to package resolver service");
      return zx::error(status);
    }
  }
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0u, &local, &remote);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  ::fidl::VectorView<::fidl::StringView> selectors;

  // This is synchronous for now so we can get the proof of concept working.
  // Eventually we will want to do this asynchronously.
  auto result = resolver_client_.Resolve(
      ::fidl::StringView(fidl::StringView::FromExternal(package_url.package_path())),
      std::move(selectors), std::move(remote));
  if (!result.ok() || result.Unwrap()->result.is_err()) {
    LOGF(ERROR, "Failed to resolve package");
    if (!result.ok()) {
      return zx::error(ZX_ERR_INTERNAL);
    } else {
      switch (result.Unwrap()->result.err()) {
        case fuchsia_pkg::wire::ResolveError::kIo:
          return zx::error(ZX_ERR_IO);
        case fuchsia_pkg::wire::ResolveError::kAccessDenied:
          return zx::error(ZX_ERR_ACCESS_DENIED);
        case fuchsia_pkg::wire::ResolveError::kRepoNotFound:
          return zx::error(ZX_ERR_NOT_FOUND);
        case fuchsia_pkg::wire::ResolveError::kPackageNotFound:
          return zx::error(ZX_ERR_NOT_FOUND);
        case fuchsia_pkg::wire::ResolveError::kUnavailableBlob:
          return zx::error(ZX_ERR_UNAVAILABLE);
        case fuchsia_pkg::wire::ResolveError::kInvalidUrl:
          return zx::error(ZX_ERR_INVALID_ARGS);
        case fuchsia_pkg::wire::ResolveError::kNoSpace:
          return zx::error(ZX_ERR_NO_SPACE);
        default:
          return zx::error(ZX_ERR_INTERNAL);
      }
    }
  }
  return zx::ok(fidl::WireSyncClient<fio::Directory>(std::move(local)));
}

zx::status<zx::vmo> PackageResolver::LoadDriver(
    fidl::WireSyncClient<fuchsia_io::Directory>* package_dir,
    const component::FuchsiaPkgUrl& package_url) {
  const uint32_t kFileRights = fio::wire::kOpenRightReadable | fio::wire::kOpenRightExecutable;
  const uint32_t kDriverVmoFlags = fio::wire::kVmoFlagRead | fio::wire::kVmoFlagExec;

  // Open and duplicate the driver vmo.
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0u, &local, &remote);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  auto file_open_result = package_dir->Open(
      kFileRights, 0u /* mode */,
      ::fidl::StringView(fidl::StringView::FromExternal(package_url.resource_path())),
      std::move(remote));
  if (!file_open_result.ok()) {
    LOGF(ERROR, "Failed to open driver file: %s", package_url.resource_path().c_str());
    return zx::error(ZX_ERR_INTERNAL);
  }

  fidl::WireSyncClient<fio::File> file_client(std::move(local));
  auto file_res = file_client.GetBuffer(kDriverVmoFlags);
  if (!file_res.ok() || file_res.Unwrap()->s != ZX_OK) {
    LOGF(ERROR, "Failed to get driver vmo");
    return zx::error(ZX_ERR_INTERNAL);
  }
  auto buf = file_res.Unwrap()->buffer.get();
  if (!buf->vmo.is_valid()) {
    return zx::error(ZX_ERR_INTERNAL);
  }
  return zx::ok(std::move(buf->vmo));
}

}  // namespace internal

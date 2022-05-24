// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/package_resolver.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.pkg/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/service/llcpp/service.h>

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

  auto driver_vmo_result = LoadDriver(package_dir_result.value(), parsed_url);
  if (driver_vmo_result.status_value()) {
    return driver_vmo_result.take_error();
  }

  Driver* driver = nullptr;
  DriverLoadCallback callback = [&driver](Driver* d, const char* version) mutable { driver = d; };

  zx_status_t status = load_driver_vmo(boot_args_, std::string_view(parsed_url.ToString()),
                                       std::move(driver_vmo_result.value()), std::move(callback));
  if (status != ZX_OK) {
    return zx::error(status);
  }

  fbl::unique_fd package_dir_fd;
  status = fdio_fd_create(package_dir_result.value().TakeClientEnd().TakeChannel().release(),
                          package_dir_fd.reset_and_get_address());
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to create package_dir_fd: %sd", zx_status_get_string(status));
    return zx::error(status);
  }
  driver->package_dir = std::move(package_dir_fd);
  return zx::ok(std::unique_ptr<Driver>(driver));
}

zx_status_t PackageResolver::ConnectToResolverService() {
  auto client_end = service::Connect<fuchsia_pkg::PackageResolver>();
  if (client_end.is_error()) {
    return client_end.error_value();
  }
  resolver_client_ = fidl::BindSyncClient(std::move(*client_end));
  return ZX_OK;
}

zx::status<fidl::WireSyncClient<fio::Directory>> PackageResolver::Resolve(
    const component::FuchsiaPkgUrl& package_url) {
  if (!resolver_client_.is_valid()) {
    zx_status_t status = ConnectToResolverService();
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to connect to package resolver service");
      return zx::error(status);
    }
  }
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }

  // This is synchronous for now so we can get the proof of concept working.
  // Eventually we will want to do this asynchronously.
  auto result = resolver_client_->Resolve(
      ::fidl::StringView(fidl::StringView::FromExternal(package_url.package_path())),
      std::move(endpoints->server));
  if (!result.ok() || result.Unwrap_NEW()->is_error()) {
    LOGF(ERROR, "Failed to resolve package");
    if (!result.ok()) {
      return zx::error(ZX_ERR_INTERNAL);
    } else {
      switch (result.Unwrap_NEW()->error_value()) {
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
  return zx::ok(fidl::BindSyncClient(std::move(endpoints->client)));
}

zx::status<zx::vmo> PackageResolver::LoadDriver(
    const fidl::WireSyncClient<fuchsia_io::Directory>& package_dir,
    const component::FuchsiaPkgUrl& package_url) {
  const fio::wire::OpenFlags kFileRights =
      fio::wire::OpenFlags::kRightReadable | fio::wire::OpenFlags::kRightExecutable;
  const fio::wire::VmoFlags kDriverVmoFlags =
      fio::wire::VmoFlags::kRead | fio::wire::VmoFlags::kExecute;

  // Open and duplicate the driver vmo.
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::File>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }
  auto file_open_result = package_dir->Open(
      kFileRights, 0u /* mode */,
      ::fidl::StringView(fidl::StringView::FromExternal(package_url.resource_path())),
      fidl::ServerEnd<fuchsia_io::Node>(endpoints->server.TakeChannel()));
  if (!file_open_result.ok()) {
    LOGF(ERROR, "Failed to open driver file: %s", package_url.resource_path().c_str());
    return zx::error(ZX_ERR_INTERNAL);
  }

  auto file_client = fidl::BindSyncClient(std::move(endpoints->client));
  fidl::WireResult file_res = file_client->GetBackingMemory(kDriverVmoFlags);
  if (!file_res.ok()) {
    LOGF(ERROR, "Failed to get driver vmo: %s", file_res.FormatDescription().c_str());
    return zx::error(ZX_ERR_INTERNAL);
  }
  const auto* res = file_res.Unwrap_NEW();
  if (res->is_error()) {
    LOGF(ERROR, "Failed to get driver vmo: %s", zx_status_get_string(res->error_value()));
    return zx::error(ZX_ERR_INTERNAL);
  }
  return zx::ok(std::move(res->value()->vmo));
}

}  // namespace internal

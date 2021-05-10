// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/package_resolver.h"

#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/pkg/llcpp/fidl.h>
#include <lib/fdio/directory.h>

#include <fbl/string_printf.h>

#include "src/devices/lib/log/log.h"
#include "src/lib/pkg_url/fuchsia_pkg_url.h"
#include "src/lib/storage/vfs/cpp/vfs.h"

namespace fio = fuchsia_io;

namespace internal {

zx::status<PackageResolver::FetchDriverVmoResult> PackageResolver::FetchDriverVmo(
    const std::string& package_url) {
  if (!resolver_client_.channel().is_valid()) {
    zx_status_t status = ConnectToResolverService();
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to connect to package resolver service");
      return zx::error(status);
    }
  }
  auto result = Resolve(package_url);
  if (!result.is_ok()) {
    LOGF(ERROR, "Failed to resolve package url %s, err %d", package_url.c_str(),
         result.status_value());
    return zx::error(result.status_value());
  }

  return LoadDriverPackage(&result.value() /* package_dir */);
}

zx::status<std::unique_ptr<Driver>> PackageResolver::FetchDriver(const std::string& package_url) {
  auto result = FetchDriverVmo(package_url);
  if (result.is_error()) {
    return result.take_error();
  }
  Driver* driver = nullptr;
  DriverLoadCallback callback = [&driver](Driver* d, const char* version) mutable { driver = d; };

  zx_status_t status = load_driver_vmo(boot_args_, result.value().libname,
                                       std::move(result.value().vmo), std::move(callback));

  if (status != ZX_OK) {
    return zx::error(status);
  }
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
    const std::string_view& package_url) {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0u, &local, &remote);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  component::FuchsiaPkgUrl fp;
  bool parse = fp.Parse(std::string(package_url));
  if (!parse) {
    LOGF(ERROR, "Failed to parse package url: %s", package_url.data());
    return zx::error(ZX_ERR_INTERNAL);
  }
  ::fidl::VectorView<::fidl::StringView> selectors;

  // This is synchronous for now so we can get the proof of concept working.
  // Eventually we will want to do this asynchronously.
  auto result = resolver_client_.Resolve(
      ::fidl::StringView(fidl::StringView::FromExternal(fp.package_path())), std::move(selectors),
      std::move(remote));
  if (!result.ok() || result.Unwrap()->result.is_err()) {
    LOGF(ERROR, "Failed to resolve package");
    return zx::error(!result.ok() ? ZX_ERR_INTERNAL : result.Unwrap()->result.err());
  }
  return zx::ok(fidl::WireSyncClient<fio::Directory>(std::move(local)));
}

zx::status<PackageResolver::FetchDriverVmoResult> PackageResolver::LoadDriverPackage(
    fidl::WireSyncClient<fio::Directory>* package_dir) {
  const uint32_t kFileRights = fio::wire::kOpenRightReadable | fio::wire::kOpenRightExecutable;
  const uint32_t kDriverVmoFlags = fio::wire::kVmoFlagRead | fio::wire::kVmoFlagExec;
  constexpr char kLibDir[] = "lib";

  // Open the lib directory.
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0u, &local, &remote);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  auto open_result = package_dir->Open(kFileRights, 0u, kLibDir, std::move(remote));
  if (!open_result.ok()) {
    LOGF(ERROR, "Failed to open driver package directory");
    return zx::error(ZX_ERR_INTERNAL);
  }
  // This could be simplified by using POSIX APIs instead.
  fidl::WireSyncClient<fio::Directory> lib_dir(std::move(local));

  auto libname_result = GetDriverLibname(&lib_dir);
  if (!libname_result.is_ok()) {
    LOGF(ERROR, "Failed to get driver libname");
    return zx::error(libname_result.status_value());
  }

  // Open and duplicate the driver vmo.
  status = zx::channel::create(0u, &local, &remote);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  auto file_open_result =
      lib_dir.Open(kFileRights, 0u /* mode */,
                   ::fidl::StringView(fidl::StringView::FromExternal(libname_result.value())),
                   std::move(remote));
  if (!file_open_result.ok()) {
    LOGF(ERROR, "Failed to open driver file: %s", libname_result.value().c_str());
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
  return zx::ok(PackageResolver::FetchDriverVmoResult{libname_result.value(), std::move(buf->vmo)});
}

zx::status<std::string> PackageResolver::GetDriverLibname(
    fidl::WireSyncClient<fio::Directory>* lib_dir) {
  auto dirent_result = lib_dir->ReadDirents(fio::wire::kMaxBuf);
  if (!dirent_result.ok() || dirent_result.status() != ZX_OK) {
    return zx::error(dirent_result.status());
  }
  fidl::WireResponse<fio::Directory::ReadDirents>* dirent_response = dirent_result.Unwrap();
  if (dirent_response->dirents.count() == 0) {
    return zx::error(ZX_ERR_INTERNAL);
  }

  size_t offset = 0;
  auto data_ptr = dirent_response->dirents.data();
  // For now just assume only one driver shared library is included in the package.
  while (sizeof(vdirent_t) < dirent_response->dirents.count() - offset) {
    const vdirent_t* entry = reinterpret_cast<const vdirent_t*>(data_ptr + offset);
    std::string entry_name(entry->name, entry->size);
    offset += sizeof(vdirent_t) + entry->size;
    if (entry_name == "." || entry_name == "..") {
      continue;
    }
    return zx::ok(std::move(entry_name));
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

}  // namespace internal

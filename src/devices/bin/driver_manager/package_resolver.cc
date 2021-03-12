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

zx_status_t PackageResolver::ConnectToResolverService() {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0u, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  const auto path = fbl::StringPrintf("/svc/%s", fuchsia_pkg::PackageResolver::Name);
  status = fdio_service_connect(path.c_str(), remote.release());
  if (status != ZX_OK) {
    return status;
  }
  resolver_client_ = fuchsia_pkg::PackageResolver::SyncClient(std::move(local));
  return ZX_OK;
}

zx::status<fio::Directory::SyncClient> PackageResolver::Resolve(
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
  auto result = resolver_client_.Resolve(::fidl::StringView(fidl::unowned_str(fp.package_path())),
                                         std::move(selectors), std::move(remote));
  if (!result.ok() || result.Unwrap()->result.is_err()) {
    LOGF(ERROR, "Failed to resolve package");
    return zx::error(!result.ok() ? ZX_ERR_INTERNAL : result.Unwrap()->result.err());
  }
  return zx::ok(fio::Directory::SyncClient(std::move(local)));
}

zx::status<PackageResolver::FetchDriverVmoResult> PackageResolver::LoadDriverPackage(
    fio::Directory::SyncClient* package_dir) {
  const uint32_t kFileRights = fio::wire::OPEN_RIGHT_READABLE | fio::wire::OPEN_RIGHT_EXECUTABLE;
  const uint32_t kDriverVmoFlags = fio::wire::VMO_FLAG_READ | fio::wire::VMO_FLAG_EXEC;
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
  fio::Directory::SyncClient lib_dir(std::move(local));

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
  auto file_open_result = lib_dir.Open(
      kFileRights, 0u /* mode */, ::fidl::StringView(fidl::unowned_str(libname_result.value())),
      std::move(remote));
  if (!file_open_result.ok()) {
    LOGF(ERROR, "Failed to open driver file: %s", libname_result.value().c_str());
    return zx::error(ZX_ERR_INTERNAL);
  }
  fio::File::SyncClient file_client(std::move(local));
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

zx::status<std::string> PackageResolver::GetDriverLibname(fio::Directory::SyncClient* lib_dir) {
  auto dirent_result = lib_dir->ReadDirents(fio::wire::MAX_BUF);
  if (!dirent_result.ok() || dirent_result.status() != ZX_OK) {
    return zx::error(dirent_result.status());
  }
  fio::Directory::ReadDirentsResponse* dirent_response = dirent_result.Unwrap();
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

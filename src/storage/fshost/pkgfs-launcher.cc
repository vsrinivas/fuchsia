// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/pkgfs-launcher.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <stdio.h>
#include <zircon/errors.h>
#include <zircon/processargs.h>

#include "src/storage/fshost/fdio.h"
#include "src/storage/fshost/filesystem-mounter.h"
#include "src/storage/fshost/fshost-fs-provider.h"
#include "src/storage/fshost/pkgfs-loader-service.h"

namespace fio = fuchsia_io;

namespace fshost {

zx::status<> LaunchPkgfs(FilesystemMounter* filesystems) {
  // Get the pkgfs.cmd boot argument
  auto cmd_status = filesystems->boot_args()->pkgfs_cmd();
  if (cmd_status.is_error()) {
    FX_LOGS(ERROR) << "unable to launch pkgfs, missing \"zircon.system.pkgfs.cmd\" boot argument";
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  const char* cmd = cmd_status.value().c_str();

  fbl::unique_fd blob_dir;
  auto status =
      zx::make_status(fdio_open_fd("/blob",
                                   static_cast<uint32_t>(fio::wire::OpenFlags::kRightReadable |
                                                         fio::wire::OpenFlags::kRightExecutable),
                                   blob_dir.reset_and_get_address()));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "fdio_open_fd(/blob) failed: " << status.status_string();
    return status;
  }

  auto args = ArgumentVector::FromCmdline(cmd);
  auto argv = args.argv();
  // Remove leading slashes before asking pkgfs_ldsvc_load_blob to load the
  // file.
  const char* file = argv[0];
  while (file[0] == '/') {
    ++file;
  }

  auto loader = PkgfsLoaderService::Create(std::move(blob_dir), filesystems->boot_args());
  auto executable = loader->LoadPkgfsFile(argv[0]);
  if (executable.is_error()) {
    FX_LOGS(ERROR) << "cannot load pkgfs executable: " << executable.status_string();
    return executable.take_error();
  }

  auto loader_conn = loader->Connect();
  if (loader_conn.is_error()) {
    FX_LOGS(ERROR) << "failed to connect to pkgfs loader: " << loader_conn.status_string();
    return loader_conn.take_error();
  }

  zx::status endpoints = fidl::CreateEndpoints<fio::Directory>();
  if (endpoints.is_error()) {
    FX_PLOGS(ERROR, endpoints.status_value()) << "cannot create pkgfs root channel";
    return endpoints.take_error();
  }

  constexpr size_t kHandles = 1;
  const zx_handle_t handles[kHandles] = {endpoints->server.TakeChannel().release()};
  const uint32_t handle_types[kHandles] = {PA_HND(PA_USER0, 0)};
  zx::process proc;
  FX_LOGS(INFO) << "starting " << args << "...";

  FshostFsProvider fs_provider;
  DevmgrLauncher launcher(&fs_provider);
  status = zx::make_status(
      launcher.LaunchWithLoader(*zx::job::default_job(), "pkgfs", std::move(executable).value(),
                                loader_conn->TakeChannel(), argv, nullptr, -1,
                                /* TODO(fxbug.dev/32044) */ zx::resource(), handles, handle_types,
                                kHandles, &proc, FS_DATA | FS_BLOB_EXEC | FS_SVC));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "failed to launch " << cmd << ": " << status.status_string();
    return status;
  }

  if (zx::status result = filesystems->InstallFs(FsManager::MountPoint::kPkgfs, {}, {},
                                                 std::move(endpoints->client));
      result.is_error()) {
    FX_PLOGS(ERROR, result.status_value()) << "failed to install /pkgfs";
    return result;
  }
  return zx::ok();
}

}  // namespace fshost

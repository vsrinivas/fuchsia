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

namespace {

zx::status<> FinishPkgfsLaunch(FilesystemMounter* filesystems, zx::channel pkgfs_root) {
  // re-export /pkgfs/system as /system
  zx::channel system_channel, system_req;
  zx_status_t status = zx::channel::create(0, &system_channel, &system_req);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  status = fdio_open_at(pkgfs_root.get(), "system", FS_READ_EXEC_DIR_FLAGS, system_req.release());
  if (status != ZX_OK) {
    return zx::error(status);
  }
  // re-export /pkgfs/packages/shell-commands/0/bin as /bin
  zx::channel bin_chan, bin_req;
  status = zx::channel::create(0, &bin_chan, &bin_req);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  status = fdio_open_at(pkgfs_root.get(), "packages/shell-commands/0/bin", FS_READ_EXEC_DIR_FLAGS,
                        bin_req.release());
  if (status != ZX_OK) {
    // non-fatal.
    FX_LOGS(ERROR) << "failed to install /bin (could not open shell-commands)";
  }
  status = filesystems->InstallFs(FsManager::MountPoint::kPkgfs, std::move(pkgfs_root));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to install /pkgfs";
    return zx::error(status);
  }
  status = filesystems->InstallFs(FsManager::MountPoint::kSystem, std::move(system_channel));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to install /system";
    return zx::error(status);
  }
  // as above, failure of /bin export is non-fatal.
  status = filesystems->InstallFs(FsManager::MountPoint::kBin, std::move(bin_chan));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to install /bin";
  }
  // start the delayed vfs
  filesystems->FuchsiaStart();
  return zx::ok();
}

}  // namespace

zx::status<> LaunchPkgfs(FilesystemMounter* filesystems) {
  // Get the pkgfs.cmd boot argument
  auto cmd_status = filesystems->boot_args()->pkgfs_cmd();
  if (cmd_status.is_error()) {
    FX_LOGS(ERROR) << "unable to launch pkgfs, missing \"zircon.system.pkgfs.cmd\" boot argument";
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  const char* cmd = cmd_status.value().c_str();

  fbl::unique_fd blob_dir;
  auto status = zx::make_status(
      fdio_open_fd("/fs/blob", fio::wire::kOpenRightReadable | fio::wire::kOpenRightExecutable,
                   blob_dir.reset_and_get_address()));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "fdio_open_fd(/fs/blob) failed: " << status.status_string();
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

  zx::channel h0, h1;
  status = zx::make_status(zx::channel::create(0, &h0, &h1));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "cannot create pkgfs root channel: " << status.status_string();
    return status;
  }

  const zx_handle_t handles[] = {h1.release()};
  const uint32_t handle_types[] = {PA_HND(PA_USER0, 0)};
  size_t hcount = sizeof(handles) / sizeof(*handles);
  zx::process proc;
  FX_LOGS(INFO) << "starting " << args << "...";

  FshostFsProvider fs_provider;
  DevmgrLauncher launcher(&fs_provider);
  status = zx::make_status(
      launcher.LaunchWithLoader(*zx::job::default_job(), "pkgfs", std::move(executable).value(),
                                loader_conn->TakeChannel(), argv, nullptr, -1,
                                /* TODO(fxbug.dev/32044) */ zx::resource(), handles, handle_types,
                                hcount, &proc, FS_DATA | FS_BLOB_EXEC | FS_SVC));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "failed to launch " << cmd << ": " << status.status_string();
    return status;
  }

  return FinishPkgfsLaunch(filesystems, std::move(h0));
}

}  // namespace fshost

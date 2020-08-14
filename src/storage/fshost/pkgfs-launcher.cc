// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pkgfs-launcher.h"

#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <inttypes.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <utility>

#include <fbl/unique_fd.h>
#include <fs-management/mount.h>
#include <loader-service/loader-service.h>

#include "fdio.h"
#include "fshost-boot-args.h"
#include "fshost-fs-provider.h"

namespace fio = ::llcpp::fuchsia::io;

namespace devmgr {
namespace {

typedef struct {
  fbl::unique_fd blobfs_root_fd;
  FshostBootArgs* boot_args;
} ldsvc_ctx_t;

void pkgfs_finish(FilesystemMounter* filesystems, zx::process proc, zx::channel pkgfs_root) {
  auto deadline = zx::deadline_after(zx::sec(20));
  zx_signals_t observed;
  zx_status_t status = proc.wait_one(ZX_USER_SIGNAL_0 | ZX_PROCESS_TERMINATED, deadline, &observed);
  if (status != ZX_OK) {
    printf("fshost: pkgfs did not signal completion: %d (%s)\n", status,
           zx_status_get_string(status));
    return;
  }
  if (!(observed & ZX_USER_SIGNAL_0)) {
    printf("fshost: pkgfs terminated prematurely\n");
    return;
  }
  // re-export /pkgfs/system as /system
  zx::channel system_channel, system_req;
  if (zx::channel::create(0, &system_channel, &system_req) != ZX_OK) {
    return;
  }
  if (fdio_open_at(pkgfs_root.get(), "system", FS_READ_EXEC_DIR_FLAGS, system_req.release()) !=
      ZX_OK) {
    return;
  }
  // re-export /pkgfs/packages/shell-commands/0/bin as /bin
  zx::channel bin_chan, bin_req;
  if (zx::channel::create(0, &bin_chan, &bin_req) != ZX_OK) {
    return;
  }
  if (fdio_open_at(pkgfs_root.get(), "packages/shell-commands/0/bin", FS_READ_EXEC_DIR_FLAGS,
                   bin_req.release()) != ZX_OK) {
    // non-fatal.
    printf("fshost: failed to install /bin (could not open shell-commands)\n");
  }
  if (filesystems->InstallFs("/pkgfs", std::move(pkgfs_root)) != ZX_OK) {
    printf("fshost: failed to install /pkgfs\n");
    return;
  }
  if (filesystems->InstallFs("/system", std::move(system_channel)) != ZX_OK) {
    printf("fshost: failed to install /system\n");
    return;
  }
  // as above, failure of /bin export is non-fatal.
  if (filesystems->InstallFs("/bin", std::move(bin_chan)) != ZX_OK) {
    printf("fshost: failed to install /bin\n");
  }
  // start the delayed vfs
  filesystems->FuchsiaStart();
}

// Launching pkgfs uses its own loader service and command lookup to run out of
// the blobfs without any real filesystem.  Files are found by
// boot_args->Get("zircon.system.pkgfs.file.PATH") returning a blob content ID.
// That is, a manifest of name->blob is embedded in /boot/config/devmgr.
zx_status_t pkgfs_ldsvc_load_blob(void* ctx, const char* prefix, const char* name,
                                  zx_handle_t* vmo) {
  const ldsvc_ctx_t* ldsvc_ctx = static_cast<ldsvc_ctx_t*>(ctx);
  auto blobp = ldsvc_ctx->boot_args->pkgfs_file_with_prefix_and_name(prefix, name);
  if (blobp == nullptr) {
    printf("fshost: failed to find pkgfs file ID in boot arguments \"%s%s\"\n", prefix, name);
    return ZX_ERR_NOT_FOUND;
  }
  const char* blob = blobp->data();

  int fd;
  zx_status_t status = fdio_open_fd_at(ldsvc_ctx->blobfs_root_fd.get(), blob,
                                       fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE, &fd);
  if (status != ZX_OK) {
    printf("fshost: failed to open pkgfs blob \"%s\": %d (%s)\n", blob, status,
           zx_status_get_string(status));
    return status;
  }

  zx::vmo exec_vmo;
  status = fdio_get_vmo_exec(fd, exec_vmo.reset_and_get_address());
  close(fd);
  if (status != ZX_OK) {
    printf("fshost: failed to get vmo for blob \"%s\" (%d)\n", blob, status);
    return status;
  }

  char key[256];
  if (snprintf(key, sizeof(key), "%s%s", prefix, name) >= (int)sizeof(key)) {
    printf("fshost: failed to format pkgfs file boot argument key\n");
    return ZX_ERR_INVALID_ARGS;
  }
  status = zx_object_set_property(exec_vmo.get(), ZX_PROP_NAME, key, strlen(key));
  if (status != ZX_OK) {
    printf("fshost: failed to set object property ZX_PROP_NAME to %s: %d (%s)\n", key, status,
           zx_status_get_string(status));
    return status;
  }

  *vmo = exec_vmo.release();
  return ZX_OK;
}

zx_status_t pkgfs_ldsvc_load_object(void* ctx, const char* name, zx_handle_t* vmo) {
  return pkgfs_ldsvc_load_blob(ctx, "lib/", name, vmo);
}

zx_status_t pkgfs_ldsvc_load_abspath(void* ctx, const char* name, zx_handle_t* vmo) {
  return pkgfs_ldsvc_load_blob(ctx, "", name + 1, vmo);
}

zx_status_t pkgfs_ldsvc_publish_data_sink(void* ctx, const char* name, zx_handle_t vmo) {
  zx_handle_close(vmo);
  return ZX_ERR_NOT_SUPPORTED;
}

void pkgfs_ldsvc_finalizer(void* ctx) { close(static_cast<int>(reinterpret_cast<intptr_t>(ctx))); }

const loader_service_ops_t pkgfs_ldsvc_ops = {
    .load_object = pkgfs_ldsvc_load_object,
    .load_abspath = pkgfs_ldsvc_load_abspath,
    .publish_data_sink = pkgfs_ldsvc_publish_data_sink,
    .finalizer = pkgfs_ldsvc_finalizer,
};

// Create a local loader service with a fixed mapping of names to blobs.
zx_status_t pkgfs_ldsvc_start(std::unique_ptr<ldsvc_ctx_t> ldsvc_ctx, zx::channel* ldsvc) {
  loader_service_t* service;
  // The loader service takes (figurative) ownership of ldsvc_ctx, which is why
  // it is intentionally leaked here. If the loader service is ever modified to
  // have a shorter lifespan this should be updated to free the ldsvc_ctx when
  // the loader service is terminated or this will cause a memory leak.
  zx_status_t status = loader_service_create(
      nullptr, &pkgfs_ldsvc_ops, reinterpret_cast<void*>(ldsvc_ctx.release()), &service);
  if (status != ZX_OK) {
    printf("fshost: cannot create pkgfs loader service: %d (%s)\n", status,
           zx_status_get_string(status));
    return status;
  }

  status = loader_service_connect(service, ldsvc->reset_and_get_address());
  loader_service_release(service);
  if (status != ZX_OK) {
    printf("fshost: cannot connect pkgfs loader service: %d (%s)\n", status,
           zx_status_get_string(status));
  }
  return status;
}

bool pkgfs_launch(FilesystemMounter* filesystems) {
  // Get the pkgfs.cmd boot argument
  auto cmdp = filesystems->boot_args()->pkgfs_cmd();
  if (cmdp == nullptr) {
    printf("fshost: unable to launch pkgfs, missing \"zircon.system.pkgfs.cmd\" boot argument\n");
    return false;
  }
  const char* cmd = cmdp->data();

  fbl::unique_fd fs_blob_fd(open("/fs/blob", O_RDONLY | O_DIRECTORY));
  if (!fs_blob_fd) {
    printf("fshost: open(/fs/blob): %m\n");
    return false;
  }

  zx::channel h0, h1;
  zx_status_t status = zx::channel::create(0, &h0, &h1);
  if (status != ZX_OK) {
    printf("fshost: cannot create pkgfs root channel: %d (%s)\n", status,
           zx_status_get_string(status));
    return false;
  }

  auto args = ArgumentVector::FromCmdline(cmd);
  auto argv = args.argv();
  // Remove leading slashes before asking pkgfs_ldsvc_load_blob to load the
  // file.
  const char* file = argv[0];
  while (file[0] == '/') {
    ++file;
  }
  // The ldsvc_ctx stores a pointer to filesystems->boot_args(), which will be
  // unsafe if this is ever modified such that the loader service will outlive
  // the FsManager.
  auto ldsvc_ctx = std::make_unique<ldsvc_ctx_t>();
  ldsvc_ctx->blobfs_root_fd = std::move(fs_blob_fd);
  ldsvc_ctx->boot_args = filesystems->boot_args();
  zx::vmo executable;
  status = pkgfs_ldsvc_load_blob(reinterpret_cast<void*>(ldsvc_ctx.get()), "", argv[0],
                                 executable.reset_and_get_address());
  if (status != ZX_OK) {
    printf("fshost: cannot load pkgfs executable: %d (%s)\n", status, zx_status_get_string(status));
    return false;
  }

  zx::channel loader;
  status = pkgfs_ldsvc_start(std::move(ldsvc_ctx), &loader);
  if (status != ZX_OK) {
    printf("fshost: cannot pkgfs loader: %d (%s)\n", status, zx_status_get_string(status));
    return false;
  }

  const zx_handle_t raw_h1 = h1.release();
  zx::process proc;
  args.Print("fshost");

  FshostFsProvider fs_provider;
  DevmgrLauncher launcher(&fs_provider);
  status = launcher.LaunchWithLoader(
      *zx::job::default_job(), "pkgfs", std::move(executable), std::move(loader), argv, nullptr, -1,
      /* TODO(fxbug.dev/32044) */ zx::resource(), &raw_h1, (const uint32_t[]){PA_HND(PA_USER0, 0)}, 1,
      &proc, FS_DATA | FS_BLOB_EXEC | FS_SVC);
  if (status != ZX_OK) {
    printf("fshost: failed to launch %s: %d (%s)\n", cmd, status, zx_status_get_string(status));
    return false;
  }

  pkgfs_finish(filesystems, std::move(proc), std::move(h0));
  return true;
}

}  // namespace

void LaunchPkgfs(FilesystemMounter* filesystems) { pkgfs_launch(filesystems); }

}  // namespace devmgr

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fs-manager.h"

#include <fcntl.h>
#include <fuchsia/fshost/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/zircon-internal/debug.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <iterator>
#include <memory>
#include <string_view>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fs/remote_dir.h>
#include <fs/vfs.h>
#include <fs/vfs_types.h>

#include "admin-server.h"
#include "block-watcher.h"
#include "fshost-boot-args.h"
#include "lib/async/cpp/task.h"
#include "lifecycle.h"
#include "metrics.h"

#define ZXDEBUG 0

namespace devmgr {

FsManager::FsManager(FsHostMetrics metrics)
    : global_loop_(new async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread)),
      outgoing_vfs_(fs::ManagedVfs(global_loop_->dispatcher())),
      registry_(global_loop_.get()),
      metrics_(std::move(metrics)) {
  ZX_ASSERT(global_root_ == nullptr);
}

// In the event that we haven't been explicitly signalled, tear ourself down.
FsManager::~FsManager() {
  if (global_shutdown_.has_handler()) {
    event_.signal(0, FSHOST_SIGNAL_EXIT);
    auto deadline = zx::deadline_after(zx::sec(2));
    zx_signals_t pending;
    event_.wait_one(FSHOST_SIGNAL_EXIT_DONE, deadline, &pending);
  }
  // Ensure all asynchronous work on global_loop_ finishes.
  // Some of the asynchronous work references memory owned by this instance, so we need to ensure
  // the work is complete before destruction.
  // TODO(sdemos): Clean up ordering of fields to let the natural destructor ordering handle
  // shutdown.
  global_loop_->Shutdown();
}

zx_status_t FsManager::Create(std::shared_ptr<loader::LoaderServiceBase> loader,
                              zx::channel dir_request, zx::channel lifecycle_request,
                              FsHostMetrics metrics, std::unique_ptr<FsManager>* out) {
  auto fs_manager = std::unique_ptr<FsManager>(new FsManager(std::move(metrics)));
  zx_status_t status = fs_manager->Initialize();
  if (status != ZX_OK) {
    return status;
  }
  if (dir_request.is_valid()) {
    status = fs_manager->SetupOutgoingDirectory(std::move(dir_request), loader);
    if (status != ZX_OK) {
      return status;
    }
  }
  if (lifecycle_request.is_valid()) {
    status = fs_manager->SetupLifecycleServer(std::move(lifecycle_request));
    if (status != ZX_OK) {
      return status;
    }
  }
  *out = std::move(fs_manager);
  return ZX_OK;
}

zx_status_t FsManager::SetupLifecycleServer(zx::channel lifecycle_request) {
  return devmgr::LifecycleServer::Create(global_loop_->dispatcher(), this,
                                         std::move(lifecycle_request));
}

// Sets up the outgoing directory, and runs it on the PA_DIRECTORY_REQUEST
// handle if it exists. See fshost.cml for a list of what's in the directory.
zx_status_t FsManager::SetupOutgoingDirectory(zx::channel dir_request,
                                              std::shared_ptr<loader::LoaderServiceBase> loader) {
  auto outgoing_dir = fbl::MakeRefCounted<fs::PseudoDir>();

  // TODO: fshost exposes two separate service directories, one here and one in
  // the registry vfs that's mounted under fs-manager-svc further down in this
  // function. These should be combined by either pulling the registry services
  // into this VFS or by pushing the services in this directory into the
  // registry.

  // Add loader and admin services to the vfs
  auto svc_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  // This service name is breaking the convention whereby the directory entry
  // name matches the protocol name. This is an implementation of
  // fuchsia.ldsvc.Loader, and is renamed to make it easier to identify that
  // this implementation comes from fshost.
  svc_dir->AddEntry(
      "fuchsia.fshost.Loader", fbl::MakeRefCounted<fs::Service>([loader](zx::channel chan) {
        auto status = loader->Bind(std::move(chan));
        if (status.is_error()) {
          fprintf(stderr, "fshost: failed to attach loader service: %s\n", status.status_string());
        }
        return status.status_value();
      }));
  svc_dir->AddEntry(llcpp::fuchsia::fshost::Admin::Name,
                    AdminServer::Create(this, global_loop_->dispatcher()));

  svc_dir->AddEntry(llcpp::fuchsia::fshost::BlockWatcher::Name,
                    BlockWatcherServer::Create(this, global_loop_->dispatcher()));

  outgoing_dir->AddEntry("svc", std::move(svc_dir));

  // Add /fs to the outgoing vfs
  zx::channel filesystems_client, filesystems_server;
  zx_status_t status = zx::channel::create(0, &filesystems_client, &filesystems_server);
  if (status != ZX_OK) {
    printf("fshost: failed to create channel\n");
    return status;
  }
  status = this->ServeRoot(std::move(filesystems_server));
  if (status != ZX_OK) {
    printf("fshost: Cannot serve root filesystem\n");
    return status;
  }
  outgoing_dir->AddEntry("fs", fbl::MakeRefCounted<fs::RemoteDir>(std::move(filesystems_client)));

  // Add /fs-manager-svc to the vfs
  zx::channel services_client, services_server;
  status = zx::channel::create(0, &services_client, &services_server);
  if (status != ZX_OK) {
    printf("fshost: failed to create channel\n");
    return status;
  }
  status = this->ServeFshostRoot(std::move(services_server));
  if (status != ZX_OK) {
    printf("fshost: Cannot serve export directory\n");
    return status;
  }
  outgoing_dir->AddEntry("fs-manager-svc",
                         fbl::MakeRefCounted<fs::RemoteDir>(std::move(services_client)));

  // TODO(fxbug.dev/39588): delete this
  // Add the delayed directory
  zx::channel filesystems_client_2, filesystems_server_2;
  status = zx::channel::create(0, &filesystems_client_2, &filesystems_server_2);
  if (status != ZX_OK) {
    printf("fshost: failed to create channel\n");
    return status;
  }
  status = this->ServeRoot(std::move(filesystems_server_2));
  if (status != ZX_OK) {
    printf("fshost: cannot serve root filesystem\n");
    return status;
  }
  outgoing_dir->AddEntry("delayed", delayed_outdir_.Initialize(std::move(filesystems_client_2)));

  // Add the diagnostics directory
  diagnostics_dir_ = inspect_.Initialize(global_loop_->dispatcher());
  outgoing_dir->AddEntry("diagnostics", diagnostics_dir_);

  // Run the outgoing directory
  outgoing_vfs_.ServeDirectory(outgoing_dir, std::move(dir_request));
  return ZX_OK;
}

zx_status_t FsManager::Initialize() {
  zx_status_t status = memfs::Vfs::Create("<root>", &root_vfs_, &global_root_);
  if (status != ZX_OK) {
    return status;
  }

  fbl::RefPtr<fs::Vnode> vn;
  if ((status = global_root_->Create(&vn, "boot", S_IFDIR)) != ZX_OK) {
    return status;
  }
  if ((status = global_root_->Create(&vn, "tmp", S_IFDIR)) != ZX_OK) {
    return status;
  }
  for (unsigned n = 0; n < std::size(kMountPoints); n++) {
    auto open_result = root_vfs_->Open(global_root_, std::string_view(kMountPoints[n]),
                                       fs::VnodeConnectionOptions::ReadWrite().set_create(),
                                       fs::Rights::ReadWrite(), S_IFDIR);
    if (open_result.is_error()) {
      return open_result.error();
    }

    ZX_ASSERT(open_result.is_ok());
    mount_nodes[n] = std::move(open_result.ok().vnode);
  }

  auto open_result =
      root_vfs_->Open(global_root_, std::string_view("/data"),
                      fs::VnodeConnectionOptions::ReadOnly(), fs::Rights::ReadOnly(), S_IFDIR);
  if (open_result.is_ok()) {
    inspect_.ServeStats("data", open_result.ok().vnode);
  } else {
    fprintf(stderr, "fshost: failed to serve /data stats");
  }

  status = zx::event::create(0, &event_);
  if (status != ZX_OK) {
    fprintf(stderr, "fshost: failed to create fs-manager event: %d\n", status);
    return status;
  }

  global_loop_->StartThread("root-dispatcher");
  root_vfs_->SetDispatcher(global_loop_->dispatcher());
  return ZX_OK;
}

void FsManager::FlushMetrics() { mutable_metrics()->FlushUntilSuccess(global_loop_->dispatcher()); }

zx_status_t FsManager::InstallFs(const char* path, zx::channel h) {
  for (unsigned n = 0; n < std::size(kMountPoints); n++) {
    if (!strcmp(path, kMountPoints[n])) {
      return root_vfs_->InstallRemote(mount_nodes[n], fs::MountChannel(std::move(h)));
    }
  }
  return ZX_ERR_NOT_FOUND;
}

zx_status_t FsManager::ServeRoot(zx::channel server) {
  fs::Rights rights;
  rights.read = true;
  rights.write = true;
  rights.admin = true;
  rights.execute = true;
  return root_vfs_->ServeDirectory(global_root_, std::move(server), rights);
}

void FsManager::WatchExit() {
  printf("fshost: watching for exit\n");
  global_shutdown_.set_handler([this](async_dispatcher_t* dispatcher, async::Wait* wait,
                                      zx_status_t status, const zx_packet_signal_t* signal) {
    printf("fshost: exit signal detected\n");
    root_vfs_->UninstallAll(zx::time::infinite());
    event_.signal(0, FSHOST_SIGNAL_EXIT_DONE);
  });

  global_shutdown_.set_object(event_.get());
  global_shutdown_.set_trigger(FSHOST_SIGNAL_EXIT);
  global_shutdown_.Begin(global_loop_->dispatcher());
}

void FsManager::Shutdown(fit::function<void(zx_status_t)> callback) {
  zx_status_t status = event_.signal(0, FSHOST_SIGNAL_EXIT);
  if (status != ZX_OK) {
    printf("fshost: error signalling event: %s\n", zx_status_get_string(status));
    return;
  }

  shutdown_waiter_ = std::make_unique<async::Wait>(event_.get(), FSHOST_SIGNAL_EXIT_DONE);
  shutdown_waiter_->set_handler(
      [callback = std::move(callback)](async_dispatcher_t*, async::Wait*, zx_status_t status,
                                       /*signal*/ const zx_packet_signal_t*) { callback(status); });
  shutdown_waiter_->Begin(global_loop_->dispatcher());
}

zx_status_t FsManager::AddFsDiagnosticsDirectory(const char* diagnostics_dir_name,
                                                 zx::channel fs_diagnostics_dir_client) {
  // The diagnostics directory may not be initialized in tests.
  if (diagnostics_dir_ == nullptr) {
    return ZX_ERR_INTERNAL;
  }
  auto fs_diagnostics_dir =
      fbl::MakeRefCounted<fs::RemoteDir>(std::move(fs_diagnostics_dir_client));
  return diagnostics_dir_->AddEntry(diagnostics_dir_name, fs_diagnostics_dir);
}

}  // namespace devmgr

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
#include <lib/syslog/cpp/macros.h>
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
#include "fuchsia/feedback/llcpp/fidl.h"
#include "fshost-boot-args.h"
#include "lib/async/cpp/task.h"
#include "lifecycle.h"
#include "metrics.h"

#define ZXDEBUG 0

namespace devmgr {

namespace {

std::string ReportReasonStr(const FsManager::ReportReason& reason) {
  switch (reason) {
    case FsManager::ReportReason::kMinfsCorrupted:
      return "fuchsia-minfs-corruption";
    case FsManager::ReportReason::kMinfsNotUpgradeable:
      return "fuchsia-minfs-not-upgraded";
  }
}

}  // namespace

FsManager::FsManager(std::shared_ptr<FshostBootArgs> boot_args,
                     std::unique_ptr<FsHostMetrics> metrics)
    : global_loop_(new async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread)),
      outgoing_vfs_(fs::ManagedVfs(global_loop_->dispatcher())),
      registry_(global_loop_.get()),
      metrics_(std::move(metrics)),
      boot_args_(boot_args) {
  ZX_ASSERT(global_root_ == nullptr);
}

// In the event that we haven't been explicitly signalled, tear ourself down.
FsManager::~FsManager() {
  if (!shutdown_called_) {
    Shutdown([](zx_status_t status) {
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "filesystem shutdown failed: " << zx_status_get_string(status);
        return;
      }
      FX_LOGS(INFO) << "filesystem shutdown complete";
    });
  }
  sync_completion_wait(&shutdown_, ZX_TIME_INFINITE);
}

zx_status_t FsManager::SetupLifecycleServer(zx::channel lifecycle_request) {
  return devmgr::LifecycleServer::Create(global_loop_->dispatcher(), this,
                                         std::move(lifecycle_request));
}

// Sets up the outgoing directory, and runs it on the PA_DIRECTORY_REQUEST
// handle if it exists. See fshost.cml for a list of what's in the directory.
zx_status_t FsManager::SetupOutgoingDirectory(zx::channel dir_request,
                                              std::shared_ptr<loader::LoaderServiceBase> loader,
                                              BlockWatcher& watcher) {
  auto outgoing_dir = fbl::MakeRefCounted<fs::PseudoDir>();

  // TODO(unknown): fshost exposes two separate service directories, one here and one in
  // the registry vfs that's mounted under fs-manager-svc further down in this
  // function. These should be combined by either pulling the registry services
  // into this VFS or by pushing the services in this directory into the
  // registry.

  // Add loader and admin services to the vfs
  auto svc_dir = fbl::MakeRefCounted<fs::PseudoDir>();

  if (loader) {
    // This service name is breaking the convention whereby the directory entry
    // name matches the protocol name. This is an implementation of
    // fuchsia.ldsvc.Loader, and is renamed to make it easier to identify that
    // this implementation comes from fshost.
    svc_dir->AddEntry(
        "fuchsia.fshost.Loader", fbl::MakeRefCounted<fs::Service>([loader](zx::channel chan) {
          auto status = loader->Bind(std::move(chan));
          if (status.is_error()) {
            FX_LOGS(ERROR) << "failed to attach loader service: " << status.status_string();
          }
          return status.status_value();
        }));
  }
  svc_dir->AddEntry(llcpp::fuchsia::fshost::Admin::Name,
                    AdminServer::Create(this, global_loop_->dispatcher()));

  svc_dir->AddEntry(llcpp::fuchsia::fshost::BlockWatcher::Name,
                    BlockWatcherServer::Create(global_loop_->dispatcher(), watcher));

  outgoing_dir->AddEntry("svc", std::move(svc_dir));

  // Add /fs to the outgoing vfs
  zx::channel filesystems_client, filesystems_server;
  zx_status_t status = zx::channel::create(0, &filesystems_client, &filesystems_server);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to create channel";
    return status;
  }
  status = this->ServeRoot(std::move(filesystems_server));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot serve root filesystem";
    return status;
  }
  outgoing_dir->AddEntry("fs", fbl::MakeRefCounted<fs::RemoteDir>(std::move(filesystems_client)));

  // Add /fs-manager-svc to the vfs
  zx::channel services_client, services_server;
  status = zx::channel::create(0, &services_client, &services_server);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to create channel";
    return status;
  }
  status = this->ServeFshostRoot(std::move(services_server));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot serve export directory";
    return status;
  }
  outgoing_dir->AddEntry("fs-manager-svc",
                         fbl::MakeRefCounted<fs::RemoteDir>(std::move(services_client)));

  // TODO(fxbug.dev/39588): delete this
  // Add the delayed directory
  zx::channel filesystems_client_2, filesystems_server_2;
  status = zx::channel::create(0, &filesystems_client_2, &filesystems_server_2);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to create channel";
    return status;
  }
  status = this->ServeRoot(std::move(filesystems_server_2));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "cannot serve root filesystem";
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

zx_status_t FsManager::Initialize(zx::channel dir_request, zx::channel lifecycle_request,
                                  std::shared_ptr<loader::LoaderServiceBase> loader,
                                  BlockWatcher& watcher) {
  zx_status_t status = memfs::Vfs::Create("<root>", &root_vfs_, &global_root_);
  if (status != ZX_OK) {
    return status;
  }

  fbl::RefPtr<fs::Vnode> vn;
  if ((status = global_root_->Create("boot", S_IFDIR, &vn)) != ZX_OK) {
    return status;
  }
  if ((status = global_root_->Create("tmp", S_IFDIR, &vn)) != ZX_OK) {
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
    FX_LOGS(ERROR) << "failed to serve /data stats";
  }

  global_loop_->StartThread("root-dispatcher");
  root_vfs_->SetDispatcher(global_loop_->dispatcher());
  if (dir_request.is_valid()) {
    status = SetupOutgoingDirectory(std::move(dir_request), std::move(loader), watcher);
    if (status != ZX_OK) {
      return status;
    }
  }
  if (lifecycle_request.is_valid()) {
    status = SetupLifecycleServer(std::move(lifecycle_request));
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

void FsManager::FlushMetrics() { mutable_metrics()->Flush(); }

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

void FsManager::Shutdown(fit::function<void(zx_status_t)> callback) {
  std::lock_guard guard(lock_);
  if (shutdown_called_) {
    FX_LOGS(ERROR) << "shutdown called more than once";
    callback(ZX_ERR_INTERNAL);
    return;
  }
  shutdown_called_ = true;

  async::PostTask(global_loop_->dispatcher(), [this, callback = std::move(callback)]() {
    FX_LOGS(INFO) << "filesystem shutdown initiated";
    zx_status_t status = root_vfs_->UninstallAll(zx::time::infinite());
    callback(status);
    sync_completion_signal(&shutdown_);
    // after this signal, FsManager can be destroyed.
  });
}

bool FsManager::IsShutdown() { return sync_completion_signaled(&shutdown_); }

void FsManager::WaitForShutdown() { sync_completion_wait(&shutdown_, ZX_TIME_INFINITE); }

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

void FsManager::FileReport(ReportReason reason) {
  if (!file_crash_report_) {
    FX_LOGS(INFO) << "Not filing a crash report for " << ReportReasonStr(reason) << " (disabled)";
    return;
  }
  FX_LOGS(INFO) << "Filing a crash report for " << ReportReasonStr(reason);
  // This thread accesses no state in the SyntheticCrashReporter, so is thread-safe even if the
  // reporter is destroyed.
  std::thread t([reason]() {
    ::zx::channel client_end, server_end;
    if (zx_status_t status = ::zx::channel::create(0, &client_end, &server_end); status != ZX_OK) {
      FX_LOGS(WARNING) << "Unable to connect to crash reporting service: "
                       << zx_status_get_string(status);
      return;
    }
    std::string path = std::string("/svc/") +
        llcpp::fuchsia::feedback::CrashReporter::Name;
    if (zx_status_t status = fdio_service_connect(path.c_str(), server_end.release());
        status != ZX_OK) {
      FX_LOGS(WARNING) << "Unable to connect to crash reporting service: "
                       << zx_status_get_string(status);
      return;
    }
    auto client = llcpp::fuchsia::feedback::CrashReporter::SyncClient(
        std::move(client_end));

    fidl::StringView name("minfs");
    std::string reason_str = ReportReasonStr(reason);
    fidl::StringView reason_fidl_str = fidl::unowned_str(reason_str);
    llcpp::fuchsia::feedback::CrashReport report =
        llcpp::fuchsia::feedback::CrashReport::Builder(
            std::make_unique<llcpp::fuchsia::feedback::CrashReport::Frame>()
        ).set_program_name(fidl::unowned_ptr(&name))
        .set_crash_signature(fidl::unowned_ptr(&reason_fidl_str))
        .build();
    auto res = client.File(std::move(report));
    if (!res.ok()) {
      FX_LOGS(WARNING) << "Unable to send crash report (fidl error): " << res.status_string();
    }
    if (res->result.is_err()) {
      FX_LOGS(WARNING) << "Failed to file crash report: "
                       << zx_status_get_string(res->result.err());
    } else {
      FX_LOGS(INFO) << "Crash report successfully filed";
    }
  });
  t.detach();
}

}  // namespace devmgr

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fs-manager.h"

#include <fcntl.h>
#include <fuchsia/feedback/llcpp/fidl.h>
#include <fuchsia/fshost/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/service/llcpp/service.h>
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
#include <fbl/ref_ptr.h>

#include "admin-server.h"
#include "block-watcher.h"
#include "fshost-boot-args.h"
#include "fuchsia/ldsvc/llcpp/fidl.h"
#include "lib/async/cpp/task.h"
#include "lifecycle.h"
#include "metrics.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"
#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"

#define ZXDEBUG 0

namespace fshost {

FsManager::FsManager(std::shared_ptr<FshostBootArgs> boot_args,
                     std::unique_ptr<FsHostMetrics> metrics)
    : global_loop_(new async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread)),
      outgoing_vfs_(fs::ManagedVfs(global_loop_->dispatcher())),
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

zx_status_t FsManager::SetupLifecycleServer(
    fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> lifecycle_request) {
  return LifecycleServer::Create(global_loop_->dispatcher(), this, std::move(lifecycle_request));
}

// Sets up the outgoing directory, and runs it on the PA_DIRECTORY_REQUEST
// handle if it exists. See fshost.cml for a list of what's in the directory.
zx_status_t FsManager::SetupOutgoingDirectory(fidl::ServerEnd<fuchsia_io::Directory> dir_request,
                                              std::shared_ptr<loader::LoaderServiceBase> loader,
                                              BlockWatcher& watcher) {
  auto outgoing_dir = fbl::MakeRefCounted<fs::PseudoDir>();

  // Add loader and admin services to the vfs
  svc_dir_ = fbl::MakeRefCounted<fs::PseudoDir>();

  if (loader) {
    // This service name is breaking the convention whereby the directory entry
    // name matches the protocol name. This is an implementation of
    // fuchsia.ldsvc.Loader, and is renamed to make it easier to identify that
    // this implementation comes from fshost.
    svc_dir_->AddEntry(
        "fuchsia.fshost.Loader",
        fbl::MakeRefCounted<fs::Service>([loader](fidl::ServerEnd<fuchsia_ldsvc::Loader> chan) {
          loader->Bind(std::move(chan));
          return ZX_OK;
        }));
  }
  svc_dir_->AddEntry(fidl::DiscoverableProtocolName<fuchsia_fshost::Admin>,
                     AdminServer::Create(this, global_loop_->dispatcher()));

  svc_dir_->AddEntry(fidl::DiscoverableProtocolName<fuchsia_fshost::BlockWatcher>,
                     BlockWatcherServer::Create(global_loop_->dispatcher(), watcher));

  outgoing_dir->AddEntry("svc", svc_dir_);

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

zx_status_t FsManager::Initialize(
    fidl::ServerEnd<fuchsia_io::Directory> dir_request,
    fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> lifecycle_request,
    fidl::ClientEnd<fuchsia_device_manager::Administrator> driver_admin,
    std::shared_ptr<loader::LoaderServiceBase> loader, BlockWatcher& watcher) {
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
  for (const auto& point : kAllMountPoints) {
    auto open_result = root_vfs_->Open(global_root_, std::string_view(MountPointPath(point)),
                                       fs::VnodeConnectionOptions::ReadWrite().set_create(),
                                       fs::Rights::ReadWrite(), S_IFDIR);
    if (open_result.is_error()) {
      return open_result.error();
    }

    ZX_ASSERT(open_result.is_ok());
    mount_nodes_[point].root_directory = std::move(open_result.ok().vnode);
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
  if (driver_admin.is_valid()) {
    driver_admin_ = fidl::WireSharedClient<fuchsia_device_manager::Administrator>(
        std::move(driver_admin), global_loop_->dispatcher());
  }
  return ZX_OK;
}

void FsManager::FlushMetrics() { mutable_metrics()->Flush(); }

zx_status_t FsManager::InstallFs(MountPoint point, zx::channel root_directory) {
  if (mount_nodes_.find(point) == mount_nodes_.end()) {
    // The map should have been fully initialized.
    return ZX_ERR_BAD_STATE;
  }
  if (zx_status_t status = root_vfs_->InstallRemote(mount_nodes_[point].root_directory,
                                                    fs::MountChannel(std::move(root_directory)));
      status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

zx_status_t FsManager::SetFsExportRoot(MountPoint point, zx::channel export_root_directory) {
  if (mount_nodes_.find(point) == mount_nodes_.end()) {
    // The map should have been fully initialized.
    return ZX_ERR_BAD_STATE;
  }
  mount_nodes_[point].root_export_dir = std::move(export_root_directory);
  return ZX_OK;
}

zx_status_t FsManager::ServeRoot(fidl::ServerEnd<fuchsia_io::Directory> server) {
  fs::Rights rights;
  rights.read = true;
  rights.write = true;
  rights.admin = true;
  rights.execute = true;
  return root_vfs_->ServeDirectory(global_root_, std::move(server), rights);
}

void FsManager::RemoveSystemDrivers(fit::callback<void(zx_status_t)> callback) {
  // If we don't have a connection to Driver Manager, just return ZX_OK.
  if (!driver_admin_.is_valid()) {
    callback(ZX_OK);
    return;
  }

  auto callback_ptr = std::make_shared<fit::callback<void(zx_status_t)>>(std::move(callback));
  auto res = driver_admin_->UnregisterSystemStorageForShutdown(
      [callback_ptr](
          fidl::WireResponse<
              fuchsia_device_manager::Administrator::UnregisterSystemStorageForShutdown>* res) {
        if (res->status != ZX_OK) {
          FX_LOGS(ERROR) << "RemoveSystemDevices returned error: "
                         << zx_status_get_string(res->status);
        }
        if (*callback_ptr) {
          (*callback_ptr)(res->status);
        }
      });
  if (res.status() != ZX_OK) {
    if (*callback_ptr) {
      (*callback_ptr)(res.status());
    }
  }
}

void FsManager::Shutdown(fit::function<void(zx_status_t)> callback) {
  std::lock_guard guard(lock_);
  if (shutdown_called_) {
    FX_LOGS(ERROR) << "shutdown called more than once";
    callback(ZX_ERR_INTERNAL);
    return;
  }
  shutdown_called_ = true;

  FX_LOGS(INFO) << "filesystem shutdown initiated";
  // Shutting down fshost involves sending asynchronous shutdown signals to several different
  // systems in order with continuation passing.
  // 0. Before fshost is told to shut down, almost everything that is running out of the
  //    filesystems is shut down by component manager.
  // 1. Shut down drivers that are running out of the system partition. These are hosted out of
  //    blobfs, and are the last thing in the system with a dependency on the filesystems.
  // 2. Shut down the outgoing vfs. This hosts the fshost services. The outgoing vfs also has
  //    handles to the filesystems, but it doesn't own them so it doesn't shut them down.
  // 3. Shut down the root vfs. This hosts the filesystems, and recursively shuts all of them down.
  // If at any point we hit an error, we log loudly, but continue with the shutdown procedure.
  RemoveSystemDrivers([this, callback = std::move(callback)](zx_status_t status) mutable {
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "RemoveSystemDrivers failed: " << zx_status_get_string(status);
    }
    outgoing_vfs_.Shutdown([this, callback = std::move(callback)](zx_status_t status) mutable {
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "outgoing_vfs shutdown failed: " << zx_status_get_string(status);
      }
      root_vfs_->Shutdown([this, callback = std::move(callback)](zx_status_t status) {
        if (status != ZX_OK) {
          FX_LOGS(ERROR) << "root_vfs shutdown failed: " << zx_status_get_string(status);
        }
        callback(status);
        sync_completion_signal(&shutdown_);
        // after this signal, FsManager can be destroyed.
      });
    });
  });
}

bool FsManager::IsShutdown() { return sync_completion_signaled(&shutdown_); }

void FsManager::WaitForShutdown() { sync_completion_wait(&shutdown_, ZX_TIME_INFINITE); }

const char* FsManager::MountPointPath(FsManager::MountPoint point) {
  switch (point) {
    case MountPoint::kUnknown:
      return "";
    case MountPoint::kBin:
      return "/bin";
    case MountPoint::kData:
      return "/data";
    case MountPoint::kVolume:
      return "/volume";
    case MountPoint::kSystem:
      return "/system";
    case MountPoint::kInstall:
      return "/install";
    case MountPoint::kBlob:
      return "/blob";
    case MountPoint::kPkgfs:
      return "/pkgfs";
    case MountPoint::kFactory:
      return "/factory";
    case MountPoint::kDurable:
      return "/durable";
  }
}

zx_status_t FsManager::ForwardFsDiagnosticsDirectory(MountPoint point,
                                                     const char* diagnostics_dir_name) {
  // The diagnostics directory may not be initialized in tests.
  if (diagnostics_dir_ == nullptr) {
    return ZX_ERR_INTERNAL;
  }
  if (point == MountPoint::kUnknown) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (!mount_nodes_[point].root_export_dir) {
    FX_LOGS(ERROR) << "Can't forward diagnostics dir for " << MountPointPath(point)
                   << ", export root directory was not set";
    return ZX_ERR_BAD_STATE;
  }

  auto inspect_node = fbl::MakeRefCounted<fs::Service>([this, point](zx::channel request) {
    std::string name = std::string("diagnostics/") + fuchsia::inspect::Tree::Name_;
    return fdio_service_connect_at(mount_nodes_[point].root_export_dir.get(), name.c_str(),
                                   request.release());
  });
  auto fs_diagnostics_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  zx_status_t status = fs_diagnostics_dir->AddEntry(fuchsia::inspect::Tree::Name_, inspect_node);
  if (status != ZX_OK) {
    return status;
  }
  return diagnostics_dir_->AddEntry(diagnostics_dir_name, fs_diagnostics_dir);
}

zx_status_t FsManager::ForwardFsService(MountPoint point, const char* service_name) {
  // The outgoing service directory may not be initialized in tests.
  if (svc_dir_ == nullptr) {
    return ZX_ERR_INTERNAL;
  }
  if (point == MountPoint::kUnknown) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (!mount_nodes_[point].root_export_dir) {
    FX_LOGS(ERROR) << "Can't forward service for " << MountPointPath(point)
                   << ", export root directory was not set";
    return ZX_ERR_BAD_STATE;
  }

  auto service_node =
      fbl::MakeRefCounted<fs::Service>([this, point, service_name](zx::channel request) {
        std::string name = std::string("svc/") + service_name;
        return fdio_service_connect_at(mount_nodes_[point].root_export_dir.get(), name.c_str(),
                                       request.release());
      });
  return svc_dir_->AddEntry(service_name, std::move(service_node));
}

void FsManager::ReportMinfsCorruption() {
  mutable_metrics()->LogMinfsCorruption();
  FlushMetrics();

  if (!file_crash_report_) {
    return;
  }

  FX_LOGS(INFO) << "Filing a crash report for minfs corruption";
  std::thread t([]() {
    auto client_end = service::Connect<fuchsia_feedback::CrashReporter>();
    if (client_end.is_error()) {
      FX_LOGS(WARNING) << "Unable to connect to crash reporting service for minfs corruption: "
                       << client_end.status_string();
      return;
    }
    auto client = fidl::BindSyncClient(std::move(*client_end));

    fidl::Arena allocator;
    fuchsia_feedback::wire::CrashReport report(allocator);
    report.set_program_name(allocator, allocator, "minfs");
    report.set_crash_signature(allocator, allocator, "fuchsia-corrupted-minfs");
    report.set_is_fatal(allocator, false);

    auto res = client.File(report);
    if (!res.ok()) {
      FX_LOGS(WARNING) << "Unable to send crash report (fidl error) for minfs corruption: "
                       << res.status_string();
    }
    if (res->result.is_err()) {
      FX_LOGS(WARNING) << "Failed to file crash report for minfs corruption: "
                       << zx_status_get_string(res->result.err());
    } else {
      FX_LOGS(INFO) << "Crash report successfully filed for minfs corruption";
    }
  });
  t.detach();
}

}  // namespace fshost

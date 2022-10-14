// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/component_runner.h"

#include <fidl/fuchsia.fs.startup/cpp/wire.h>
#include <fidl/fuchsia.fs/cpp/wire.h>
#include <fidl/fuchsia.update.verify/cpp/wire.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/syslog/cpp/macros.h>

#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/remote_dir.h"
#include "src/storage/blobfs/mount.h"
#include "src/storage/blobfs/service/admin.h"
#include "src/storage/blobfs/service/blobfs.h"
#include "src/storage/blobfs/service/health_check.h"
#include "src/storage/blobfs/service/lifecycle.h"
#include "src/storage/blobfs/service/startup.h"

namespace blobfs {

ComponentRunner::ComponentRunner(async::Loop& loop, ComponentOptions config)
    : fs::PagedVfs(loop.dispatcher(), config.pager_threads), loop_(loop), config_(config) {
  outgoing_ = fbl::MakeRefCounted<fs::PseudoDir>();
  auto startup = fbl::MakeRefCounted<fs::PseudoDir>();
  outgoing_->AddEntry("startup", startup);

  FX_LOGS(INFO) << "setting up services";

  auto startup_svc = fbl::MakeRefCounted<StartupService>(
      loop_.dispatcher(), config_,
      [this](std::unique_ptr<BlockDevice> device, const MountOptions& options) {
        FX_LOGS(INFO) << "configure callback is called";
        zx::status<> status = Configure(std::move(device), options);
        if (status.is_error()) {
          FX_LOGS(ERROR) << "Could not configure blobfs: " << status.status_string();
        }
        return status;
      });
  startup->AddEntry(fidl::DiscoverableProtocolName<fuchsia_fs_startup::Startup>, startup_svc);
}

ComponentRunner::~ComponentRunner() {
  // Inform PagedVfs so that it can stop threads that might call out to blobfs.
  TearDown();
}

void ComponentRunner::RemoveSystemDrivers(fit::callback<void(zx_status_t)> callback) {
  // If we don't have a connection to Driver Manager, just return ZX_OK.
  if (!driver_admin_.is_valid()) {
    FX_LOGS(INFO) << "blobfs doesn't have driver manager connection; assuming test environment";
    callback(ZX_OK);
    return;
  }

  using Unregister = fuchsia_device_manager::Administrator::UnregisterSystemStorageForShutdown;
  driver_admin_->UnregisterSystemStorageForShutdown().ThenExactlyOnce(
      [callback = std::move(callback)](fidl::WireUnownedResult<Unregister>& result) mutable {
        if (!result.ok()) {
          callback(result.status());
          return;
        }
        callback(result.value().status);
      });
}

void ComponentRunner::Shutdown(fs::FuchsiaVfs::ShutdownCallback cb) {
  TRACE_DURATION("blobfs", "ComponentRunner::Shutdown");
  // Before shutting down blobfs, we need to try to shut down any drivers that are running out of
  // it, because right now those drivers don't have an explicit dependency on blobfs in the
  // component hierarchy so they don't get shut down before us yet.
  RemoveSystemDrivers([this, cb = std::move(cb)](zx_status_t status) mutable {
    // If we failed to notify the driver stack about the impending shutdown, log a warning, but
    // continue the shutdown.
    if (status != ZX_OK) {
      FX_LOGS(WARNING) << "failed to send shutdown signal to driver manager: "
                       << zx_status_get_string(status);
    }
    // Shutdown all external connections to blobfs.
    ManagedVfs::Shutdown([this, cb = std::move(cb)](zx_status_t status) mutable {
      async::PostTask(dispatcher(), [this, status, cb = std::move(cb)]() mutable {
        // Manually destroy the filesystem. The promise of Shutdown is that no
        // connections are active, and destroying the Runner object
        // should terminate all background workers.
        blobfs_ = nullptr;

        // Tell the mounting thread that the filesystem has terminated.
        loop_.Quit();

        // Tell the unmounting channel that we've completed teardown. This *must* be the last thing
        // we do because after this, the caller can assume that it's safe to destroy the runner.
        cb(status);
      });
    });
  });
}

zx::status<fs::FilesystemInfo> ComponentRunner::GetFilesystemInfo() {
  return blobfs_->GetFilesystemInfo();
}

zx::status<> ComponentRunner::ServeRoot(
    fidl::ServerEnd<fuchsia_io::Directory> root,
    fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> lifecycle,
    fidl::ClientEnd<fuchsia_device_manager::Administrator> driver_admin_client,
    zx::resource vmex_resource) {
  LifecycleServer::Create(
      loop_.dispatcher(),
      [this](fs::FuchsiaVfs::ShutdownCallback cb) { this->Shutdown(std::move(cb)); },
      std::move(lifecycle));

  fidl::WireSharedClient<fuchsia_device_manager::Administrator> driver_admin;
  if (driver_admin_client.is_valid()) {
    driver_admin = fidl::WireSharedClient<fuchsia_device_manager::Administrator>(
        std::move(driver_admin_client), loop_.dispatcher());
  }
  driver_admin_ = std::move(driver_admin);

  // Make dangling endpoints for the root directory and the service directory. Creating the
  // endpoints and putting them into the filesystem tree has the effect of queuing incoming
  // requests until the server end of the endpoints is bound.
  auto svc_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (svc_endpoints.is_error()) {
    FX_LOGS(ERROR) << "mount failed; could not create service directory endpoints";
    return svc_endpoints.take_error();
  }
  outgoing_->AddEntry("svc", fbl::MakeRefCounted<fs::RemoteDir>(std::move(svc_endpoints->client)));
  svc_server_end_ = std::move(svc_endpoints->server);
  auto root_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  if (root_endpoints.is_error()) {
    FX_LOGS(ERROR) << "mount failed; could not create root directory endpoints";
    return root_endpoints.take_error();
  }
  outgoing_->AddEntry("root",
                      fbl::MakeRefCounted<fs::RemoteDir>(std::move(root_endpoints->client)));
  root_server_end_ = std::move(root_endpoints->server);

  vmex_resource_ = std::move(vmex_resource);
  zx_status_t status = ServeDirectory(outgoing_, std::move(root));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "mount failed; could not serve root directory";
    return zx::error(status);
  }

  return zx::ok();
}

zx::status<> ComponentRunner::Configure(std::unique_ptr<BlockDevice> device,
                                        const MountOptions& options) {
  if (auto status = Init(); status.is_error()) {
    FX_LOGS(ERROR) << "configure failed; vfs init failed";
    return status.take_error();
  }

  // All of our pager threads get the deadline profile for scheduling.
  SetDeadlineProfile(GetPagerThreads());

  auto blobfs_or = Blobfs::Create(loop_.dispatcher(), std::move(device), this, options,
                                  std::move(vmex_resource_));
  if (blobfs_or.is_error()) {
    FX_LOGS(ERROR) << "configure failed; could not create blobfs: " << blobfs_or.status_string();
    return blobfs_or.take_error();
  }
  blobfs_ = std::move(blobfs_or.value());
  SetReadonly(blobfs_->writability() != Writability::Writable);

  fbl::RefPtr<fs::Vnode> root;
  zx_status_t status = blobfs_->OpenRootNode(&root);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "configure failed; could not get root blob: " << zx_status_get_string(status);
    return zx::error(status);
  }

  status = ServeDirectory(std::move(root), std::move(root_server_end_));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "configure failed; could not serve root directory: "
                   << zx_status_get_string(status);
    return zx::error(status);
  }

  // Specify to fall back to DeepCopy mode instead of Live mode (the default) on failures to send
  // a Frozen copy of the tree (e.g. if we could not create a child copy of the backing VMO).
  // This helps prevent any issues with querying the inspect tree while the filesystem is under
  // load, since snapshots at the receiving end must be consistent. See fxbug.dev/57330 for details.
  inspect::TreeHandlerSettings settings{.snapshot_behavior =
                                            inspect::TreeServerSendPreference::Frozen(
                                                inspect::TreeServerSendPreference::Type::DeepCopy)};

  auto inspect_tree = fbl::MakeRefCounted<fs::Service>(
      [connector = inspect::MakeTreeHandler(blobfs_->GetMetrics()->inspector(), loop_.dispatcher(),
                                            settings)](zx::channel chan) mutable {
        connector(fidl::InterfaceRequest<fuchsia::inspect::Tree>(std::move(chan)));
        return ZX_OK;
      });

  // Add the diagnostics directory straight to the outgoing directory. Nothing should be relying on
  // the diagnostics directory queuing incoming requests.
  auto diagnostics_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  outgoing_->AddEntry("diagnostics", diagnostics_dir);
  diagnostics_dir->AddEntry(fuchsia::inspect::Tree::Name_, inspect_tree);

  auto svc_dir = fbl::MakeRefCounted<fs::PseudoDir>();

  svc_dir->AddEntry(fidl::DiscoverableProtocolName<fuchsia_update_verify::BlobfsVerifier>,
                    fbl::MakeRefCounted<HealthCheckService>(loop_.dispatcher(), *blobfs_));

  svc_dir->AddEntry(fidl::DiscoverableProtocolName<fuchsia_fs::Admin>,
                    fbl::MakeRefCounted<AdminService>(blobfs_->dispatcher(),
                                                      [this](fs::FuchsiaVfs::ShutdownCallback cb) {
                                                        this->Shutdown(std::move(cb));
                                                      }));
  svc_dir->AddEntry(fidl::DiscoverableProtocolName<fuchsia_blobfs::Blobfs>,
                    fbl::MakeRefCounted<BlobfsService>(loop_.dispatcher(), *blobfs_));

  status = ServeDirectory(std::move(svc_dir), std::move(svc_server_end_));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "configure failed; could not serve svc dir: " << zx_status_get_string(status);
    return zx::error(status);
  }

  return zx::ok();
}

}  // namespace blobfs

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/component_runner.h"

#include <fidl/fuchsia.fs.startup/cpp/wire.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/storage/vfs/cpp/remote_dir.h"
#include "src/lib/storage/vfs/cpp/trace.h"
#include "src/storage/minfs/service/admin.h"
#include "src/storage/minfs/service/lifecycle.h"
#include "src/storage/minfs/service/startup.h"

namespace minfs {

ComponentRunner::ComponentRunner(async_dispatcher_t* dispatcher)
    : fs::ManagedVfs(dispatcher), dispatcher_(dispatcher) {
  outgoing_ = fbl::MakeRefCounted<fs::PseudoDir>();
  auto startup = fbl::MakeRefCounted<fs::PseudoDir>();
  outgoing_->AddEntry("startup", startup);

  FX_LOGS(INFO) << "setting up startup service";
  auto startup_svc = fbl::MakeRefCounted<StartupService>(
      dispatcher_, [this](std::unique_ptr<Bcache> device, const MountOptions& options) {
        FX_LOGS(INFO) << "configure callback is called";
        zx::result<> status = Configure(std::move(device), options);
        if (status.is_error()) {
          FX_LOGS(ERROR) << "Could not configure minfs: " << status.status_string();
        }
        return status;
      });
  startup->AddEntry(fidl::DiscoverableProtocolName<fuchsia_fs_startup::Startup>, startup_svc);
}

zx::result<> ComponentRunner::ServeRoot(
    fidl::ServerEnd<fuchsia_io::Directory> root,
    fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> lifecycle) {
  LifecycleServer::Create(
      dispatcher_, [this](fs::FuchsiaVfs::ShutdownCallback cb) { this->Shutdown(std::move(cb)); },
      std::move(lifecycle));

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

  zx_status_t status = ServeDirectory(outgoing_, std::move(root));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "mount failed; could not serve root directory";
    return zx::error(status);
  }

  return zx::ok();
}

zx::result<> ComponentRunner::Configure(std::unique_ptr<Bcache> bcache,
                                        const MountOptions& options) {
  auto minfs = Minfs::Create(dispatcher_, std::move(bcache), options, this);
  if (minfs.is_error()) {
    FX_LOGS(ERROR) << "configure failed; could not create minfs: " << minfs.status_string();
    return minfs.take_error();
  }
  minfs_ = *std::move(minfs);
  SetReadonly(options.writability != Writability::Writable);

  auto root = minfs_->OpenRootNode();
  if (root.is_error()) {
    FX_LOGS(ERROR) << "cannot find root inode: " << root.status_string();
    return root.take_error();
  }

  zx_status_t status = ServeDirectory(*std::move(root), std::move(root_server_end_));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "configure failed; could not serve root directory: "
                   << zx_status_get_string(status);
    return zx::error(status);
  }

  // Specify to fall back to DeepCopy mode instead of Live mode (the default) on failures to send
  // a Frozen copy of the tree (e.g. if we could not create a child copy of the backing VMO).
  // This helps prevent any issues with querying the inspect tree while the filesystem is under
  // load, since snapshots at the receiving end must be consistent. See fxbug.dev/57330 for
  // details.
  inspect::TreeHandlerSettings settings{.snapshot_behavior =
                                            inspect::TreeServerSendPreference::Frozen(
                                                inspect::TreeServerSendPreference::Type::DeepCopy)};

  auto inspect_tree = fbl::MakeRefCounted<fs::Service>(
      [connector = inspect::MakeTreeHandler(&minfs_->InspectTree()->Inspector(), dispatcher_,
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
  svc_dir->AddEntry(
      fidl::DiscoverableProtocolName<fuchsia_fs::Admin>,
      fbl::MakeRefCounted<AdminService>(dispatcher_, [this](fs::FuchsiaVfs::ShutdownCallback cb) {
        this->Shutdown(std::move(cb));
      }));

  status = ServeDirectory(std::move(svc_dir), std::move(svc_server_end_));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "configure failed; could not serve svc dir: " << zx_status_get_string(status);
    return zx::error(status);
  }

  return zx::ok();
}

void ComponentRunner::Shutdown(fs::FuchsiaVfs::ShutdownCallback cb) {
  TRACE_DURATION("minfs", "ComponentRunner::Shutdown");
  FX_LOGS(INFO) << "Shutting down";
  ManagedVfs::Shutdown([this, cb = std::move(cb)](zx_status_t status) mutable {
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Managed VFS shutdown failed with status: " << zx_status_get_string(status);
    }
    if (minfs_) {
      minfs_->Sync([this, cb = std::move(cb)](zx_status_t sync_status) mutable {
        if (sync_status != ZX_OK) {
          FX_LOGS(ERROR) << "Sync at unmount failed with status: "
                         << zx_status_get_string(sync_status);
        }
        async::PostTask(dispatcher(), [this, cb = std::move(cb)]() mutable {
          std::unique_ptr<Bcache> bc = Minfs::Destroy(std::move(minfs_));
          bc.reset();

          if (on_unmount_) {
            on_unmount_();
          }

          // Tell the unmounting channel that we've completed teardown. This *must* be the last
          // thing we do because after this, the caller can assume that it's safe to destroy the
          // runner.
          cb(ZX_OK);
        });
      });
    } else {
      async::PostTask(dispatcher(), [this, cb = std::move(cb)]() mutable {
        if (on_unmount_) {
          on_unmount_();
        }

        cb(ZX_OK);
      });
    }
  });
}

zx::result<fs::FilesystemInfo> ComponentRunner::GetFilesystemInfo() {
  return minfs_->GetFilesystemInfo();
}

void ComponentRunner::OnNoConnections() {
  if (IsTerminating()) {
    return;
  }
  Shutdown([](zx_status_t status) mutable {
    ZX_ASSERT_MSG(status == ZX_OK, "Filesystem shutdown failed on OnNoConnections(): %s",
                  zx_status_get_string(status));
  });
}

}  // namespace minfs

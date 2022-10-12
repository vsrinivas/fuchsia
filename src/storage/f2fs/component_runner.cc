// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.fs.startup/cpp/wire.h>

#include "src/lib/storage/vfs/cpp/remote_dir.h"
#include "src/storage/f2fs/f2fs.h"
#include "src/storage/f2fs/service/admin.h"
#include "src/storage/f2fs/service/lifecycle.h"
#include "src/storage/f2fs/service/startup.h"

namespace f2fs {

ComponentRunner::ComponentRunner(async_dispatcher_t* dispatcher)
    : fs::PagedVfs(dispatcher), dispatcher_(dispatcher) {
  outgoing_ = fbl::MakeRefCounted<fs::PseudoDir>();
  auto startup = fbl::MakeRefCounted<fs::PseudoDir>();
  outgoing_->AddEntry("startup", startup);

  FX_LOGS(INFO) << "setting up startup service";
  auto startup_svc = fbl::MakeRefCounted<StartupService>(
      dispatcher_, [this](std::unique_ptr<Bcache> device, const MountOptions& options) {
        return Configure(std::move(device), options);
      });
  startup->AddEntry(fidl::DiscoverableProtocolName<fuchsia_fs_startup::Startup>, startup_svc);
}

ComponentRunner::~ComponentRunner() {
  // Inform PagedVfs so that it can stop threads that might call out to f2fs.
  TearDown();
}

zx::status<> ComponentRunner::ServeRoot(
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

zx::status<> ComponentRunner::Configure(std::unique_ptr<Bcache> bcache,
                                        const MountOptions& options) {
  // Create Pager and PagerPool
  if (auto status = Init(); status.is_error()) {
    return status.take_error();
  }

  uint32_t readonly;
  ZX_ASSERT(options.GetValue(f2fs::kOptReadOnly, &readonly) == ZX_OK);
  SetReadonly(readonly != 0);

  auto f2fs = F2fs::Create(dispatcher_, std::move(bcache), options, this);
  if (f2fs.is_error()) {
    FX_LOGS(ERROR) << "configure failed; could not create f2fs: " << f2fs.status_string();
    return f2fs.take_error();
  }
  f2fs_ = *std::move(f2fs);

  auto root_vnode = f2fs_->GetRootVnode();
  ZX_DEBUG_ASSERT(root_vnode.is_ok());

  zx_status_t status = ServeDirectory(std::move(*root_vnode), std::move(root_server_end_));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "configure failed; could not serve root directory: "
                   << zx_status_get_string(status);
    return zx::error(status);
  }

  f2fs_->GetInspectTree().Initialize();

  // Specify to fall back to DeepCopy mode instead of Live mode (the default) on failures to send
  // a Frozen copy of the tree (e.g. if we could not create a child copy of the backing VMO).
  // This helps prevent any issues with querying the inspect tree while the filesystem is under
  // load, since snapshots at the receiving end must be consistent. See fxbug.dev/57330 for
  // details.
  inspect::TreeHandlerSettings settings{.snapshot_behavior =
                                            inspect::TreeServerSendPreference::Frozen(
                                                inspect::TreeServerSendPreference::Type::DeepCopy)};

  auto inspect_tree = fbl::MakeRefCounted<fs::Service>(
      [connector = inspect::MakeTreeHandler(&f2fs_->GetInspectTree().GetInspector(), dispatcher_,
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
  TRACE_DURATION("f2fs", "ComponentRunner::Shutdown");
  FX_LOGS(INFO) << "Shutting down";
  PagedVfs::Shutdown([this, cb = std::move(cb)](zx_status_t status) mutable {
    if (f2fs_) {
      f2fs_->Sync([this, cb = std::move(cb)](zx_status_t sync_status) mutable {
        async::PostTask(dispatcher(), [this, cb = std::move(cb)]() mutable {
          f2fs_->PutSuper();
          ZX_ASSERT(f2fs_->TakeBc().is_ok());

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

zx::status<fs::FilesystemInfo> ComponentRunner::GetFilesystemInfo() {
  return f2fs_->GetFilesystemInfo();
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

}  // namespace f2fs

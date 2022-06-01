// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/runner.h"

#include "src/storage/minfs/minfs_private.h"

#ifdef __Fuchsia__
#include <lib/inspect/service/cpp/service.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/trace.h"
#include "src/storage/minfs/service/admin.h"
#endif

namespace minfs {

zx::status<std::unique_ptr<Runner>> Runner::Create(FuchsiaDispatcher dispatcher,
                                                   std::unique_ptr<Bcache> bc,
                                                   const MountOptions& options) {
  std::unique_ptr<Runner> runner(new Runner(dispatcher));

  auto minfs = Minfs::Create(dispatcher, std::move(bc), options, runner.get());
  if (minfs.is_error()) {
    return minfs.take_error();
  }

  runner->minfs_ = *std::move(minfs);
  runner->SetReadonly(options.writability != Writability::Writable);

  return zx::ok(std::move(runner));
}

std::unique_ptr<Bcache> Runner::Destroy(std::unique_ptr<Runner> runner) {
  return Minfs::Destroy(std::move(runner->minfs_));
}

#ifdef __Fuchsia__
Runner::Runner(async_dispatcher_t* dispatcher)
    : fs::ManagedVfs(dispatcher), dispatcher_(dispatcher) {}
#else
Runner::Runner(std::nullptr_t dispatcher) {}
#endif

#ifdef __Fuchsia__
void Runner::Shutdown(fs::FuchsiaVfs::ShutdownCallback cb) {
  TRACE_DURATION("minfs", "Runner::Shutdown");
  FX_LOGS(INFO) << "Shutting down";
  ManagedVfs::Shutdown([this, cb = std::move(cb)](zx_status_t status) mutable {
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Managed VFS shutdown failed with status: " << zx_status_get_string(status);
    }
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

        // Tell the unmounting channel that we've completed teardown. This *must* be the last thing
        // we do because after this, the caller can assume that it's safe to destroy the runner.
        cb(ZX_OK);
      });
    });
  });
}

zx::status<fs::FilesystemInfo> Runner::GetFilesystemInfo() { return minfs_->GetFilesystemInfo(); }

zx::status<> Runner::ServeRoot(fidl::ServerEnd<fuchsia_io::Directory> root) {
  auto vn = minfs_->VnodeGet(kMinfsRootIno);
  if (vn.is_error()) {
    FX_LOGS(ERROR) << "cannot find root inode: " << vn.is_error();
    return vn.take_error();
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

  auto outgoing = fbl::MakeRefCounted<fs::PseudoDir>(this);
  outgoing->AddEntry("root", *std::move(vn));

  auto diagnostics_dir = fbl::MakeRefCounted<fs::PseudoDir>(this);
  outgoing->AddEntry("diagnostics", diagnostics_dir);
  diagnostics_dir->AddEntry(fuchsia::inspect::Tree::Name_, inspect_tree);

  outgoing->AddEntry(fidl::DiscoverableProtocolName<fuchsia_fs::Admin>,
                     fbl::MakeRefCounted<AdminService>(dispatcher_, *this));

  zx_status_t status = ServeDirectory(std::move(outgoing), std::move(root));
  if (status != ZX_OK) {
    return zx::error(status);
  }

  return zx::ok();
}

void Runner::OnNoConnections() {
  if (IsTerminating()) {
    return;
  }
  Shutdown([](zx_status_t status) mutable {
    ZX_ASSERT_MSG(status == ZX_OK, "Filesystem shutdown failed on OnNoConnections(): %s",
                  zx_status_get_string(status));
  });
}
#endif  // __Fuchsia__

bool Runner::IsReadonly() const {
  std::lock_guard lock(vfs_lock_);
  return ReadonlyLocked();
}

}  // namespace minfs

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"
#ifdef __Fuchsia__
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#endif  // __Fuchsia__

namespace f2fs {

zx::status<std::unique_ptr<Runner>> Runner::CreateRunner(FuchsiaDispatcher dispatcher) {
  std::unique_ptr<Runner> runner(new Runner(dispatcher));
#ifdef __Fuchsia__
  // Create Pager and PagerPool
  if (auto status = runner->Init(); status.is_error()) {
    return status.take_error();
  }
#endif  // __Fuchsia__
  return zx::ok(std::move(runner));
}

zx::status<std::unique_ptr<Runner>> Runner::Create(FuchsiaDispatcher dispatcher,
                                                   std::unique_ptr<Bcache> bc,
                                                   const MountOptions& options) {
  auto runner_or = CreateRunner(dispatcher);
  if (runner_or.is_error()) {
    return runner_or.take_error();
  }

  uint32_t readonly;
  ZX_ASSERT(options.GetValue(f2fs::kOptReadOnly, &readonly) == ZX_OK);
  runner_or->SetReadonly(readonly != 0);

  auto fs_or = F2fs::Create(dispatcher, std::move(bc), options, (*runner_or).get());
  if (fs_or.is_error()) {
    return fs_or.take_error();
  }

  runner_or->f2fs_ = std::move(*fs_or);
  return zx::ok(std::move(*runner_or));
}

#ifndef __Fuchsia__
Runner::Runner(std::nullptr_t dispatcher) {}
Runner::~Runner() {}
#else  // __Fuchsia__
Runner::Runner(async_dispatcher_t* dispatcher)
    : fs::PagedVfs(dispatcher), dispatcher_(dispatcher) {}

void Runner::Shutdown(fs::FuchsiaVfs::ShutdownCallback cb) {
  TRACE_DURATION("f2fs", "Runner::Shutdown");
  FX_LOGS(INFO) << "[f2fs] Shutting down";
  fs::PagedVfs::Shutdown([this, cb = std::move(cb)](zx_status_t status) mutable {
    if (f2fs_) {
      f2fs_->Sync([this, status, cb = std::move(cb)](zx_status_t) mutable {
        async::PostTask(dispatcher_, [this, status, cb = std::move(cb)]() mutable {
          f2fs_->PutSuper();
          ZX_ASSERT(f2fs_->TakeBc().is_ok());

          if (on_unmount_) {
            on_unmount_();
          }
          // Tell the unmounting channel that we've completed teardown. This *must* be the last
          // thing we do because after this, the caller can assume that it's safe to destroy the
          // runner.
          cb(status);
        });
      });
    } else {
      async::PostTask(dispatcher_, [this, status, cb = std::move(cb)]() mutable {
        if (on_unmount_) {
          on_unmount_();
        }

        cb(status);
      });
    }
  });
}

Runner::~Runner() {
  // Inform PagedVfs so that it can stop threads that might call out to f2fs.
  TearDown();
}

zx::status<fs::FilesystemInfo> Runner::GetFilesystemInfo() { return f2fs_->GetFilesystemInfo(); }

zx::status<> Runner::ServeRoot(fidl::ServerEnd<fuchsia_io::Directory> root) {
  auto root_vnode = f2fs_->GetRootVnode();
  if (root_vnode.is_error()) {
    FX_LOGS(ERROR) << "failed to get the root vnode. " << root_vnode.status_string();
    return root_vnode.take_error();
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

  auto outgoing = fbl::MakeRefCounted<fs::PseudoDir>();
  outgoing->AddEntry("root", *std::move(root_vnode));

  auto diagnostics_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  outgoing->AddEntry("diagnostics", diagnostics_dir);
  diagnostics_dir->AddEntry(fuchsia::inspect::Tree::Name_, inspect_tree);

  outgoing->AddEntry(
      fidl::DiscoverableProtocolName<fuchsia_fs::Admin>,
      fbl::MakeRefCounted<AdminService>(dispatcher_, [this](fs::FuchsiaVfs::ShutdownCallback cb) {
        this->Shutdown(std::move(cb));
      }));

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
    ZX_ASSERT_MSG(status == ZX_OK, "[f2fs] Filesystem shutdown failed on OnNoConnections(): %s",
                  zx_status_get_string(status));
  });
}

#endif  // __Fuchsia__

}  // namespace f2fs

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/runner.h"

#include <fidl/fuchsia.fs/cpp/wire.h>
#include <fidl/fuchsia.update.verify/cpp/wire.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/storage/blobfs/query.h"

namespace blobfs {

// static.
zx::status<std::unique_ptr<Runner>> Runner::Create(async::Loop* loop,
                                                   std::unique_ptr<BlockDevice> device,
                                                   const MountOptions& options,
                                                   zx::resource vmex_resource) {
  // The runner owns the blobfs, but the runner needs to be created first because it is the Vfs
  // object that Blobfs uses.
  std::unique_ptr<Runner> runner(new Runner(loop, options.paging_threads));
  if (auto status = runner->Init(); status.is_error())
    return status.take_error();

  // All of our pager threads get the deadline profile for scheduling.
  SetDeadlineProfile(runner->GetPagerThreads());

  auto blobfs_or = Blobfs::Create(loop->dispatcher(), std::move(device), runner.get(), options,
                                  std::move(vmex_resource));
  if (blobfs_or.is_error())
    return blobfs_or.take_error();

  runner->blobfs_ = std::move(blobfs_or.value());
  runner->SetReadonly(runner->blobfs_->writability() != Writability::Writable);

  return zx::ok(std::move(runner));
}

Runner::Runner(async::Loop* loop, int32_t paging_threads)
    : fs::PagedVfs(loop->dispatcher(), paging_threads), loop_(loop) {}

Runner::~Runner() = default;

void Runner::Shutdown(fs::FuchsiaVfs::ShutdownCallback cb) {
  TRACE_DURATION("blobfs", "Runner::Unmount");
  // Shutdown all external connections to blobfs.
  ManagedVfs::Shutdown([this, cb = std::move(cb)](zx_status_t status) mutable {
    async::PostTask(dispatcher(), [this, status, cb = std::move(cb)]() mutable {
      // Manually destroy the filesystem. The promise of Shutdown is that no connections are active,
      // and destroying the Runner object should terminate all background workers.
      blobfs_ = nullptr;

      // Tell the mounting thread that the filesystem has terminated.
      loop_->Quit();

      // Tell the unmounting channel that we've completed teardown. This *must* be the last thing we
      // do because after this, the caller can assume that it's safe to destroy the runner.
      cb(status);
    });
  });
}

zx_status_t Runner::ServeRoot(fidl::ServerEnd<fuchsia_io::Directory> root, ServeLayout layout) {
  fbl::RefPtr<fs::Vnode> vn;
  zx_status_t status = blobfs_->OpenRootNode(&vn);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "mount failed; could not get root blob";
    return status;
  }

  // TODO(fxbug.dev/57330): Remove force_private_snapshot when we support requesting different
  // consistency from servers.
  auto inspect_tree = fbl::MakeRefCounted<fs::Service>(
      [connector =
           inspect::MakeTreeHandler(blobfs_->GetMetrics()->inspector(), loop_->dispatcher(),
                                    inspect::TreeHandlerSettings{.force_private_snapshot = true})](
          zx::channel chan) mutable {
        connector(fidl::InterfaceRequest<fuchsia::inspect::Tree>(std::move(chan)));
        return ZX_OK;
      });

  fbl::RefPtr<fs::Vnode> export_root;
  switch (layout) {
    case ServeLayout::kDataRootOnly:
      export_root = std::move(vn);
      break;
    case ServeLayout::kExportDirectory:
      auto outgoing = fbl::MakeRefCounted<fs::PseudoDir>();
      outgoing->AddEntry(kOutgoingDataRoot, std::move(vn));

      auto diagnostics_dir = fbl::MakeRefCounted<fs::PseudoDir>();
      outgoing->AddEntry("diagnostics", diagnostics_dir);
      diagnostics_dir->AddEntry(fuchsia::inspect::Tree::Name_, inspect_tree);

      auto svc_dir = fbl::MakeRefCounted<fs::PseudoDir>();
      outgoing->AddEntry("svc", svc_dir);

      query_svc_ = fbl::MakeRefCounted<QueryService>(loop_->dispatcher(), blobfs_.get(), this);
      svc_dir->AddEntry(fidl::DiscoverableProtocolName<fuchsia_fs::Query>, query_svc_);

      health_check_svc_ = fbl::MakeRefCounted<HealthCheckService>(loop_->dispatcher(), *blobfs_);
      svc_dir->AddEntry(fidl::DiscoverableProtocolName<fuchsia_update_verify::BlobfsVerifier>,
                        health_check_svc_);

      export_root = std::move(outgoing);
      break;
  }

  status = ServeDirectory(std::move(export_root), std::move(root));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "mount failed; could not serve root directory";
    return status;
  }
  return ZX_OK;
}

bool Runner::IsReadonly() {
  std::lock_guard lock(vfs_lock_);
  return ReadonlyLocked();
}

}  // namespace blobfs

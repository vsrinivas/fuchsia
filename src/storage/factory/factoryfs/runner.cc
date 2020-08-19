// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/factory/factoryfs/runner.h"

#include <fuchsia/fs/llcpp/fidl.h>

#include <fbl/auto_lock.h>
#include <fs/pseudo_dir.h>

#include "src/storage/factory/factoryfs/query.h"

namespace factoryfs {

zx_status_t Runner::Create(async::Loop* loop, std::unique_ptr<BlockDevice> device,
                           MountOptions* options, std::unique_ptr<Runner>* out) {
  std::unique_ptr<Factoryfs> fs;
  zx_status_t status = Factoryfs::Create(loop->dispatcher(), std::move(device), options, &fs);
  if (status != ZX_OK) {
    return status;
  }
  auto runner = std::unique_ptr<Runner>(new Runner(loop, std::move(fs)));
  *out = std::move(runner);
  return ZX_OK;
}

void Runner::Shutdown(fs::Vfs::ShutdownCallback cb) {
  FS_TRACE_INFO("factoryfs: Shutdown\n");
  // Shutdown all external connections to Factoryfs.
  ManagedVfs::Shutdown([this, cb = std::move(cb)](zx_status_t status) mutable {
    async::PostTask(dispatcher(), [this, status, cb = std::move(cb)]() mutable {
      // Manually destroy the filesystem.
      // factoryfs_ = nullptr;
      // Tell the mounting thread that the filesystem has terminated.
      loop_->Quit();

      // Tell the unmounting channel that we've completed teardown. This *must* be the last thing we
      // do because after this, the caller can assume that it's safe to destroy the runner.
      cb(status);
    });
  });
}

zx_status_t Runner::ServeRoot(zx::channel root, ServeLayout layout) {
  fbl::RefPtr<fs::Vnode> vn;
  zx_status_t status = factoryfs_->OpenRootNode(&vn);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("factoryfs: mount failed; could not get root node\n");
    return status;
  }

  fbl::RefPtr<fs::Vnode> export_root;
  switch (layout) {
    case ServeLayout::kDataRootOnly:
      export_root = std::move(vn);
      break;
    case ServeLayout::kExportDirectory:
      auto outgoing = fbl::MakeRefCounted<fs::PseudoDir>();
      outgoing->AddEntry(kOutgoingDataRoot, std::move(vn));
      auto svc_dir = fbl::MakeRefCounted<fs::PseudoDir>();
      outgoing->AddEntry("svc", svc_dir);
      query_svc_ = fbl::MakeRefCounted<QueryService>(loop_->dispatcher(), factoryfs_.get(), this);
      svc_dir->AddEntry(::llcpp::fuchsia::fs::Query::Name, query_svc_);
      export_root = std::move(outgoing);
      break;
  }

  status = ServeDirectory(std::move(export_root), std::move(root));
  if (status != ZX_OK) {
    FS_TRACE_ERROR("factoryfs: mount failed; could not serve root directory\n");
    return status;
  }
  return ZX_OK;
}

Runner::Runner(async::Loop* loop, std::unique_ptr<Factoryfs> fs)
    : ManagedVfs(loop->dispatcher()), loop_(loop), factoryfs_(std::move(fs)) {}

bool Runner::IsReadonly() const { return true; }

}  // namespace factoryfs

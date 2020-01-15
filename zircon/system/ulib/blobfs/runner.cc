// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runner.h"

#include <fuchsia/fs/llcpp/fidl.h>

#include <fs/pseudo_dir.h>

#include "query.h"

namespace blobfs {

// static.
zx_status_t Runner::Create(async::Loop* loop, std::unique_ptr<BlockDevice> device,
                           MountOptions* options, std::unique_ptr<Runner>* out) {
  std::unique_ptr<Blobfs> fs;
  zx_status_t status = Blobfs::Create(loop->dispatcher(), std::move(device), options, &fs);
  if (status != ZX_OK) {
    return status;
  }

  auto runner = std::unique_ptr<Runner>(new Runner(loop, std::move(fs)));
  *out = std::move(runner);
  return ZX_OK;
}

Runner::Runner(async::Loop* loop, std::unique_ptr<Blobfs> fs)
    : ManagedVfs(loop->dispatcher()), loop_(loop), blobfs_(std::move(fs)) {
  SetReadonly(blobfs_->writability() != Writability::Writable);
}

Runner::~Runner() {}

void Runner::Shutdown(fs::Vfs::ShutdownCallback cb) {
  TRACE_DURATION("blobfs", "Runner::Unmount");
  // Shutdown all external connections to blobfs.
  ManagedVfs::Shutdown([this, cb = std::move(cb)](zx_status_t status) mutable {
    async::PostTask(dispatcher(), [this, status, cb = std::move(cb)]() mutable {
      // Manually destroy the filesystem. The promise of Shutdown is that no
      // connections are active, and destroying the Runner object
      // should terminate all background workers.
      blobfs_ = nullptr;

      // Tell the unmounting channel that we've completed teardown.
      cb(status);

      // Tell the mounting thread that the filesystem has terminated.
      loop_->Quit();
    });
  });
}

zx_status_t Runner::ServeRoot(zx::channel root, ServeLayout layout) {
  fbl::RefPtr<fs::Vnode> vn;
  zx_status_t status = blobfs_->OpenRootNode(&vn);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: mount failed; could not get root blob\n");
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
      query_svc_ = fbl::MakeRefCounted<QueryService>(loop_->dispatcher(), blobfs_.get(), this);
      svc_dir->AddEntry(::llcpp::fuchsia::fs::Query::Name, query_svc_);
      export_root = std::move(outgoing);
      break;
  }

  status = ServeDirectory(std::move(export_root), std::move(root));
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: mount failed; could not serve root directory\n");
    return status;
  }
  return ZX_OK;
}

bool Runner::IsReadonly() {
#ifdef __Fuchsia__
  fbl::AutoLock lock(&vfs_lock_);
#endif
  return ReadonlyLocked();
}

}  // namespace blobfs

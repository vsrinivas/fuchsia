// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/factory/factoryfs/runner.h"

#include <fidl/fuchsia.fs/cpp/wire.h>
#include <lib/syslog/cpp/macros.h>

#include <fbl/auto_lock.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/storage/factory/factoryfs/admin_service.h"

namespace factoryfs {

zx::status<std::unique_ptr<Runner>> Runner::Create(async::Loop* loop,
                                                   std::unique_ptr<BlockDevice> device,
                                                   MountOptions* options) {
  auto runner = std::unique_ptr<Runner>(new Runner(loop));

  auto fs_or = Factoryfs::Create(loop->dispatcher(), std::move(device), options, runner.get());
  if (fs_or.is_error())
    return fs_or.take_error();

  runner->factoryfs_ = std::move(fs_or.value());
  return zx::ok(std::move(runner));
}

void Runner::Shutdown(fs::FuchsiaVfs::ShutdownCallback cb) {
  FX_LOGS(INFO) << "Shutdown";
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

zx::status<fs::FilesystemInfo> Runner::GetFilesystemInfo() {
  return factoryfs_->GetFilesystemInfo();
}

zx_status_t Runner::ServeRoot(fidl::ServerEnd<fuchsia_io::Directory> root) {
  fbl::RefPtr<fs::Vnode> vn;
  zx_status_t status = factoryfs_->OpenRootNode(&vn);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "mount failed; could not get root node";
    return status;
  }

  fbl::RefPtr<fs::Vnode> export_root;
  auto outgoing = fbl::MakeRefCounted<fs::PseudoDir>();
  outgoing->AddEntry(kOutgoingDataRoot, std::move(vn));

  outgoing->AddEntry(fidl::DiscoverableProtocolName<fuchsia_fs::Admin>,
                     fbl::MakeRefCounted<AdminService>(loop_->dispatcher(), *this));

  status = ServeDirectory(std::move(outgoing), std::move(root));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "mount failed; could not serve root directory";
    return status;
  }
  return ZX_OK;
}

Runner::Runner(async::Loop* loop) : ManagedVfs(loop->dispatcher()), loop_(loop) {}

bool Runner::IsReadonly() const { return true; }

}  // namespace factoryfs

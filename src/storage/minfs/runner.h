// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_RUNNER_H_
#define SRC_STORAGE_MINFS_RUNNER_H_

#ifdef __Fuchsia__
#include <lib/async-loop/cpp/loop.h>

#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#endif

#include <lib/zx/status.h>

#include "src/storage/minfs/minfs_private.h"
#include "src/storage/minfs/mount.h"

namespace minfs {

// A wrapper class around a "Minfs" object which manages the external FIDL connections.
class Runner final : public PlatformVfs {
 public:
  Runner(const Runner&) = delete;
  Runner& operator=(const Runner&) = delete;

  static zx::status<std::unique_ptr<Runner>> Create(FuchsiaDispatcher dispatcher,
                                                    std::unique_ptr<Bcache> bc,
                                                    const MountOptions& options);

  static std::unique_ptr<Bcache> Destroy(std::unique_ptr<Runner> runner);

#ifdef __Fuchsia__
  // ManagedVfs implementation.
  void Shutdown(fs::FuchsiaVfs::ShutdownCallback cb) final;
  zx::status<fs::FilesystemInfo> GetFilesystemInfo() final;
  void OnNoConnections() final;

  zx::status<> ServeRoot(fidl::ServerEnd<fuchsia_io::Directory> root);
#endif

  void SetUnmountCallback(fit::closure on_unmount) { on_unmount_ = std::move(on_unmount); }

  Minfs& minfs() { return *minfs_; }

 private:
  explicit Runner(FuchsiaDispatcher dispatcher);

  // Check if filesystem is readonly.
  bool IsReadonly() const __TA_EXCLUDES(vfs_lock_);

#ifdef __Fuchsia__
  async_dispatcher_t* dispatcher_;
#endif

  std::unique_ptr<Minfs> minfs_;
  fit::closure on_unmount_;
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_RUNNER_H_

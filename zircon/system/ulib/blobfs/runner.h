// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_RUNNER_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_RUNNER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <blobfs/mount.h>
#include <fs/managed_vfs.h>
#include <fs/trace.h>
#include <fs/vfs.h>
#include <fs/vnode.h>
#include <trace/event.h>

#include "blobfs.h"

namespace blobfs {

class QueryService;

// A wrapper class around a "Blobfs" object which additionally manages
// external IPC connections.
//
// Using this interface, a caller can initialize a Blobfs object and access
// the filesystem hierarchy through the ulib/fs Vnode classes, but not modify
// the internal structure of the filesystem.
class Runner : public fs::ManagedVfs {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Runner);

  virtual ~Runner();

  static zx_status_t Create(async::Loop* loop, std::unique_ptr<BlockDevice> device,
                            MountOptions* options, std::unique_ptr<Runner>* out);

  // fs::ManagedVfs interface.

  void Shutdown(fs::Vfs::ShutdownCallback closure) final;

  // Other methods.

  // Serves the root directory of the filesystem using |root| as the server-end
  // of an IPC connection.
  zx_status_t ServeRoot(zx::channel root, ServeLayout layout);

 private:
  Runner(async::Loop* loop, std::unique_ptr<Blobfs> fs);

  // Check if filesystem is readonly.
  bool IsReadonly() FS_TA_EXCLUDES(vfs_lock_);

  async::Loop* loop_;
  std::unique_ptr<Blobfs> blobfs_;
  fbl::RefPtr<QueryService> query_svc_;
};

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_RUNNER_H_

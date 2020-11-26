// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FACTORY_FACTORYFS_RUNNER_H_
#define SRC_STORAGE_FACTORY_FACTORYFS_RUNNER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/trace/event.h>
#include <lib/zx/resource.h>

#include <fs/managed_vfs.h>
#include <fs/trace.h>
#include <fs/vfs.h>
#include <fs/vnode.h>

#include "src/storage/factory/factoryfs/factoryfs.h"
#include "src/storage/factory/factoryfs/mount.h"

namespace factoryfs {

constexpr char kOutgoingDataRoot[] = "root";

class QueryService;

// A wrapper class around a "Factoryfs" object which additionally manages
// external IPC connections.
//
// Using this interface, a caller can initialize a Factoryfs object and access
// the filesystem through the ulib/fs Vnode classes, but not modify
// the internal structure of the filesystem.
class Runner : public fs::ManagedVfs {
 public:
  Runner(const Runner&) = delete;
  Runner(Runner&&) = delete;
  Runner& operator=(const Runner&) = delete;
  Runner& operator=(Runner&&) = delete;

  static zx_status_t Create(async::Loop* loop, std::unique_ptr<BlockDevice> device,
                            MountOptions* options, std::unique_ptr<Runner>* out);

  // fs::ManagedVfs interface.

  void Shutdown(fs::Vfs::ShutdownCallback closure) final;

  // Other methods.

  // Serves the root directory of the filesystem using |root| as the server-end
  // of an IPC connection.
  zx_status_t ServeRoot(zx::channel root, ServeLayout layout);

 private:
  Runner(async::Loop* loop, std::unique_ptr<Factoryfs> fs);
  bool IsReadonly() const;
  async::Loop* loop_;
  std::unique_ptr<Factoryfs> factoryfs_;
  fbl::RefPtr<QueryService> query_svc_;
};

}  // namespace factoryfs

#endif  // SRC_STORAGE_FACTORY_FACTORYFS_RUNNER_H_

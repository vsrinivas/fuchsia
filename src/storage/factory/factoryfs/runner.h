// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FACTORY_FACTORYFS_RUNNER_H_
#define SRC_STORAGE_FACTORY_FACTORYFS_RUNNER_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/trace/event.h>
#include <lib/zx/resource.h>

#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/lib/storage/vfs/cpp/vnode.h"
#include "src/storage/factory/factoryfs/factoryfs.h"
#include "src/storage/factory/factoryfs/mount.h"

namespace factoryfs {

constexpr char kOutgoingDataRoot[] = "root";

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

  static zx::result<std::unique_ptr<Runner>> Create(async::Loop* loop,
                                                    std::unique_ptr<BlockDevice> device,
                                                    MountOptions* options);

  // fs::ManagedVfs interface.
  void Shutdown(fs::FuchsiaVfs::ShutdownCallback closure) final;
  zx::result<fs::FilesystemInfo> GetFilesystemInfo() final;

  // Other methods.

  // Serves the root directory of the filesystem using |root| as the server-end
  // of an IPC connection.
  zx_status_t ServeRoot(fidl::ServerEnd<fuchsia_io::Directory> root);

 private:
  explicit Runner(async::Loop* loop);
  bool IsReadonly() const;
  async::Loop* loop_;
  std::unique_ptr<Factoryfs> factoryfs_;
};

}  // namespace factoryfs

#endif  // SRC_STORAGE_FACTORY_FACTORYFS_RUNNER_H_

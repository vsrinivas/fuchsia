// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_RUNNER_H_
#define SRC_STORAGE_BLOBFS_RUNNER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/trace/event.h>
#include <lib/zx/resource.h>
#include <lib/zx/status.h>

#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#include "src/lib/storage/vfs/cpp/paged_vfs.h"
#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/mount.h"

namespace blobfs {

// A wrapper class around a "Blobfs" object which additionally manages external IPC connections.
//
// Using this interface, a caller can initialize a Blobfs object and access the filesystem hierarchy
// through the ulib/fs Vnode classes, but not modify the internal structure of the filesystem.
//
// The Runner class *has* to be final because it calls PagedVfs::TearDown from
// its destructor which is required to ensure thread-safety at destruction time.
class Runner final : public fs::PagedVfs {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Runner);

  virtual ~Runner();

  static zx::result<std::unique_ptr<Runner>> Create(async::Loop* loop,
                                                    std::unique_ptr<BlockDevice> device,
                                                    const MountOptions& options,
                                                    zx::resource vmex_resource);

  // fs::PagedVfs implementation.
  void Shutdown(fs::FuchsiaVfs::ShutdownCallback closure) final;
  zx::result<fs::FilesystemInfo> GetFilesystemInfo() final;

  // Serves the root directory of the filesystem using |root| as the server-end of an IPC
  // connection.
  zx_status_t ServeRoot(fidl::ServerEnd<fuchsia_io::Directory> root);

 private:
  Runner(async::Loop* loop, int32_t paging_threads);

  // Check if filesystem is readonly.
  bool IsReadonly() __TA_EXCLUDES(vfs_lock_);

  async::Loop* loop_;
  std::unique_ptr<Blobfs> blobfs_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_RUNNER_H_

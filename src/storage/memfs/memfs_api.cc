// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/loop.h>
#include <lib/fdio/namespace.h>
#include <lib/fit/function.h>
#include <lib/memfs/memfs.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <zircon/assert.h>

#include <memory>
#include <string>

#include "src/storage/memfs/memfs.h"
#include "src/storage/memfs/vnode_dir.h"

struct memfs_filesystem {
 public:
  // Creates a memfs instance associated with the given dispatcher.
  static zx::result<memfs_filesystem> Create(async_dispatcher_t* dispatcher) {
    auto fs_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    if (fs_endpoints.is_error())
      return fs_endpoints.take_error();

    std::unique_ptr<memfs::Memfs> memfs;
    fbl::RefPtr<memfs::VnodeDir> root;
    if (zx_status_t status = memfs::Memfs::Create(dispatcher, "<tmp>", &memfs, &root);
        status != ZX_OK) {
      return zx::error(status);
    }

    if (zx_status_t status =
            memfs->ServeDirectory(std::move(root), std::move(fs_endpoints->server));
        status != ZX_OK) {
      return zx::error(status);
    }

    return zx::ok(memfs_filesystem(std::move(memfs), fs_endpoints->client.TakeChannel()));
  }

  // Moveable but not copyable.
  memfs_filesystem(const memfs_filesystem&) = delete;
  memfs_filesystem(memfs_filesystem&&) = default;

  // If AsyncTearDown() has not been called, does synchronous tear-down, blocking on cleanup. The
  // message loop (dispatcher passed to Create()) must still be alive or this will deadlock.
  ~memfs_filesystem() {
    if (memfs_) {
      // Need to synchronize on teardown.
      sync_completion_t unmounted;
      AsyncTearDown([&unmounted](zx_status_t) { sync_completion_signal(&unmounted); });
      sync_completion_wait(&unmounted, zx::duration::infinite().get());
    }
  }

  // Takes ownership of the root() channel and installs it at the given path. The root() must be
  // a valid handle before this call (ZX_ERR_BAD_STATE will be returned if not) and it will be
  // cleared before the call completes.
  //
  // The mounted path will be automatically unmounted at tear-down.
  zx_status_t MountAt(std::string path) {
    if (!root_)
      return ZX_ERR_BAD_STATE;
    if (path.empty())
      return ZX_ERR_NOT_SUPPORTED;

    if (zx_status_t status = fdio_ns_get_installed(&namespace_); status != ZX_OK)
      return status;

    mounted_path_ = std::move(path);
    return fdio_ns_bind(namespace_, mounted_path_.c_str(), root_.release());
  }

  // Deleting the setup via the destructor will trigger synchronous teardown and block on the
  // filesystem cleanup (which might be on another thread or happen in the future on the current
  // one).
  //
  // This function allows clients to trigger asynchronous cleanup. The callback will get called
  // ON THE MEMFS THREAD (the dispatcher passed into Create()) class was created) after Memfs has
  // been deleted with the status value from memfs teardown. After this call, the memfs_filesystem
  // object can get deleted and memfs may outlive it.
  void AsyncTearDown(fit::callback<void(zx_status_t)> cb) {
    ZX_DEBUG_ASSERT(memfs_);

    if (!mounted_path_.empty()) {
      // If unmounting fails we continue with tear-down since there's not much else to do.
      fdio_ns_unbind(namespace_, mounted_path_.c_str());
    }

    memfs::Memfs* memfs_ptr = memfs_.get();  // Need to both use & move in below line.
    memfs_ptr->Shutdown(
        [memfs = std::move(memfs_), cb = std::move(cb)](zx_status_t status) mutable {
          memfs.reset();  // Release memfs class class before signaling.
          cb(status);
        });
  }

  // The channel to the root directory of the filesystem. Users can move this out, close it, or use
  // in-place as they need.
  //
  // InstallRootAt() will take ownership of the root and clear this handle.
  zx::channel& root() { return root_; }
  const zx::channel& root() const { return root_; }

 private:
  memfs_filesystem(std::unique_ptr<memfs::Memfs> memfs, zx::channel root)
      : memfs_(std::move(memfs)), root_(std::move(root)) {}

  std::unique_ptr<memfs::Memfs> memfs_;
  zx::channel root_;

  fdio_ns_t* namespace_ = nullptr;  // Set when mounted (for unmounting).
  std::string mounted_path_;        // Empty if not mounted.
};

zx_status_t memfs_create_filesystem(async_dispatcher_t* dispatcher, memfs_filesystem_t** out_fs,
                                    zx_handle_t* out_root) {
  ZX_DEBUG_ASSERT(dispatcher != nullptr);
  ZX_DEBUG_ASSERT(out_fs != nullptr);
  ZX_DEBUG_ASSERT(out_root != nullptr);

  zx::result<memfs_filesystem> setup_or = memfs_filesystem::Create(dispatcher);
  if (setup_or.is_error())
    return setup_or.error_value();

  *out_root = setup_or->root().release();
  *out_fs = new memfs_filesystem_t(std::move(*setup_or));
  return ZX_OK;
}

zx_status_t memfs_install_at(async_dispatcher_t* dispatcher, const char* path,
                             memfs_filesystem_t** out_fs) {
  ZX_DEBUG_ASSERT(dispatcher);
  ZX_DEBUG_ASSERT(path);
  ZX_DEBUG_ASSERT(out_fs);

  zx::result<memfs_filesystem> setup_or = memfs_filesystem::Create(dispatcher);
  if (setup_or.is_error())
    return setup_or.error_value();

  if (zx_status_t status = setup_or->MountAt(path); status != ZX_OK)
    return status;

  *out_fs = new memfs_filesystem_t(std::move(*setup_or));
  return ZX_OK;
}

void memfs_free_filesystem(memfs_filesystem_t* fs, sync_completion_t* unmounted) {
  ZX_DEBUG_ASSERT(fs);

  // Note: This deletes the memfs_filesystem_t pointer on the memfs thread which might be different
  // than the current one.
  fs->AsyncTearDown([fs, unmounted](zx_status_t status) {
    delete fs;
    if (unmounted) {
      sync_completion_signal(unmounted);
    }
  });
}

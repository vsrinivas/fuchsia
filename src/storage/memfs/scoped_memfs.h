// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MEMFS_SCOPED_MEMFS_H_
#define SRC_STORAGE_MEMFS_SCOPED_MEMFS_H_

#include <lib/memfs/memfs.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <lib/zx/result.h>

// A wrapper around the C API that sets up and tears down memfs.
//
// There are two modes of operation:
//
//  - Call ScopedMemfs::Create() and then use the root() channel to talk to the root directory of
//    the filesystem. This will give a memfs instance not mounted in any location. You can also
//    choose to mount it in your namespace manually.
//
//  - The more common mode is to use ScopedMemfs::CreateMountedAt() which will automatically mount
//    the new memfs instance at the given path in your local namespace. This will be unmounted on
//    cleanup.
//
// Memfs will run on the given dispatcher. This must be a different thread from that of the
// ScopedMemfs object because the destructor will synchronize with memfs' cleanup and if this is
// the same thread it will deadlock.
class ScopedMemfs {
 public:
  static zx::result<ScopedMemfs> Create(async_dispatcher_t* dispatcher) {
    memfs_filesystem_t* fs = nullptr;
    zx_handle_t root = 0;

    zx_status_t status = memfs_create_filesystem(dispatcher, &fs, &root);
    if (status != ZX_OK)
      return zx::error(status);

    return zx::ok(ScopedMemfs(fs, root));
  }

  static zx::result<ScopedMemfs> CreateMountedAt(async_dispatcher_t* dispatcher, const char* path) {
    memfs_filesystem_t* fs = nullptr;
    zx_status_t status = memfs_install_at(dispatcher, path, &fs);
    if (status != ZX_OK)
      return zx::error(status);

    return zx::ok(ScopedMemfs(fs, ZX_HANDLE_INVALID));
  }

  // Moveable but not copyable.
  ScopedMemfs(const ScopedMemfs&) = delete;
  ScopedMemfs(ScopedMemfs&& other)
      : cleanup_timeout_(other.cleanup_timeout_),
        memfs_(other.memfs_),
        root_(std::move(other.root_)) {
    // Need to explicitly clear out the raw pointer since the default move will not do this.
    // Otherwise, the "moved from" ScopedMemfs will still try to clean up the filesystem.
    other.memfs_ = nullptr;
  }

  // Blocks on cleanup/shutdown for "cleanup_timeout_" time, see set_cleanup_timeout(). The
  // dispatcher must still be running for this to succeed.
  ~ScopedMemfs() {
    if (memfs_) {
      sync_completion_t unmounted;
      memfs_free_filesystem(memfs_, &unmounted);
      sync_completion_wait(&unmounted, cleanup_timeout_.get());
    }
  }

  // Set the timeout that this class will wait for memfs cleanup on the dispatcher thread. By
  // default this is infinite. In practice, memfs cleanup is fast and deterministic so if you
  // encounter hangs it indicates a more serious problem like the associated dispatcher is no
  // longer running.
  void set_cleanup_timeout(zx::duration duration) { cleanup_timeout_ = duration; }

  // The channel to the root directory of the filesystem. Users can move this out, close it, or use
  // in-place as they need.
  zx::channel& root() { return root_; }
  const zx::channel& root() const { return root_; }

 private:
  ScopedMemfs(memfs_filesystem_t* memfs, zx_handle_t root) : memfs_(memfs), root_(root) {}

  zx::duration cleanup_timeout_ = zx::duration::infinite();

  memfs_filesystem_t* memfs_;
  zx::channel root_;
};

#endif  // SRC_STORAGE_MEMFS_SCOPED_MEMFS_H_

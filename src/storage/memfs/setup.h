// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MEMFS_SETUP_H_
#define SRC_STORAGE_MEMFS_SETUP_H_

#include <lib/async-loop/loop.h>
#include <lib/fdio/namespace.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>

#include <memory>
#include <string>

namespace memfs {

class Memfs;

// Handles setup and optionally synchronous/asynchronous teardown of memfs.
//
// This class will create a memfs instance on an existing message loop. It then exposes a FIDL
// connection to the root directory of this filesystem via root() which the client would use to
// modify it. The client can alternatively mount this in its filesystem namespace via
// MountAt() and use the standard I/O functions to access the contents by name.
//
// THREADSAFETY: This class by itself is NOT threadsafe. Memfs can be running on another thread (the
// passed-in dispatcher determines this), but the lifetime of this class and its accessors/mutators
// are not synchronized.
class Setup {
 public:
  // Creates a memfs instance associated with the given dispatcher.
  static zx::status<Setup> Create(async_dispatcher_t* dispatcher);

  // Moveable but not copyable.
  Setup(const Setup&) = delete;
  Setup(Setup&&);

  // If AsyncTearDown() has not been called, does synchronous tear-down, blocking on cleanup. The
  // message loop (dispatcher passed to Create()) must still be alive or this will deadlock.
  ~Setup();

  // Takes ownership of the root() channel and installs it at the given path. The root() must be
  // a valid handle before this call (ZX_ERR_BAD_STATE will be returned if not) and it will be
  // cleared before the call completes.
  //
  // The mounted path will be automatically unmounted at tear-down.
  zx_status_t MountAt(const char* path);

  // Deleting the setup via the destructor will trigger synchronous teardown and block on the
  // filesystem cleanup (which might be on another thread or happen in the future on the current
  // one).
  //
  // This function allows clients to trigger asynchronous cleanup. The callback will get called
  // ON THE MEMFS THREAD (the dispatcher passed into Create()) class was created) after Memfs has
  // been deleted with the status value from memfs teardown. After this call, the Setup object can
  // get deleted and memfs may outlive it.
  void AsyncTearDown(fit::callback<void(zx_status_t)> cb);

  // The channel to the root directory of the filesystem. Users can move this out, close it, or use
  // in-place as they need.
  //
  // InstallRootAt() will take ownership of the root and clear this handle.
  zx::channel& root() { return root_; }
  const zx::channel& root() const { return root_; }

 private:
  Setup(std::unique_ptr<Memfs> memfs, zx::channel root);

  std::unique_ptr<Memfs> memfs_;
  zx::channel root_;

  fdio_ns_t* namespace_ = nullptr;  // Set when mounted (for unmounting).
  std::string mounted_path_;        // Empty if not mounted.
};

}  // namespace memfs

#endif  // SRC_STORAGE_MEMFS_SETUP_H_

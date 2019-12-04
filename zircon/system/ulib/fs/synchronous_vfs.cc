// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/sync/completion.h>

#include <memory>
#include <utility>

#include <fs/synchronous_vfs.h>

namespace fs {

SynchronousVfs::SynchronousVfs() : is_shutting_down_(false) {}

SynchronousVfs::SynchronousVfs(async_dispatcher_t* dispatcher)
    : Vfs(dispatcher), is_shutting_down_(false) {}

SynchronousVfs::~SynchronousVfs() {
  Shutdown(nullptr);
  ZX_DEBUG_ASSERT(connections_.is_empty());
}

// Synchronously drop all connections.
void SynchronousVfs::Shutdown(ShutdownCallback handler) {
  is_shutting_down_ = true;

  UninstallAll(zx::time::infinite());
  while (!connections_.is_empty()) {
    connections_.front().SyncTeardown();
  }
  ZX_ASSERT_MSG(connections_.is_empty(), "Failed to complete VFS shutdown");
  if (handler) {
    handler(ZX_OK);
  }
}

void SynchronousVfs::RegisterConnection(std::unique_ptr<internal::Connection> connection) {
  ZX_DEBUG_ASSERT(!is_shutting_down_);
  connections_.push_back(std::move(connection));
}

void SynchronousVfs::UnregisterConnection(internal::Connection* connection) {
  // We drop the result of |erase| on the floor, effectively destroying the
  // connection.
  connections_.erase(*connection);
}

bool SynchronousVfs::IsTerminating() const { return is_shutting_down_; }

}  // namespace fs

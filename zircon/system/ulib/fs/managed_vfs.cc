// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/sync/completion.h>

#include <memory>
#include <utility>

#include <fbl/auto_lock.h>
#include <fs/managed_vfs.h>

namespace fs {

ManagedVfs::ManagedVfs() : is_shutting_down_(false) {}

ManagedVfs::ManagedVfs(async_dispatcher_t* dispatcher)
    : Vfs(dispatcher), is_shutting_down_(false) {}

ManagedVfs::~ManagedVfs() { ZX_DEBUG_ASSERT(connections_.is_empty()); }

bool ManagedVfs::IsTerminated() const {
  return is_shutting_down_.load() && connections_.is_empty();
}

// Asynchronously drop all connections.
void ManagedVfs::Shutdown(ShutdownCallback handler) {
  ZX_DEBUG_ASSERT(handler);
  zx_status_t status =
      async::PostTask(dispatcher(), [this, closure = std::move(handler)]() mutable {
        fbl::AutoLock<fbl::Mutex> lock(&lock_);
        ZX_DEBUG_ASSERT(!shutdown_handler_);
        shutdown_handler_ = std::move(closure);
        is_shutting_down_.store(true);

        UninstallAll(zx::time::infinite());

        // Signal the teardown on channels in a way that doesn't potentially
        // pull them out from underneath async callbacks.
        for (auto& c : connections_) {
          c.AsyncTeardown();
        }

        CheckForShutdownComplete();
      });
  ZX_DEBUG_ASSERT(status == ZX_OK);
}

// Trigger "OnShutdownComplete" if all preconditions have been met.
void ManagedVfs::CheckForShutdownComplete() {
  if (IsTerminated()) {
    shutdown_task_.Post(dispatcher());
  }
}

void ManagedVfs::OnShutdownComplete(async_dispatcher_t*, async::TaskBase*, zx_status_t status) {
  fbl::AutoLock<fbl::Mutex> lock(&lock_);
  ZX_ASSERT_MSG(IsTerminated(), "Failed to complete VFS shutdown: dispatcher status = %d\n",
                status);
  ZX_DEBUG_ASSERT(shutdown_handler_);

  auto handler = std::move(shutdown_handler_);
  handler(status);
}

void ManagedVfs::RegisterConnection(std::unique_ptr<internal::Connection> connection) {
  fbl::AutoLock<fbl::Mutex> lock(&lock_);
  ZX_DEBUG_ASSERT(!is_shutting_down_.load());
  connections_.push_back(std::move(connection));
}

void ManagedVfs::UnregisterConnection(internal::Connection* connection) {
  fbl::AutoLock<fbl::Mutex> lock(&lock_);
  // We drop the result of |erase| on the floor, effectively destroying the
  // connection when all other references (like async callbacks) have
  // completed.
  connections_.erase(*connection);
  CheckForShutdownComplete();
}

bool ManagedVfs::IsTerminating() const { return is_shutting_down_.load(); }

}  // namespace fs

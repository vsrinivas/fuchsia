// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/managed-vfs.h>

#include <lib/async/cpp/task.h>
#include <fbl/atomic.h>
#include <fbl/unique_ptr.h>
#include <sync/completion.h>

namespace fs {

ManagedVfs::ManagedVfs() : is_shutting_down_(false) {}

ManagedVfs::ManagedVfs(async_t* async) : Vfs(async), is_shutting_down_(false) {}

ManagedVfs::~ManagedVfs() {
    if (!connections_.is_empty()) {
        printf("WARNING: ManagedVfs destroyed with live connections.\n"
               "         Call ManagedVfs::Shutdown before terminating.\n");
    }
    // TODO(US-480): Assert that connections is empty.
}

// Asynchronously drop all connections.
void ManagedVfs::Shutdown(ShutdownCallback handler) {
    ZX_DEBUG_ASSERT(handler);
    zx_status_t status = async::PostTask(async(), [this, closure = fbl::move(handler)]() mutable {
        ZX_DEBUG_ASSERT(!shutdown_handler_);
        shutdown_handler_ = fbl::move(closure);
        is_shutting_down_ = true;

        if (connections_.is_empty()) {
            CheckForShutdownComplete();
        } else {
            // Signal the teardown on channels in a way that doesn't potentially
            // pull them out from underneath async callbacks.
            for (auto& c : connections_) {
                c.SignalTeardown();
            }
        }
    });
    ZX_DEBUG_ASSERT(status == ZX_OK);
}

// Trigger "OnShutdownComplete" if all preconditions have been met.
void ManagedVfs::CheckForShutdownComplete() {
    if (connections_.is_empty() && is_shutting_down_) {
        shutdown_task_.Post(async());
    }
}

void ManagedVfs::OnShutdownComplete(async_t*, async::TaskBase*, zx_status_t status) {
    ZX_ASSERT_MSG(connections_.is_empty(),
                  "Failed to complete VFS shutdown: dispatcher status = %d\n", status);
    ZX_DEBUG_ASSERT(shutdown_handler_);
    ZX_DEBUG_ASSERT(is_shutting_down_);

    UninstallAll(zx_deadline_after(ZX_SEC(5)));
    auto handler = fbl::move(shutdown_handler_);
    handler(status);
}

void ManagedVfs::RegisterConnection(fbl::unique_ptr<Connection> connection) {
    ZX_DEBUG_ASSERT(!is_shutting_down_);
    connections_.push_back(fbl::move(connection));
}

void ManagedVfs::UnregisterConnection(Connection* connection) {
    // We drop the result of |erase| on the floor, effectively destroying the
    // connection when all other references (like async callbacks) have
    // completed.
    connections_.erase(*connection);
    CheckForShutdownComplete();
}

bool ManagedVfs::IsTerminating() const {
    return is_shutting_down_;
}

} // namespace fs

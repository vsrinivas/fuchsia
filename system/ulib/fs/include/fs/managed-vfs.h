// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <lib/async/cpp/task.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/function.h>
#include <fbl/unique_ptr.h>
#include <fs/connection.h>
#include <fs/vfs.h>

namespace fs {

// A specialization of |Vfs| which tears down all active connections when it
// is destroyed.
//
// Unlike |Vfs|, this class is NOT thread-safe and it must be used with a
// single-threaded asynchronous dispatcher. It is unsafe to shutdown the
// dispatch loop before shutting down the ManagedVfs object.
class ManagedVfs : public Vfs {
public:
    ManagedVfs();
    ManagedVfs(async_t* async);

    // The ManagedVfs destructor is only safe to execute if
    // no connections are actively registered.
    //
    // To ensure that this state is achieved, it is recommended that
    // clients issue a call to |Shutdown| before calling the destructor.
    ~ManagedVfs() override;

    // Asynchronously drop all connections managed by the VFS.
    //
    // Invokes |handler| once when all connections are destroyed.
    // It is safe to delete ManagedVfs from within the closure.
    //
    // It is unsafe to call Shutdown multiple times.
    void Shutdown(ShutdownCallback handler) override;

private:
    // Posts the task for OnShutdownComplete if it is safe to do so.
    void CheckForShutdownComplete();

    // Invokes the handler from |Shutdown| once all connections have been
    // released. Additionally, unmounts all sub-mounted filesystems, if any
    // exist.
    void OnShutdownComplete(async_t*, async::TaskBase*, zx_status_t status);

    void RegisterConnection(fbl::unique_ptr<Connection> connection) final;
    void UnregisterConnection(Connection* connection) final;
    bool IsTerminating() const final;

    fbl::DoublyLinkedList<fbl::unique_ptr<Connection>> connections_;

    bool is_shutting_down_;
    async::TaskMethod<ManagedVfs, &ManagedVfs::OnShutdownComplete> shutdown_task_{this};
    ShutdownCallback shutdown_handler_;
};

} // namespace fs

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <stdint.h>

#include <lib/async/cpp/wait.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fs/vfs.h>
#include <fs/vnode.h>
#include <lib/zx/event.h>

namespace fs {

// Connection represents an open connection to a Vnode (the server-side
// component of a file descriptor).  The Vnode's methods will be invoked
// in response to RIO protocol messages received over the channel.
//
// This class is thread-safe.
class Connection : public fbl::DoublyLinkedListable<fbl::unique_ptr<Connection>> {
public:
    // Create a connection bound to a particular vnode.
    //
    // The VFS will be notified when remote side closes the connection.
    //
    // |vfs| is the VFS which is responsible for dispatching operations to the vnode.
    // |vnode| is the vnode which will handle I/O requests.
    // |channel| is the channel on which the RIO protocol will be served.
    // |flags| are the file flags passed to |open()|, such as
    // |ZX_FS_RIGHT_READABLE|.
    Connection(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> vnode, zx::channel channel,
               uint32_t flags);

    // Closes the connection.
    //
    // The connection must not be destroyed if its wait handler is running
    // concurrently on another thread.
    //
    // In practice, this means the connection must have already been remotely
    // closed, or it must be destroyed on the wait handler's dispatch thread
    // to prevent a race.
    ~Connection();

    // Begins waiting for messages on the channel.
    //
    // Must be called at most once in the lifetime of the connection.
    zx_status_t Serve();

private:
    void HandleSignals(async_t* async, async::WaitBase* wait, zx_status_t status,
                       const zx_packet_signal_t* signal);

    zx_status_t CallHandler();

    // Sends an explicit close message to the underlying vnode.
    // Only necessary if the handler has not returned ERR_DISPATCHER_DONE
    // and has been opened.
    void CallClose();

    static zx_status_t HandleMessageThunk(zxrio_msg_t* msg, void* cookie);
    zx_status_t HandleMessage(zxrio_msg_t* msg);

    bool is_waiting() const { return wait_.object() != ZX_HANDLE_INVALID; }

    fs::Vfs* const vfs_;
    fbl::RefPtr<fs::Vnode> const vnode_;

    // Channel on which the connection is being served.
    zx::channel channel_;

    // Asynchronous wait for incoming messages.
    // The object field is |ZX_HANDLE_INVALID| when not actively waiting.
    async::WaitMethod<Connection, &Connection::HandleSignals> wait_;

    // Open flags such as |ZX_FS_RIGHT_READABLE|, and other bits.
    uint32_t flags_;

    // Handle to event which allows client to refer to open vnodes in multi-path
    // operations (see: link, rename). Defaults to ZX_HANDLE_INVALID.
    // Validated on the server-side using cookies.
    zx::event token_{};

    // Directory cookie for readdir operations.
    fs::vdircookie_t dircookie_{};

    // Current seek offset.
    size_t offset_{};
};

} // namespace fs

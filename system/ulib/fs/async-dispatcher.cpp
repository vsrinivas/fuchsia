// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <stdint.h>

#include <async/dispatcher.h>
#include <async/wait.h>
#include <fs/async-dispatcher.h>
#include <magenta/types.h>

namespace fs {

AsyncHandler::AsyncHandler(mx::channel channel, vfs_dispatcher_cb_t cb, void* cookie) :
    channel_(fbl::move(channel)), cb_(cb), cookie_(cookie),
    wait_(channel_.get(), MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
          ASYNC_FLAG_HANDLE_SHUTDOWN) {
    wait_.set_handler(fbl::BindMember(this, &AsyncHandler::Handle));
}

AsyncHandler::~AsyncHandler() = default;

async_wait_result_t AsyncHandler::Handle(async_t* async, mx_status_t status,
                                         const mx_packet_signal_t* signal) {
    if (status == MX_OK && (signal->observed & MX_CHANNEL_READABLE)) {
        status = mxrio_handler(channel_.get(), (void*) cb_, cookie_);
        if (status != MX_OK) {
            // Disconnect the handler in the case of
            // (1) Explicit Close from the client, or
            // (2) IPC-related error
            return HandlerClose(status != ERR_DISPATCHER_DONE);
        }
        return ASYNC_WAIT_AGAIN;
    } else {
        // Either the dispatcher failed to wait for signals, or
        // we received |MX_CHANNEL_PEER_CLOSED|. Either way, terminate
        // the handler.
        return HandlerClose(true);
    }
}

async_wait_result_t AsyncHandler::HandlerClose(bool need_close_cb) {
    if (need_close_cb) {
        // We're closing the handle here; we don't care about the result.
        mxrio_handler(MX_HANDLE_INVALID, (void*) cb_, cookie_);
    }
    delete this;
    return ASYNC_WAIT_FINISHED;
}

AsyncDispatcher::AsyncDispatcher(async_t* async) : async_(async) {}

AsyncDispatcher::~AsyncDispatcher() = default;

mx_status_t AsyncDispatcher::AddVFSHandler(mx::channel channel,
                                           vfs_dispatcher_cb_t cb,
                                           void* iostate) {
    AsyncHandler* handler = new AsyncHandler(fbl::move(channel), cb, iostate);
    mx_status_t status;
    if ((status = handler->Begin(async_)) != MX_OK) {
        delete handler;
    }
    return status;
}

} // namespace fs

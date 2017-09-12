// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <stdint.h>

#include <async/dispatcher.h>
#include <async/wait.h>
#include <fs/async-dispatcher.h>
#include <zircon/types.h>

namespace fs {

AsyncHandler::AsyncHandler(zx::channel channel, vfs_dispatcher_cb_t cb, void* cookie) :
    channel_(fbl::move(channel)), cb_(cb), cookie_(cookie),
    wait_(channel_.get(), ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
          ASYNC_FLAG_HANDLE_SHUTDOWN) {
    wait_.set_handler(fbl::BindMember(this, &AsyncHandler::Handle));
}

AsyncHandler::~AsyncHandler() = default;

async_wait_result_t AsyncHandler::Handle(async_t* async, zx_status_t status,
                                         const zx_packet_signal_t* signal) {
    if (status == ZX_OK && (signal->observed & ZX_CHANNEL_READABLE)) {
        status = zxrio_handler(channel_.get(), (void*) cb_, cookie_);
        if (status != ZX_OK) {
            // Disconnect the handler in the case of
            // (1) Explicit Close from the client, or
            // (2) IPC-related error
            return HandlerClose(status != ERR_DISPATCHER_DONE);
        }
        return ASYNC_WAIT_AGAIN;
    } else {
        // Either the dispatcher failed to wait for signals, or
        // we received |ZX_CHANNEL_PEER_CLOSED|. Either way, terminate
        // the handler.
        return HandlerClose(true);
    }
}

async_wait_result_t AsyncHandler::HandlerClose(bool need_close_cb) {
    if (need_close_cb) {
        // We're closing the handle here; we don't care about the result.
        zxrio_handler(ZX_HANDLE_INVALID, (void*) cb_, cookie_);
    }
    delete this;
    return ASYNC_WAIT_FINISHED;
}

AsyncDispatcher::AsyncDispatcher(async_t* async) : async_(async) {}

AsyncDispatcher::~AsyncDispatcher() = default;

zx_status_t AsyncDispatcher::AddVFSHandler(zx::channel channel,
                                           vfs_dispatcher_cb_t cb,
                                           void* iostate) {
    AsyncHandler* handler = new AsyncHandler(fbl::move(channel), cb, iostate);
    zx_status_t status;
    if ((status = handler->Begin(async_)) != ZX_OK) {
        delete handler;
    }
    return status;
}

} // namespace fs

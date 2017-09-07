// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdlib.h>
#include <stdint.h>

#include <async/dispatcher.h>
#include <async/wait.h>
#include <fs/vfs.h>
#include <magenta/types.h>

namespace fs {

class AsyncHandler {
public:
    AsyncHandler(mx::channel channel, vfs_dispatcher_cb_t cb, void* cookie);
    ~AsyncHandler();

    mx_status_t Begin(async_t* async) { return wait_.Begin(async); }

    async_wait_result_t Handle(async_t* async, mx_status_t status,
                               const mx_packet_signal_t* signal);
private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(AsyncHandler);

    // Helper function to send final callback, delete |this|,
    // and terminate the handler by returning |ASYNC_WAIT_FINISHED|
    async_wait_result_t HandlerClose(bool need_close_cb);

    mx::channel channel_;
    vfs_dispatcher_cb_t cb_;
    void* cookie_;
    async::Wait wait_;
};

class AsyncDispatcher final : public fs::Dispatcher {
public:
    AsyncDispatcher(async_t* async);
    ~AsyncDispatcher();

    mx_status_t AddVFSHandler(mx::channel channel, vfs_dispatcher_cb_t cb,
                              void* iostate) final;

private:
    async_t* async_;
};

} // namespace fs

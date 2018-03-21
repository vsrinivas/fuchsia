// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testutils/test_loop.h>
#include <lib/async/default.h>

namespace async {

TestLoop::TestLoop() : dispatcher_(&current_time_) {
    async_set_default(&dispatcher_);
}

TestLoop::~TestLoop() {
    dispatcher_.Shutdown();
    async_set_default(nullptr);
}

zx_status_t TestLoop::RunUntilIdle() {
    zx_status_t status;
    do {
        if (has_quit_) {
            return ZX_ERR_CANCELED;
        }
        dispatcher_.DispatchTasks();
        status = dispatcher_.DispatchNextWait();
    } while (status == ZX_OK);

    if (status == ZX_ERR_TIMED_OUT) {
        return ZX_OK;
    }
    return status;
}

} // namespace async

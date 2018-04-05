// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include <zircon/assert.h>

namespace async {

Loop::Loop(const async_loop_config_t* config) {
    zx_status_t status = async_loop_create(config, &loop_);
    ZX_ASSERT_MSG(status == ZX_OK, "status=%d", status);
}

Loop::~Loop() {
    async_loop_destroy(loop_);
}

void Loop::Shutdown() {
    async_loop_shutdown(loop_);
}

zx_status_t Loop::Run(zx::time deadline, bool once) {
    return async_loop_run(loop_, deadline.get(), once);
}

zx_status_t Loop::RunUntilIdle() {
    return async_loop_run_until_idle(loop_);
}

void Loop::Quit() {
    async_loop_quit(loop_);
}

zx_status_t Loop::ResetQuit() {
    return async_loop_reset_quit(loop_);
}

async_loop_state_t Loop::GetState() const {
    return async_loop_get_state(loop_);
}

zx_status_t Loop::StartThread(const char* name, thrd_t* out_thread) {
    return async_loop_start_thread(loop_, name, out_thread);
}

void Loop::JoinThreads() {
    async_loop_join_threads(loop_);
}

} // namespace async

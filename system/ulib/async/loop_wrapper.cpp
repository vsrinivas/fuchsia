// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <async/loop.h>

#include <magenta/assert.h>

namespace async {

Loop::Loop(const async_loop_config_t* config) {
    mx_status_t status = async_loop_create(config, &async_);
    MX_ASSERT_MSG(status == MX_OK, "status=%d", status);
}

Loop::~Loop() {
    async_loop_destroy(async_);
}

void Loop::Shutdown() {
    async_loop_shutdown(async_);
}

mx_status_t Loop::Run(mx_time_t deadline, bool once) {
    return async_loop_run(async_, deadline, once);
}

void Loop::Quit() {
    async_loop_quit(async_);
}

mx_status_t Loop::ResetQuit() {
    return async_loop_reset_quit(async_);
}

async_loop_state_t Loop::GetState() const {
    return async_loop_get_state(async_);
}

bool Loop::IsCurrentThreadDefault() const {
    return async_ == async_get_default();
}

mx_status_t Loop::StartThread(const char* name, thrd_t* out_thread) {
    return async_loop_start_thread(async_, name, out_thread);
}

void Loop::JoinThreads() {
    async_loop_join_threads(async_);
}

} // namespace async

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devhost-tracing.h"

#include <lib/async-loop/loop.h>
#include <trace-provider/provider.h>

#include "log.h"

using namespace devmgr;

zx_status_t devhost_start_trace_provider() {
    async_loop_t* loop;
    zx_status_t status = async_loop_create(&kAsyncLoopConfigNoAttachToThread, &loop);
    if (status != ZX_OK) {
        log(ERROR, "devhost: error creating async loop: %d\n", status);
        return status;
    }

    status = async_loop_start_thread(loop, "devhost-tracer", nullptr);
    if (status != ZX_OK) {
        async_loop_shutdown(loop);
        log(ERROR, "devhost: error starting async loop thread: %d\n", status);
        return status;
    }

    async_dispatcher_t* dispatcher = async_loop_get_dispatcher(loop);
    trace_provider_t* trace_provider = trace_provider_create(dispatcher);
    if (!trace_provider) {
        async_loop_shutdown(loop);
        log(ERROR, "devhost: error registering provider\n");
        return ZX_ERR_INTERNAL;
    }

    // N.B. The registry has begun, but these things are async. TraceManager
    // may not even be running yet (and likely isn't).
    log(INFO, "devhost: trace provider registry begun\n");
    return ZX_OK;
}

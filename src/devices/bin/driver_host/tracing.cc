// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tracing.h"

#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>

#include <trace-provider/fdio_connect.h>
#include <trace-provider/provider.h>

#include "log.h"

namespace internal {

zx_status_t start_trace_provider() {
  async_loop_t* loop;
  zx_status_t status = async_loop_create(&kAsyncLoopConfigNoAttachToCurrentThread, &loop);
  if (status != ZX_OK) {
    log(ERROR, "driver_host: error creating async loop: %d\n", status);
    return status;
  }

  status = async_loop_start_thread(loop, "driver_host-tracer", nullptr);
  if (status != ZX_OK) {
    async_loop_destroy(loop);
    log(ERROR, "driver_host: error starting async loop thread: %d\n", status);
    return status;
  }

  async_dispatcher_t* dispatcher = async_loop_get_dispatcher(loop);
  zx_handle_t to_service;
  status = trace_provider_connect_with_fdio(&to_service);
  if (status != ZX_OK) {
    log(ERROR, "driver_host: trace-provider connection failed: %d\n", status);
    return status;
  }
  trace_provider_t* trace_provider = trace_provider_create(to_service, dispatcher);
  if (!trace_provider) {
    async_loop_destroy(loop);
    log(ERROR, "driver_host: error registering provider\n");
    return ZX_ERR_INTERNAL;
  }

  // N.B. The registry has begun, but these things are async. TraceManager
  // may not even be running yet (and likely isn't).
  log(SPEW, "driver_host: trace provider registry begun\n");
  return ZX_OK;
}

}  // namespace internal

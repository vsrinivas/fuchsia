// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon/syscalls/port.h"
#include <perftest/perftest.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

namespace {

bool ObjectWaitAsyncTest(perftest::RepeatState* state) {
  state->DeclareStep("CreateEvent");
  state->DeclareStep("CreatePort");
  state->DeclareStep("ObjectWaitAsync");
  state->DeclareStep("Close");

  zx_handle_t event;
  zx_handle_t port;
  while (state->KeepRunning()) {
    zx_status_t status = zx_event_create(0u, &event);
    ZX_ASSERT(status == ZX_OK);
    state->NextStep();

    status = zx_port_create(0, &port);
    ZX_ASSERT(status == ZX_OK);
    state->NextStep();

    status = zx_object_wait_async(event, port, 0, ZX_EVENT_SIGNALED, ZX_WAIT_ASYNC_ONCE);
    ZX_ASSERT(status == ZX_OK);
    state->NextStep();

    status = zx_handle_close(event);
    ZX_ASSERT(status == ZX_OK);
    status = zx_handle_close(port);
    ZX_ASSERT(status == ZX_OK);
  }

  return true;
}

void RegisterTests() { perftest::RegisterTest("ObjectWaitAsync", ObjectWaitAsyncTest); }
PERFTEST_CTOR(RegisterTests)

}  // namespace

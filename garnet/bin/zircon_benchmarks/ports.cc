// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/port.h>
#include <zircon/syscalls/port.h>

#include <perftest/perftest.h>

#include "assert.h"

namespace {

// Measure the times taken to enqueue and then dequeue a packet from a
// Zircon port, on a single thread.  This does not involve any cross-thread
// wakeups.
bool PortQueueWaitTest(perftest::RepeatState* state) {
  state->DeclareStep("queue");
  state->DeclareStep("wait");

  zx::port port;
  ASSERT_OK(zx::port::create(0, &port));
  zx_port_packet in_packet;
  zx_port_packet out_packet = {};
  out_packet.type = ZX_PKT_TYPE_USER;

  while (state->KeepRunning()) {
    ASSERT_OK(port.queue(&out_packet));
    state->NextStep();
    ASSERT_OK(port.wait(zx::time::infinite(), &in_packet));
  }
  return true;
}

void RegisterTests() { perftest::RegisterTest("Port/QueueWait", PortQueueWaitTest); }
PERFTEST_CTOR(RegisterTests);

}  // namespace

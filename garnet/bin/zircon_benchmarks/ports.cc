// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/port.h>
#include <perftest/perftest.h>
#include <zircon/syscalls/port.h>

namespace {

// Measure the times taken to enqueue and then dequeue a packet from a
// Zircon port, on a single thread.  This does not involve any cross-thread
// wakeups.
bool PortQueueWaitTest(perftest::RepeatState* state) {
  state->DeclareStep("queue");
  state->DeclareStep("wait");

  zx::port port;
  ZX_ASSERT(zx::port::create(0, &port) == ZX_OK);
  zx_port_packet in_packet;
  zx_port_packet out_packet = {};
  out_packet.type = ZX_PKT_TYPE_USER;

  while (state->KeepRunning()) {
    ZX_ASSERT(port.queue(&out_packet) == ZX_OK);
    state->NextStep();
    ZX_ASSERT(port.wait(zx::time::infinite(), &in_packet) == ZX_OK);
  }
  return true;
}

void RegisterTests() {
  perftest::RegisterTest("Port/QueueWait", PortQueueWaitTest);
}
PERFTEST_CTOR(RegisterTests);

}  // namespace

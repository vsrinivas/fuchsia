// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/spawn.h>
#include <lib/zx/handle.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <perftest/perftest.h>

#include "assert.h"
#include "lib/zx/time.h"

namespace {

constexpr const char* kPath = "/bin/context_switch_overhead_helper";

void ChannelWait(const zx::channel& channel) {
  // If we only pass ZX_CHANNEL_READABLE here, we will end up waiting
  // forever if the process holding the other channel endpoint dies, so we
  // should pass ZX_CHANNEL_PEER_CLOSED too.
  ASSERT_OK(channel.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(),
                             nullptr));
}

// Measure the time taken for NUM_CPUS pairs of threads, running in parallel, to each start up and
// then execute |number_of_switches| round trips (via eventpair wakeup), when running in a separate
// process. Each pair of threads is pinned to a different CPU on the system.
//
// The flow is as follows:
// Host = this process
// Helper = The helper process runnning the tests.
//
// 1. Helper sends a small message over the channel, to signal it is ready to run a test.
// 2. Host sends a message containing the number of context switches to do.
// 3. Helper sends a small message to signal the setup is ready.
// 4. Helper runs the test, sending another small message to signal the test is done.
// 5. Helper waits for another message and returns to step 2.
//
// The test is intended to reach peak context switches on all cores, and it is meant to be sensitive
// to changes that modify shared data on cache-lines between cores.
bool ContextSwitchTest(perftest::RepeatState* state, size_t number_of_switches) {
  zx::channel chan1, chan2;
  ASSERT_OK(zx::channel::create(0, &chan1, &chan2));

  const char* const argv[] = {kPath, nullptr};

  zx::job job;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0, &job));
  zx::handle process;
  zx_handle_t server_handle = chan2.release();
  fdio_spawn_action_t actions[1] = {
      {.action = FDIO_SPAWN_ACTION_ADD_HANDLE, .h = {.id = PA_USER0, .handle = server_handle}}};
  ASSERT_OK(fdio_spawn_etc(job.get(), FDIO_SPAWN_CLONE_ALL, kPath, argv, nullptr /* environ */,
                           1 /* action count */, actions, process.reset_and_get_address(),
                           nullptr /* err message out */));

  char out[1024];
  uint32_t len;

  ChannelWait(chan1);
  ASSERT_OK(chan1.read(0, &out, nullptr, sizeof(out), 0, &len, nullptr));

  state->DeclareStep("setup");
  state->DeclareStep("execute");

  while (state->KeepRunning()) {
    ASSERT_OK(chan1.write(0, &number_of_switches, sizeof(number_of_switches), nullptr, 0));
    ChannelWait(chan1);
    ASSERT_OK(chan1.read(0, &out, nullptr, sizeof(out), 0, &len, nullptr));
    state->NextStep();
    ChannelWait(chan1);
    ASSERT_OK(chan1.read(0, &out, nullptr, sizeof(out), 0, &len, nullptr));
  }

  ASSERT_OK(job.kill());
  ASSERT_OK(process.wait_one(ZX_TASK_TERMINATED, zx::time::infinite(), nullptr));

  return true;
}

void RegisterTests() { perftest::RegisterTest("ContextSwitch/1000", ContextSwitchTest, 1000); }
PERFTEST_CTOR(RegisterTests)

}  // namespace

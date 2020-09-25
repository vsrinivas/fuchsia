// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/event.h>

#include <thread>
#include <vector>

#include <fbl/string_printf.h>
#include <perftest/perftest.h>

#include "assert.h"

namespace {

// Helper function that will run in its own thread. Continuously calls get_info
// until told to stop via a shared variable.
void DoHandleValid(std::atomic<bool>* stop, zx::event* event) {
  while (!stop->load(std::memory_order_relaxed)) {
    ASSERT_OK(event->get_info(ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
  }
}

// Measure how long a simple 'get_info' call takes whilst other cores are doing
// the same. This is measuring the scalability of
// get_info(ZX_INFO_HANDLE_INVALID), particularly the synchronization in the
// kernel on this syscall path.
//
// Should not be invoked with more threads than there are cpus otherwise there
// is a chance with the current scheduler that the main test thread does not get
// to run (or gets to run very sporadically) and makes the test hang (fxbug.dev/31453).
bool HandleValid(perftest::RepeatState* state, uint32_t num_threads) {
  // Object so we have a handle to test validity of.
  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));

  // Shared variable for signaling worker threads to stop.
  std::atomic<bool> stop(false);

  // Create our worker threads.
  std::vector<std::thread> threads(num_threads - 1);

  for (auto& t : threads) {
    t = std::thread(&DoHandleValid, &stop, &event);
  }

  while (state->KeepRunning()) {
    ASSERT_OK(event.get_info(ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
  }

  // Inform our worker threads to stop so we can nicely join them.
  stop.store(true, std::memory_order_seq_cst);

  for (auto& t : threads) {
    t.join();
  }

  return true;
}

void RegisterTests() {
  for (unsigned i = 1; i <= zx_system_get_num_cpus(); i++) {
    auto name = fbl::StringPrintf("HandleValid/%uThreads", i);
    perftest::RegisterTest(name.c_str(), HandleValid, i);
  }
}
PERFTEST_CTOR(RegisterTests);

}  // namespace

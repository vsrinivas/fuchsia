// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/run-once.h"

#include <lib/sync/completion.h>
#include <stddef.h>

#include <atomic>
#include <thread>

#include <gtest/gtest.h>

namespace fuzzing {

TEST(RunOnceTest, Run) {
  SyncWait called;
  SyncWait proceed;
  std::atomic<size_t> calls = 0;
  std::atomic<size_t> callers = 0;

  RunOnce once([&]() {
    called.Signal();
    proceed.WaitFor("permission to proceed");
    calls++;
  });

  std::thread t1([&]() {
    once.Run();
    callers++;
  });
  std::thread t2([&]() {
    once.Run();
    callers++;
  });
  std::thread t3([&]() {
    once.Run();
    callers++;
  });

  EXPECT_EQ(callers, 0U);
  EXPECT_EQ(calls, 0U);

  proceed.Signal();
  t1.join();
  t2.join();
  t3.join();

  EXPECT_EQ(callers, 3U);
  EXPECT_EQ(calls, 1U);
}

}  // namespace fuzzing

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/coverage/provider.h"

#include <lib/zx/eventpair.h>
#include <stdint.h>

#include <atomic>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/sync-wait.h"
#include "src/sys/fuzzing/framework/coverage/event-queue.h"

namespace fuzzing {

using ::fuchsia::fuzzer::CoverageProviderSyncPtr;

TEST(CoverageProviderTest, WatchCoverageEvent) {
  auto events = std::make_shared<CoverageEventQueue>();
  CoverageProviderImpl provider(events);

  uint64_t target_id = 16ULL;
  SyncWait sync;

  // Callback is invoked asynchronously.
  provider.WatchCoverageEvent([&](CoverageEvent event) {
    EXPECT_EQ(event.target_id, target_id);
    EXPECT_TRUE(event.payload.is_process_started());
    sync.Signal();
  });

  zx::eventpair ep_a, ep_b;
  EXPECT_EQ(zx::eventpair::create(0, &ep_a, &ep_b), ZX_OK);
  InstrumentedProcess instrumented;
  instrumented.set_eventpair(std::move(ep_a));
  events->AddProcess(target_id, std::move(instrumented));
  sync.WaitFor("coverage event");
}

TEST(CoverageProviderTest, AwaitClose) {
  auto events = std::make_shared<CoverageEventQueue>();
  CoverageProviderImpl provider(events);

  std::atomic<bool> connected = false;
  std::atomic<bool> closed = false;
  std::atomic<bool> invoked = false;

  std::thread t1([&]() {
    provider.AwaitConnect();
    connected = true;
  });

  EXPECT_FALSE(connected);
  EXPECT_FALSE(closed);
  EXPECT_FALSE(invoked);

  // Connect
  CoverageProviderSyncPtr ptr;
  auto handler = provider.GetHandler();
  handler(ptr.NewRequest());
  t1.join();

  std::thread t2([&]() {
    provider.AwaitClose();
    closed = true;
  });

  EXPECT_TRUE(connected);
  EXPECT_FALSE(closed);
  EXPECT_FALSE(invoked);

  provider.WatchCoverageEvent([&](CoverageEvent ignored) { invoked = true; });
  ptr.Unbind();
  t2.join();

  // Callback should never have been invoked.
  EXPECT_TRUE(connected);
  EXPECT_TRUE(closed);
  EXPECT_FALSE(invoked);
}

}  // namespace fuzzing

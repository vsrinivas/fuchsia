// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/coverage/event-queue.h"

#include <lib/zx/eventpair.h>
#include <stdint.h>
#include <zircon/types.h>

#include <thread>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/shared-memory.h"
#include "src/sys/fuzzing/framework/testing/module.h"
#include "src/sys/fuzzing/framework/testing/process.h"

namespace fuzzing {

TEST(CoverageEventQueueTest, SetOptions) {
  CoverageEventQueue events;

  Options options1;
  const uint64_t kMallocLimit = 64ULL << 20;
  options1.set_malloc_limit(kMallocLimit);
  events.SetOptions(std::move(options1));

  auto options2 = events.GetOptions();
  EXPECT_EQ(options2.malloc_limit(), kMallocLimit);
}

TEST(CoverageEventQueueTest, AddProcess) {
  CoverageEventQueue events;

  FakeProcess process1;
  zx::eventpair ep1a, ep1b;
  EXPECT_EQ(zx::eventpair::create(0, &ep1a, &ep1b), ZX_OK);
  events.AddProcess(/* target_id*/ 1U, process1.IgnoreTarget(std::move(ep1a)));

  FakeProcess process2;
  zx::eventpair ep2a, ep2b;
  EXPECT_EQ(zx::eventpair::create(0, &ep2a, &ep2b), ZX_OK);
  events.AddProcess(/* target_id*/ 2U, process2.IgnoreTarget(std::move(ep2a)));

  // Events should arrive in order...
  auto event1 = events.GetEvent();
  auto event2 = events.GetEvent();
  EXPECT_EQ(event1->target_id, 1U);
  EXPECT_EQ(event2->target_id, 2U);

  // ...and should have transferred handles.
  auto instrumented1 = std::move(event1->payload.process_started());
  auto instrumented2 = std::move(event2->payload.process_started());
  instrumented1.eventpair().signal_peer(0, ZX_USER_SIGNAL_1);
  instrumented2.eventpair().signal_peer(0, ZX_USER_SIGNAL_2);
  EXPECT_EQ(ep1b.wait_one(ZX_USER_SIGNAL_1, zx::time::infinite_past(), nullptr), ZX_OK);
  EXPECT_EQ(ep2b.wait_one(ZX_USER_SIGNAL_2, zx::time::infinite_past(), nullptr), ZX_OK);
}

TEST(CoverageEventQueueTest, AddLlvmModule) {
  CoverageEventQueue events;

  FakeFrameworkModule module1(/* seed */ 1U);
  events.AddLlvmModule(/* target_id*/ 1U, module1.GetLlvmModule());

  FakeFrameworkModule module2(/* seed */ 2U);
  events.AddLlvmModule(/* target_id*/ 2U, module2.GetLlvmModule());

  // Events should arrive in order...
  auto event1 = events.GetEvent();
  auto event2 = events.GetEvent();
  EXPECT_EQ(event1->target_id, 1U);
  EXPECT_EQ(event2->target_id, 2U);

  // ...and should have transferred handles.
  SharedMemory shmem1;
  auto llvm_module1 = std::move(event1->payload.llvm_module_added());
  auto* inline_8bit_counters = llvm_module1.mutable_inline_8bit_counters();
  shmem1.LinkMirrored(std::move(*inline_8bit_counters));
  module1[0] = 1U;
  module1.Update();
  EXPECT_EQ(shmem1.data()[0], 1U);

  SharedMemory shmem2;
  auto llvm_module2 = std::move(event2->payload.llvm_module_added());
  inline_8bit_counters = llvm_module2.mutable_inline_8bit_counters();
  shmem2.LinkMirrored(std::move(*inline_8bit_counters));
  module2[0] = 2U;
  module2.Update();
  EXPECT_EQ(shmem2.data()[0], 2U);
}

TEST(CoverageEventQueueTest, GetEvent) {
  CoverageEventQueue events;
  std::atomic<bool> is_null = true;
  std::atomic<bool> done = false;

  // |GetEvent| should block until an event is added.
  std::thread t1([&]() {
    is_null = !events.GetEvent();
    done = true;
  });

  EXPECT_FALSE(done.load());

  FakeProcess process;
  events.AddProcess(/* target_id*/ 1U, process.IgnoreAll());
  t1.join();

  EXPECT_FALSE(is_null.load());
  EXPECT_TRUE(done.load());

  // Calling |Stop| returns null.
  done = false;
  std::thread t2([&]() {
    is_null = !events.GetEvent();
    done = true;
  });

  EXPECT_FALSE(done.load());

  events.Stop();
  t2.join();

  EXPECT_TRUE(is_null.load());
  EXPECT_TRUE(done.load());
}

}  // namespace fuzzing

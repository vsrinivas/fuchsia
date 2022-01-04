// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/coverage/forwarder.h"

#include <lib/zx/eventpair.h>
#include <stdint.h>

#include <atomic>
#include <thread>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/framework/coverage/event-queue.h"
#include "src/sys/fuzzing/framework/testing/module.h"
#include "src/sys/fuzzing/framework/testing/process.h"

namespace fuzzing {

using ::fuchsia::fuzzer::CoverageProviderSyncPtr;
using ::fuchsia::fuzzer::InstrumentationSyncPtr;

// Unit tests.

TEST(CoverageForwarderTest, TwoInstrumentedProcesses) {
  CoverageForwarder forwarder;
  std::thread t([&]() { forwarder.Run(); });

  CoverageProviderSyncPtr provider;
  auto provider_handler = forwarder.GetCoverageProviderHandler();
  provider_handler(provider.NewRequest());

  Options options;
  const uint64_t kMallocLimit = 64ULL << 20;
  options.set_malloc_limit(kMallocLimit);
  provider->SetOptions(std::move(options));

  auto instrumentation_handler = forwarder.GetInstrumentationHandler();

  InstrumentationSyncPtr instrumentation1;
  instrumentation_handler(instrumentation1.NewRequest());

  InstrumentationSyncPtr instrumentation2;
  instrumentation_handler(instrumentation2.NewRequest());

  // Initialize

  FakeProcess process1;
  zx::eventpair ep1a, ep1b;
  EXPECT_EQ(zx::eventpair::create(0, &ep1a, &ep1b), ZX_OK);
  auto instrumented1 = process1.IgnoreTarget(std::move(ep1a));
  Options options1;
  ASSERT_EQ(instrumentation1->Initialize(std::move(instrumented1), &options1), ZX_OK);
  EXPECT_EQ(options1.malloc_limit(), kMallocLimit);

  FakeProcess process2;
  zx::eventpair ep2a, ep2b;
  EXPECT_EQ(zx::eventpair::create(0, &ep2a, &ep2b), ZX_OK);
  auto instrumented2 = process2.IgnoreTarget(std::move(ep2a));
  Options options2;
  ASSERT_EQ(instrumentation2->Initialize(std::move(instrumented2), &options2), ZX_OK);
  EXPECT_EQ(options2.malloc_limit(), kMallocLimit);

  CoverageEvent event;
  provider->WatchCoverageEvent(&event);
  EXPECT_TRUE(event.payload.is_process_started());
  auto target_id1 = event.target_id;

  provider->WatchCoverageEvent(&event);
  EXPECT_TRUE(event.payload.is_process_started());
  auto target_id2 = event.target_id;

  // AddLlvmModule

  FakeFrameworkModule module1(/* seed */ 1U);
  EXPECT_EQ(instrumentation1->AddLlvmModule(module1.GetLlvmModule()), ZX_OK);

  FakeFrameworkModule module2(/* seed */ 2U);
  EXPECT_EQ(instrumentation2->AddLlvmModule(module2.GetLlvmModule()), ZX_OK);

  provider->WatchCoverageEvent(&event);
  EXPECT_TRUE(event.payload.is_llvm_module_added());
  EXPECT_EQ(target_id1, event.target_id);

  provider->WatchCoverageEvent(&event);
  EXPECT_TRUE(event.payload.is_llvm_module_added());
  EXPECT_EQ(target_id2, event.target_id);

  provider.Unbind();
  t.join();
}

}  // namespace fuzzing

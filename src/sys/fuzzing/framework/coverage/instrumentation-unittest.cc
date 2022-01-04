// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/coverage/instrumentation.h"

#include <lib/zx/eventpair.h>
#include <stdint.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/framework/coverage/event-queue.h"
#include "src/sys/fuzzing/framework/testing/module.h"
#include "src/sys/fuzzing/framework/testing/process.h"

namespace fuzzing {

TEST(InstrumentationTest, AddProcess) {
  auto events = std::make_shared<CoverageEventQueue>();

  Options options;
  const uint64_t kMallocLimit = 64ULL << 20;
  options.set_malloc_limit(kMallocLimit);
  events->SetOptions(std::move(options));

  auto target_id = 0ULL;
  InstrumentationImpl impl(target_id, events);

  FakeProcess process;
  Options received;
  impl.Initialize(process.IgnoreAll(), [&](Options response) { received = std::move(response); });
  EXPECT_EQ(received.malloc_limit(), kMallocLimit);

  auto event = events->GetEvent();
  EXPECT_EQ(event->target_id, target_id);
  EXPECT_TRUE(event->payload.is_process_started());
}

TEST(InstrumentationTest, AddLlvmModule) {
  auto events = std::make_shared<CoverageEventQueue>();

  auto target_id = 1ULL;
  InstrumentationImpl impl(target_id, events);

  FakeFrameworkModule module;
  bool added = false;
  impl.AddLlvmModule(module.GetLlvmModule(), [&]() { added = true; });
  EXPECT_TRUE(added);

  auto event = events->GetEvent();
  EXPECT_EQ(event->target_id, target_id);
  EXPECT_TRUE(event->payload.is_llvm_module_added());
}

}  // namespace fuzzing

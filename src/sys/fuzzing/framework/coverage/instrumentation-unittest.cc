// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/coverage/instrumentation.h"

#include <stdint.h>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/testing/async-test.h"
#include "src/sys/fuzzing/framework/testing/module.h"

namespace fuzzing {

// Test fixtures.

class InstrumentationTest : public AsyncTest {};

// Unit tests.

TEST_F(InstrumentationTest, Initialize) {
  auto target_id = 0ULL;
  auto options = MakeOptions();
  auto events = AsyncDeque<CoverageEvent>::MakePtr();
  InstrumentationImpl instrumentation(target_id, options, events);

  const uint64_t kMallocLimit = 64ULL << 20;
  options->set_malloc_limit(kMallocLimit);

  Bridge<Options> bridge;
  instrumentation.Initialize(InstrumentedProcess(), bridge.completer.bind());

  Options received;
  CoverageEvent event;
  FUZZING_EXPECT_OK(bridge.consumer.promise_or(fpromise::error()), &received);
  FUZZING_EXPECT_OK(events->Receive(), &event);
  RunUntilIdle();

  EXPECT_EQ(received.malloc_limit(), kMallocLimit);
  EXPECT_EQ(event.target_id, target_id);
  EXPECT_TRUE(event.payload.is_process_started());
}

TEST_F(InstrumentationTest, AddLlvmModule) {
  auto target_id = 1ULL;
  auto options = MakeOptions();
  auto events = AsyncDeque<CoverageEvent>::MakePtr();
  InstrumentationImpl instrumentation(target_id, options, events);

  Bridge<> bridge;
  instrumentation.AddLlvmModule(LlvmModule(), bridge.completer.bind());

  CoverageEvent event;
  FUZZING_EXPECT_OK(bridge.consumer.promise_or(fpromise::error()));
  FUZZING_EXPECT_OK(events->Receive(), &event);
  RunUntilIdle();

  EXPECT_EQ(event.target_id, target_id);
  EXPECT_TRUE(event.payload.is_llvm_module_added());
}

}  // namespace fuzzing

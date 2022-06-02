// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/coverage/provider.h"

#include <lib/zx/eventpair.h>
#include <stdint.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/testing/async-test.h"

namespace fuzzing {

// Test fixtures.

using fuchsia::fuzzer::InstrumentedProcess;
using fuchsia::fuzzer::LlvmModule;
using fuchsia::fuzzer::Payload;

class CoverageProviderTest : public AsyncTest {};

// Unit tests.

TEST_F(CoverageProviderTest, SetOptions) {
  auto options1 = MakeOptions();
  CoverageProviderImpl provider(executor(), options1, AsyncDeque<CoverageEvent>::MakePtr());

  const int kMallocExitcode = 3333;
  Options options2;
  options2.set_malloc_exitcode(kMallocExitcode);
  provider.SetOptions(std::move(options2));

  EXPECT_EQ(options1->malloc_exitcode(), kMallocExitcode);
}

TEST_F(CoverageProviderTest, WatchCoverageEvent) {
  auto events = AsyncDeque<CoverageEvent>::MakePtr();
  CoverageProviderImpl provider(executor(), MakeOptions(), events);

  Bridge<CoverageEvent> bridge1, bridge2, bridge3;
  provider.WatchCoverageEvent(bridge1.completer.bind());
  provider.WatchCoverageEvent(bridge2.completer.bind());
  provider.WatchCoverageEvent(bridge3.completer.bind());

  FUZZING_EXPECT_OK(
      bridge1.consumer.promise_or(fpromise::error()).and_then([&](const CoverageEvent& event) {
        EXPECT_EQ(event.target_id, 101ULL);
        EXPECT_TRUE(event.payload.is_process_started());
      }));

  FUZZING_EXPECT_OK(
      bridge2.consumer.promise_or(fpromise::error()).and_then([&](const CoverageEvent& event) {
        EXPECT_EQ(event.target_id, 202ULL);
        EXPECT_TRUE(event.payload.is_llvm_module_added());
      }));

  FUZZING_EXPECT_ERROR(bridge3.consumer.promise_or(fpromise::error()));

  CoverageEvent event;
  event.target_id = 101ULL;
  event.payload = Payload::WithProcessStarted(InstrumentedProcess());
  EXPECT_EQ(events->Send(std::move(event)), ZX_OK);

  event.target_id = 202ULL;
  event.payload = Payload::WithLlvmModuleAdded(LlvmModule());
  EXPECT_EQ(events->Send(std::move(event)), ZX_OK);

  events->Close();

  RunUntilIdle();
}

}  // namespace fuzzing

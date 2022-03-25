// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/coverage/forwarder.h"

#include <stdint.h>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/testing/async-test.h"

namespace fuzzing {

// Test fixtures.

using ::fuchsia::fuzzer::CoverageProviderPtr;
using ::fuchsia::fuzzer::InstrumentationPtr;

class CoverageForwarderTest : public AsyncTest {};

// Unit tests.

TEST_F(CoverageForwarderTest, TwoInstrumentedProcesses) {
  CoverageForwarder forwarder(executor());

  CoverageProviderPtr provider;
  auto provider_handler = forwarder.GetCoverageProviderHandler();
  provider_handler(provider.NewRequest(executor()->dispatcher()));

  Options options;
  const uint64_t kMallocLimit = 64ULL << 20;
  options.set_malloc_limit(kMallocLimit);
  provider->SetOptions(std::move(options));

  auto instrumentation_handler = forwarder.GetInstrumentationHandler();
  InstrumentationPtr instrumentation1, instrumentation2;
  instrumentation_handler(instrumentation1.NewRequest(executor()->dispatcher()));
  instrumentation_handler(instrumentation2.NewRequest(executor()->dispatcher()));

  // Watch for coverage events.
  Bridge<CoverageEvent> bridge1, bridge2;
  provider->WatchCoverageEvent(bridge1.completer.bind());
  provider->WatchCoverageEvent(bridge2.completer.bind());

  // Initialize both processes.
  Bridge<Options> bridge3, bridge4;
  instrumentation1->Initialize(InstrumentedProcess(), bridge3.completer.bind());
  instrumentation2->Initialize(InstrumentedProcess(), bridge4.completer.bind());

  uint64_t target_id1, target_id2;
  FUZZING_EXPECT_OK(
      bridge1.consumer.promise_or(fpromise::error()).and_then([&](const CoverageEvent& event) {
        EXPECT_TRUE(event.payload.is_process_started());
        return fpromise::ok(event.target_id);
      }),
      &target_id1);
  FUZZING_EXPECT_OK(
      bridge2.consumer.promise_or(fpromise::error()).and_then([&](const CoverageEvent& event) {
        EXPECT_TRUE(event.payload.is_process_started());
        return fpromise::ok(event.target_id);
      }),
      &target_id2);

  FUZZING_EXPECT_OK(
      bridge3.consumer.promise_or(fpromise::error()).and_then([&](const Options& options) {
        return fpromise::ok(options.malloc_limit());
      }),
      kMallocLimit);
  FUZZING_EXPECT_OK(
      bridge4.consumer.promise_or(fpromise::error()).and_then([&](const Options& options) {
        return fpromise::ok(options.malloc_limit());
      }),
      kMallocLimit);
  RunUntilIdle();

  // Add modules in a different order.
  Bridge<CoverageEvent> bridge5, bridge6;
  provider->WatchCoverageEvent(bridge5.completer.bind());
  provider->WatchCoverageEvent(bridge6.completer.bind());

  Bridge<> bridge7, bridge8;
  instrumentation2->AddLlvmModule(LlvmModule(), bridge7.completer.bind());
  instrumentation1->AddLlvmModule(LlvmModule(), bridge8.completer.bind());

  FUZZING_EXPECT_OK(
      bridge5.consumer.promise_or(fpromise::error()).and_then([&](const CoverageEvent& event) {
        EXPECT_TRUE(event.payload.is_llvm_module_added());
        return fpromise::ok(event.target_id);
      }),
      target_id2);
  FUZZING_EXPECT_OK(
      bridge6.consumer.promise_or(fpromise::error()).and_then([&](const CoverageEvent& event) {
        EXPECT_TRUE(event.payload.is_llvm_module_added());
        return fpromise::ok(event.target_id);
      }),
      target_id1);

  FUZZING_EXPECT_OK(bridge7.consumer.promise_or(fpromise::error()));
  FUZZING_EXPECT_OK(bridge8.consumer.promise_or(fpromise::error()));
  RunUntilIdle();
}

}  // namespace fuzzing

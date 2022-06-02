// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/coverage-client.h"

#include <fuchsia/fuzzer/cpp/fidl.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/async-deque.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/testing/async-test.h"
#include "src/sys/fuzzing/framework/coverage/provider.h"
#include "src/sys/fuzzing/framework/testing/process.h"

namespace fuzzing {

// Test fixtures.

using fuchsia::fuzzer::Payload;

class CoverageProviderClientTest : public AsyncTest {};

// Unit tests.

TEST_F(CoverageProviderClientTest, SetOptions) {
  auto options1 = MakeOptions();
  options1->set_seed(111U);

  auto options2 = MakeOptions();
  CoverageProviderImpl provider(executor(), options2, AsyncDeque<CoverageEvent>::MakePtr());

  CoverageProviderClient client(executor());
  client.set_handler(provider.GetHandler());

  client.SetOptions(options1);
  RunUntilIdle();
  ASSERT_TRUE(options2->has_seed());
  EXPECT_EQ(options2->seed(), 111U);
}

TEST_F(CoverageProviderClientTest, WatchCoverageEvent) {
  auto events = AsyncDeque<CoverageEvent>::MakePtr();
  CoverageProviderImpl provider(executor(), MakeOptions(), events);

  CoverageProviderClient client(executor());
  client.set_handler(provider.GetHandler());

  FUZZING_EXPECT_OK(client.WatchCoverageEvent().and_then(
                        [&](const CoverageEvent& event) { return fpromise::ok(event.target_id); }),
                    2222U);
  CoverageEvent event;
  event.target_id = 2222U;
  event.payload = Payload::WithProcessStarted(InstrumentedProcess());
  EXPECT_EQ(events->Send(std::move(event)), ZX_OK);
  RunUntilIdle();

  FUZZING_EXPECT_ERROR(client.WatchCoverageEvent());
  events->Close();
  RunUntilIdle();
}

}  // namespace fuzzing

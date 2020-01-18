// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/logging.h>

#include "garnet/bin/trace_manager/tests/trace_manager_test.h"

namespace tracing {
namespace test {

TEST_F(TraceManagerTest, TerminateOnClose) {
  ConnectToControllerService();

  ASSERT_TRUE(InitializeSession());

  EXPECT_EQ(GetSessionState(), SessionState::kInitialized);

  DisconnectFromControllerService();

  RunLoopUntilIdle();
  EXPECT_EQ(GetSessionState(), SessionState::kNonexistent);
}

TEST_F(TraceManagerTest, TerminateWhenNotInitialized) {
  ConnectToControllerService();

  controller::TerminateOptions options;
  options.set_write_results(false);
  bool terminated = false;
  controller()->TerminateTracing(
      std::move(options), [&terminated](controller::TerminateResult result) { terminated = true; });

  RunLoopUntilIdle();

  // There is no error result in this case.
  // Mostly we just want to verify we don't crash/hang.
  ASSERT_TRUE(terminated);
}

}  // namespace test
}  // namespace tracing

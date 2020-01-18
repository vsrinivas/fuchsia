// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/logging.h>

#include "garnet/bin/trace_manager/tests/trace_manager_test.h"

namespace tracing {
namespace test {

TEST_F(TraceManagerTest, DuplicateInitialization) {
  ConnectToControllerService();

  ASSERT_TRUE(InitializeSession());

  zx::socket our_socket, their_socket;
  zx_status_t status = zx::socket::create(0u, &our_socket, &their_socket);
  ASSERT_EQ(status, ZX_OK);

  controller::TraceConfig config{GetDefaultTraceConfig()};
  controller()->InitializeTracing(std::move(config), std::move(their_socket));
  // There's no state transition here that would trigger a call to
  // |LoopQuit()|. Nor is there a result.
  // Mostly we just want to verify things don't hang.
  RunLoopUntilIdle();
}

TEST_F(TraceManagerTest, InitializeWithoutProviders) {
  ConnectToControllerService();

  ASSERT_TRUE(InitializeSession());

  EXPECT_EQ(GetSessionState(), SessionState::kInitialized);
}

}  // namespace test
}  // namespace tracing

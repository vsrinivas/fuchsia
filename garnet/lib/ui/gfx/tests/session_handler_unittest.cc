// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/session_handler_test.h"
#include "gtest/gtest.h"

namespace scenic_impl {
namespace gfx {
namespace test {

constexpr SessionId kSessionId = 1;

TEST(SessionHandlerTest,
     WhenSessionHandlerDestroyed_ShouldRemoveSessionHandlerPtrFromSessionManager) {
  SessionManager manager;
  SessionContext session_context;
  scenic_impl::Session session(kSessionId, nullptr);

  CommandDispatcherContext dispatch_context(nullptr, &session, kSessionId);

  auto handler = manager.CreateCommandDispatcher(std::move(dispatch_context), session_context);
  ASSERT_NE(handler, nullptr);

  EXPECT_EQ(manager.FindSessionHandler(kSessionId), handler.get());

  // Reset session_handler
  handler.reset();

  EXPECT_EQ(handler, nullptr);
  EXPECT_EQ(manager.FindSessionHandler(kSessionId), nullptr);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

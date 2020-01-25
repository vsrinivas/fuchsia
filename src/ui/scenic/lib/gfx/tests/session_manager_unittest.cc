// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/session_manager.h"

#include "gtest/gtest.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/scenic/event_reporter.h"
#include "src/ui/scenic/lib/scenic/util/error_reporter.h"

namespace scenic_impl {
namespace gfx {
namespace test {

constexpr SessionId kSessionId = 1;

TEST(SessionManagerTest, WhenSessionDestroyed_ShouldRemoveSessionPtrFromSessionManager) {
  SessionManager manager;

  auto dispatcher = manager.CreateCommandDispatcher(
      kSessionId, /*session_context=*/{}, EventReporter::Default(), ErrorReporter::Default());
  ASSERT_NE(dispatcher, nullptr);

  EXPECT_EQ(manager.FindSession(kSessionId), dispatcher.get());

  // Kill the session.
  dispatcher.reset();
  EXPECT_EQ(dispatcher, nullptr);
  EXPECT_EQ(manager.FindSession(kSessionId), nullptr);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/session_handler_test.h"

#include "gtest/gtest.h"

namespace scenic_impl {
namespace gfx {
namespace test {

TEST_F(
    SessionHandlerTest,
    WhenSessionHandlerDestroyed_ShouldRemoveSessionHandlerPtrFromSessionManager) {
  InitializeSessionHandler();
  auto id = session_->id();

  EXPECT_NE(session_handler_.get(), nullptr);
  EXPECT_EQ(session_manager_->FindSessionHandler(id), session_handler_.get());

  ResetSessionHandler();

  EXPECT_EQ(session_handler_.get(), nullptr);
  EXPECT_EQ(session_manager_->FindSessionHandler(id), nullptr);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

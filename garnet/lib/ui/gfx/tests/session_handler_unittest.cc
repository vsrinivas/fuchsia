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
  auto id = session_handler_->session()->id();

  auto session_manager = engine_->session_context().session_manager;
  ASSERT_NE(session_manager, nullptr);

  EXPECT_NE(session_handler_.get(), nullptr);
  EXPECT_EQ(session_manager->FindSessionHandler(id), session_handler_.get());

  session_handler_.reset();

  EXPECT_EQ(session_handler_.get(), nullptr);
  EXPECT_EQ(session_manager->FindSessionHandler(id), nullptr);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

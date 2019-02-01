// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_TESTS_SESSION_HANDLER_TEST_H_
#define GARNET_LIB_UI_GFX_TESTS_SESSION_HANDLER_TEST_H_

#include "garnet/lib/ui/gfx/tests/session_test.h"

#include <lib/fit/function.h>

#include "garnet/lib/ui/gfx/displays/display_manager.h"
#include "garnet/lib/ui/gfx/engine/engine.h"
#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/tests/mocks.h"
#include "garnet/lib/ui/scenic/event_reporter.h"
#include "garnet/lib/ui/scenic/tests/scenic_test.h"
#include "lib/gtest/test_loop_fixture.h"

namespace scenic_impl {
namespace gfx {
namespace test {

// For testing SessionHandler without having to manually provide all the state
// necessary for SessionHandler to run
class SessionHandlerTest : public SessionTest {
 public:
  void ResetSessionHandler() { session_handler_.reset(); }
  void InitializeScenic();
  void InitializeSessionHandler();

  void SetUp() override;
  void TearDown() override;

 protected:
  std::unique_ptr<component::StartupContext> app_context_;
  std::unique_ptr<Scenic> scenic_;
  std::unique_ptr<SessionHandlerForTest> session_handler_;
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_TESTS_SESSION_HANDLER_TEST_H_

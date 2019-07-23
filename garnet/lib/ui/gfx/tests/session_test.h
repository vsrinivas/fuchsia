// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_TESTS_SESSION_TEST_H_
#define GARNET_LIB_UI_GFX_TESTS_SESSION_TEST_H_

#include <lib/fit/function.h>

#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/tests/error_reporting_test.h"
#include "garnet/lib/ui/gfx/tests/mocks.h"
#include "garnet/lib/ui/scenic/event_reporter.h"
#include "garnet/lib/ui/scenic/tests/scenic_test.h"
#include "lib/gtest/test_loop_fixture.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class SessionTest : public ErrorReportingTest {
 protected:
  // Subclasses that override SetUp() and TearDown() should be sure to call
  // their parent class's implementations.

  // | ::testing::Test |
  void SetUp() override;
  // | ::testing::Test |
  void TearDown() override;

  std::unique_ptr<Session> CreateSession();

  // Apply the specified Command.  Return true if it was applied successfully,
  // and false if an error occurred.
  bool Apply(::fuchsia::ui::gfx::Command command);

  template <class ResourceT>
  fxl::RefPtr<ResourceT> FindResource(ResourceId id) {
    return session_->resources()->FindResource<ResourceT>(id);
  }

  Session* session() { return session_.get(); }

  DisplayManager* display_manager() { return display_manager_.get(); }

 protected:
  // Creates a SessionContext with only a SessionManager and a
  // FrameScheduler. Subclasses should override to customize Session creation.
  virtual SessionContext CreateSessionContext();
  // Creates an empty CommandContext for Apply(). Subclasses should override to
  // customize command application.
  virtual CommandContext CreateCommandContext();

 private:
  SessionContext session_context_;

  std::unique_ptr<DisplayManager> display_manager_;
  std::shared_ptr<FrameScheduler> frame_scheduler_;
  std::unique_ptr<Session> session_;
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_TESTS_SESSION_TEST_H_

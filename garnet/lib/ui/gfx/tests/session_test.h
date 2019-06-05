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

class SessionTest : public ErrorReportingTest, public EventReporter {
 protected:
  // | ::testing::Test |
  void SetUp() override;
  void TearDown() override;

  // |EventReporter|
  void EnqueueEvent(fuchsia::ui::gfx::Event event) override;
  void EnqueueEvent(fuchsia::ui::input::InputEvent event) override;
  void EnqueueEvent(fuchsia::ui::scenic::Command unhandled) override;

  // Subclasses should override to provide their own Session.
  virtual std::unique_ptr<SessionForTest> CreateSession();

  // Creates a SessionContext with only a SessionManager and a
  // FrameScheduler.
  SessionContext CreateBarebonesSessionContext();

  // Apply the specified Command.  Return true if it was applied successfully,
  // and false if an error occurred.
  bool Apply(::fuchsia::ui::gfx::Command command) {
    CommandContext context(nullptr);
    return session_->ApplyCommand(&context, std::move(command));
  }

  template <class ResourceT>
  fxl::RefPtr<ResourceT> FindResource(ResourceId id) {
    return session_->resources()->FindResource<ResourceT>(id);
  }

  std::unique_ptr<DisplayManager> display_manager_;
  std::unique_ptr<FrameScheduler> frame_scheduler_;
  std::unique_ptr<SessionForTest> session_;
  std::unique_ptr<SessionManager> session_manager_;
  std::vector<fuchsia::ui::scenic::Event> events_;
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_TESTS_SESSION_TEST_H_

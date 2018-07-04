// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_TESTS_SESSION_TEST_H_
#define GARNET_LIB_UI_GFX_TESTS_SESSION_TEST_H_

#include "garnet/lib/ui/gfx/tests/error_reporting_test.h"

#include <lib/fit/function.h>

#include "garnet/lib/ui/gfx/displays/display_manager.h"
#include "garnet/lib/ui/gfx/engine/engine.h"
#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/tests/mocks.h"
#include "lib/gtest/test_loop_fixture.h"

namespace scenic {
namespace gfx {
namespace test {

class SessionTest : public ErrorReportingTest, public EventReporter {
 protected:
  // | ::testing::Test |
  void SetUp() override;
  void TearDown() override;

  // |EventReporter|
  void EnqueueEvent(fuchsia::ui::scenic::Event event) override;

  // Subclasses should override to provide their own Engine.
  virtual std::unique_ptr<Engine> CreateEngine();

  // Apply the specified Command.  Return true if it was applied successfully,
  // and false if an error occurred.
  bool Apply(::fuchsia::ui::gfx::Command command) {
    return session_->ApplyCommand(std::move(command));
  }

  template <class ResourceT>
  fxl::RefPtr<ResourceT> FindResource(scenic::ResourceId id) {
    return session_->resources()->FindResource<ResourceT>(id);
  }

  DisplayManager display_manager_;
  std::unique_ptr<Engine> engine_;
  fxl::RefPtr<SessionForTest> session_;
  std::vector<fuchsia::ui::scenic::Event> events_;
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_TESTS_SESSION_TEST_H_

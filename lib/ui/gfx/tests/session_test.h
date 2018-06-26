// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_TESTS_SESSION_TEST_H_
#define GARNET_LIB_UI_GFX_TESTS_SESSION_TEST_H_

#include <lib/fit/function.h>

#include "garnet/lib/ui/gfx/engine/engine.h"
#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/tests/mocks.h"
#include "lib/gtest/test_loop_fixture.h"

namespace scenic {
namespace gfx {
namespace test {

class SessionTest : public ::gtest::TestLoopFixture,
                    public ErrorReporter,
                    public EventReporter {
 public:
  // ::testing::Test virtual method.
  void SetUp() override;

  // ::testing::Test virtual method.
  void TearDown() override;

  // Subclasses should override to provide their own Engine.
  virtual std::unique_ptr<Engine> CreateEngine();

 protected:
  // |ErrorReporter|
  void ReportError(fxl::LogSeverity severity,
                   std::string error_string) override;

  // |EventReporter|
  void EnqueueEvent(fuchsia::ui::scenic::Event event) override;

  // Apply the specified Command, and verify that it succeeds.
  bool Apply(::fuchsia::ui::gfx::Command command) {
    return session_->ApplyCommand(std::move(command));
  }

  template <class ResourceT>
  fxl::RefPtr<ResourceT> FindResource(scenic::ResourceId id) {
    return session_->resources()->FindResource<ResourceT>(id);
  }

  // Verify that the last reported error is as expected.  If no error is
  // expected, use nullptr as |expected_error_string|.
  void ExpectLastReportedError(const char* expected_error_string) {
    if (!expected_error_string) {
      EXPECT_TRUE(reported_errors_.empty());
    } else {
      EXPECT_EQ(reported_errors_.back(), expected_error_string);
    }
  }

  DisplayManager display_manager_;
  std::unique_ptr<Engine> engine_;
  fxl::RefPtr<SessionForTest> session_;
  std::vector<std::string> reported_errors_;
  std::vector<fuchsia::ui::scenic::Event> events_;
};

}  // namespace test
}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_TESTS_SESSION_TEST_H_

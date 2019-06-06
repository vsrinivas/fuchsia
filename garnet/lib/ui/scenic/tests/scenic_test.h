// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_TESTS_SCENIC_TEST_H_
#define GARNET_LIB_UI_SCENIC_TESTS_SCENIC_TEST_H_

#include <lib/async-testing/test_loop.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/scenic/cpp/session.h>

#include "garnet/lib/ui/scenic/scenic.h"
#include "garnet/lib/ui/scenic/util/error_reporter.h"
#include "gtest/gtest.h"

namespace scenic_impl {
namespace test {

// Base class that can be specialized to configure a Scenic with the systems
// required for a set of tests.
class ScenicTest : public ::gtest::TestLoopFixture,
                   public ErrorReporter,
                   public EventReporter {
 public:
  std::unique_ptr<::scenic::Session> CreateSession();

 protected:
  // ::testing::Test virtual method.
  void SetUp() override;

  // ::testing::Test virtual method.
  void TearDown() override;

  Scenic* scenic() { return scenic_.get(); }

  // Subclasses may override this to install any systems required by the test;
  // none are installed by default.
  virtual void InitializeScenic(Scenic* scenic);

  // |ErrorReporter|
  void ReportError(fxl::LogSeverity severity,
                   std::string error_string) override;

  // |EventReporter|
  void EnqueueEvent(fuchsia::ui::gfx::Event event) override;
  void EnqueueEvent(fuchsia::ui::input::InputEvent event) override;
  void EnqueueEvent(fuchsia::ui::scenic::Command event) override;

  // Verify that the last reported error is as expected.  If no error is
  // expected, use nullptr as |expected_error_string|.
  void ExpectLastReportedError(const char* expected_error_string) {
    if (!expected_error_string) {
      EXPECT_TRUE(reported_errors_.empty());
    } else {
      EXPECT_EQ(reported_errors_.back(), expected_error_string);
    }
  }

  static std::unique_ptr<sys::ComponentContext> app_context_;
  std::unique_ptr<Scenic> scenic_;
  std::vector<std::string> reported_errors_;
  std::vector<fuchsia::ui::scenic::Event> events_;
};

}  // namespace test
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_SCENIC_TESTS_SCENIC_TEST_H_

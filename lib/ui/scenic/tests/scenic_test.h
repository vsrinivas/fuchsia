// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_TESTS_SCENIC_TEST_H_
#define GARNET_LIB_UI_SCENIC_TESTS_SCENIC_TEST_H_

#include <lib/async-testutils/test_loop.h>

#include "garnet/lib/ui/scenic/scenic.h"
#include "garnet/lib/ui/scenic/util/error_reporter.h"
#include "gtest/gtest.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/gtest/test_loop_fixture.h"

namespace scenic {
namespace test {

// Base class that can be specialized to configure a Scenic with the systems
// required for a set of tests.
class ScenicTest : public ::gtest::TestLoopFixture,
                   public ErrorReporter,
                   public EventReporter {
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
  void EnqueueEvent(fuchsia::ui::scenic::Event event) override;

  // Verify that the last reported error is as expected.  If no error is
  // expected, use nullptr as |expected_error_string|.
  void ExpectLastReportedError(const char* expected_error_string) {
    if (!expected_error_string) {
      EXPECT_TRUE(reported_errors_.empty());
    } else {
      EXPECT_EQ(reported_errors_.back(), expected_error_string);
    }
  }

  static std::unique_ptr<component::StartupContext> app_context_;
  std::unique_ptr<Scenic> scenic_;
  std::vector<std::string> reported_errors_;
  std::vector<fuchsia::ui::scenic::Event> events_;
};

}  // namespace test
}  // namespace scenic

#endif  // GARNET_LIB_UI_SCENIC_TESTS_SCENIC_TEST_H_

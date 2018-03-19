// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_TESTS_SCENIC_TEST_H_
#define GARNET_LIB_UI_SCENIC_TESTS_SCENIC_TEST_H_

#include "garnet/lib/ui/scenic/scenic.h"
#include "garnet/lib/ui/scenic/tests/clock_task_runner.h"
#include "garnet/lib/ui/scenic/util/error_reporter.h"
#include "gtest/gtest.h"
#include "lib/fxl/tasks/task_runner.h"

namespace scenic {
namespace test {

// Base class that can be specialized to configure a Scenic with the systems
// required for a set of tests.
class ScenicTest : public ::testing::Test,
                   public ErrorReporter,
                   public EventReporter {
 public:
  // ::testing::Test virtual method.
  void SetUp() override;

  // ::testing::Test virtual method.
  void TearDown() override;

  // Allow tests to advance time and run tasks.
  void Tick(zx_time_t delta_nanos) { clock_task_runner_->Tick(delta_nanos); }

  Scenic* scenic() { return scenic_.get(); }

 protected:
  // Subclasses may override this to install any systems required by the test;
  // none are installed by default.
  virtual void InitializeScenic(Scenic* scenic);

  // |ErrorReporter|
  void ReportError(fxl::LogSeverity severity,
                   std::string error_string) override;

  // |EventReporter|
  void SendEvents(::f1dl::VectorPtr<ui::EventPtr> events) override;

  // Verify that the last reported error is as expected.  If no error is
  // expected, use nullptr as |expected_error_string|.
  void ExpectLastReportedError(const char* expected_error_string) {
    if (!expected_error_string) {
      EXPECT_TRUE(reported_errors_.empty());
    } else {
      EXPECT_EQ(reported_errors_.back(), expected_error_string);
    }
  }

  fxl::RefPtr<ClockTaskRunner> clock_task_runner_;
  std::unique_ptr<Scenic> scenic_;
  std::vector<std::string> reported_errors_;
  std::vector<ui::EventPtr> events_;
};

}  // namespace test
}  // namespace scenic

#endif  // GARNET_LIB_UI_SCENIC_TESTS_SCENIC_TEST_H_

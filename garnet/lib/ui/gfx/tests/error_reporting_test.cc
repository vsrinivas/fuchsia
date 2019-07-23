// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/error_reporting_test.h"

namespace scenic_impl {
namespace gfx {
namespace test {

namespace {
const char* kSetUpTearDownErrorMsg =
    "subclasses of ErrorReportingTest must call SetUp() and TearDown()";
}

void TestErrorReporter::ReportError(fxl::LogSeverity severity, std::string error_string) {
  // Typically, we don't want to log expected errors when running the tests.
  // However, it is useful to print these errors while writing the tests.
#ifndef NDEBUG
  // Allow force printing of errors via --verbose=3 as a parameter.
  if (FXL_VLOG_IS_ON(3)) {
    switch (severity) {
      case ::fxl::LOG_INFO:
        FXL_LOG(INFO) << error_string;
        break;
      case ::fxl::LOG_WARNING:
        FXL_LOG(WARNING) << error_string;
        break;
      case ::fxl::LOG_ERROR:
        FXL_LOG(ERROR) << error_string;
        break;
      case ::fxl::LOG_FATAL:
        FXL_LOG(FATAL) << error_string;
        break;
    }
  }
#endif

  reported_errors_.push_back(error_string);
}

void TestEventReporter::EnqueueEvent(fuchsia::ui::gfx::Event event) {
  fuchsia::ui::scenic::Event scenic_event;
  scenic_event.set_gfx(std::move(event));
  events_.push_back(std::move(scenic_event));
}

void TestEventReporter::EnqueueEvent(fuchsia::ui::input::InputEvent event) {
  fuchsia::ui::scenic::Event scenic_event;
  scenic_event.set_input(std::move(event));
  events_.push_back(std::move(scenic_event));
}

void TestEventReporter::EnqueueEvent(fuchsia::ui::scenic::Command unhandled) {
  fuchsia::ui::scenic::Event scenic_event;
  scenic_event.set_unhandled(std::move(unhandled));
  events_.push_back(std::move(scenic_event));
}

ErrorReportingTest::ErrorReportingTest() = default;

ErrorReportingTest::~ErrorReportingTest() {
  FXL_CHECK(setup_called_) << kSetUpTearDownErrorMsg;
  FXL_CHECK(teardown_called_) << kSetUpTearDownErrorMsg;
}

ErrorReporter* ErrorReportingTest::error_reporter() const { return shared_error_reporter().get(); }

EventReporter* ErrorReportingTest::event_reporter() const { return shared_event_reporter().get(); }

std::shared_ptr<ErrorReporter> ErrorReportingTest::shared_error_reporter() const {
  FXL_CHECK(setup_called_) << kSetUpTearDownErrorMsg;
  return error_reporter_;
}

std::shared_ptr<EventReporter> ErrorReportingTest::shared_event_reporter() const {
  FXL_CHECK(setup_called_) << kSetUpTearDownErrorMsg;
  return event_reporter_;
}

const std::vector<fuchsia::ui::scenic::Event>& ErrorReportingTest::events() const {
  FXL_CHECK(setup_called_) << kSetUpTearDownErrorMsg;
  return event_reporter_->events();
}

void ErrorReportingTest::ExpectErrorAt(size_t pos, const char* expected_error_string) {
  if (expected_error_string) {
    // Ensure pos is inside the array and references the error string.
    EXPECT_LT(pos, error_reporter_->errors().size());
    EXPECT_STREQ(error_reporter_->errors()[pos].c_str(), expected_error_string);
  } else {
    // Error string was not present, so ensure pos is outside of the errors
    // array.
    EXPECT_TRUE(pos >= error_reporter_->errors().size());
  }
}

void ErrorReportingTest::ExpectLastReportedError(const char* expected_error_string) {
  if (error_reporter_->errors().empty()) {
    EXPECT_EQ(nullptr, expected_error_string);
  } else {
    ExpectErrorAt(error_reporter_->errors().size() - 1, expected_error_string);
  }
}

void ErrorReportingTest::SetUp() {
  setup_called_ = true;

  error_reporter_ = std::make_shared<TestErrorReporter>();
  event_reporter_ = std::make_shared<TestEventReporter>();
}

void ErrorReportingTest::TearDown() {
  teardown_called_ = true;

  error_reporter_.reset();
  event_reporter_.reset();
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

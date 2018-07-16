// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/tests/scenic_test.h"

namespace scenic {
namespace test {

std::unique_ptr<component::StartupContext> ScenicTest::app_context_;

void ScenicTest::SetUp() {
  // TODO(SCN-720): Wrap CreateFromStartupInfo using ::gtest::Environment
  // instead of this hack.  This code has the chance to break non-ScenicTests.
  if (app_context_ == nullptr) {
    app_context_ = component::StartupContext::CreateFromStartupInfo();
  }
  scenic_ =
      std::make_unique<Scenic>(app_context_.get(), [this] { QuitLoop(); });
  InitializeScenic(scenic_.get());
}

void ScenicTest::TearDown() {
  reported_errors_.clear();
  events_.clear();
  scenic_.reset();
}

void ScenicTest::InitializeScenic(Scenic* scenic) {}

void ScenicTest::ReportError(fxl::LogSeverity severity,
                             std::string error_string) {
// Typically, we don't want to log expected errors when running the tests.
// However, it is useful to print these errors while writing the tests.
#if 0
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
#endif
  reported_errors_.push_back(error_string);
}

void ScenicTest::EnqueueEvent(fuchsia::ui::scenic::Event event) {
  events_.push_back(std::move(event));
}

}  // namespace test
}  // namespace scenic

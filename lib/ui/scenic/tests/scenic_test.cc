// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/tests/scenic_test.h"

extern std::unique_ptr<app::ApplicationContext> g_application_context;

namespace mz {
namespace test {

void ScenicTest::SetUp() {
  clock_task_runner_ = ClockTaskRunner::New(0);
  scenic_ = std::make_unique<Scenic>(g_application_context.get(),
                                     clock_task_runner_.get(),
                                     clock_task_runner_.get());
  InitializeScenic(scenic_.get());
}

void ScenicTest::TearDown() {
  reported_errors_.clear();
  scenic_.reset();
  clock_task_runner_ = nullptr;
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

void ScenicTest::SendEvents(::f1dl::Array<ui::EventPtr> events) {
  for (auto& event : events) {
    events_.push_back(std::move(event));
  }
}

}  // namespace test
}  // namespace mz

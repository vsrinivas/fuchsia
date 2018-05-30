// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/session_test.h"

#include "garnet/lib/ui/gfx/tests/mocks.h"

namespace scenic {
namespace gfx {
namespace test {

void SessionTest::SetUp() {
  engine_ = std::unique_ptr<Engine>(CreateEngine());
  session_ = fxl::MakeRefCounted<SessionForTest>(1, engine_.get(), this, this);
}

// ::testing::Test virtual method.
void SessionTest::TearDown() {
  reported_errors_.clear();
  session_->TearDown();
  session_ = nullptr;
  engine_.reset();
}

std::unique_ptr<Engine> SessionTest::CreateEngine() {
  return std::make_unique<EngineForTest>(&display_manager_, nullptr);
}

void SessionTest::ReportError(fxl::LogSeverity severity,
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

void SessionTest::EnqueueEvent(fuchsia::ui::scenic::Event event) {
  events_.push_back(std::move(event));
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/tests/session_test.h"

#include "lib/ftl/synchronization/waitable_event.h"
#include "lib/mtl/tasks/message_loop.h"

#include "apps/mozart/src/scene_manager/tests/mocks.h"

namespace scene_manager {
namespace test {

void SessionTest::SetUp() {
  engine_ = std::unique_ptr<Engine>(CreateEngine());
  session_ = ftl::MakeRefCounted<Session>(1, engine_.get(), this, this);
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

void SessionTest::ReportError(ftl::LogSeverity severity,
                              std::string error_string) {
// Typically, we don't want to log expected errors when running the tests.
// However, it is useful to print these errors while writing the tests.
#if 0
  switch (severity) {
    case ::ftl::LOG_INFO:
      FTL_LOG(INFO) << error_string;
      break;
    case ::ftl::LOG_WARNING:
      FTL_LOG(WARNING) << error_string;
      break;
    case ::ftl::LOG_ERROR:
      FTL_LOG(ERROR) << error_string;
      break;
    case ::ftl::LOG_FATAL:
      FTL_LOG(FATAL) << error_string;
      break;
  }
#endif
  reported_errors_.push_back(error_string);
}

void SessionTest::SendEvents(::fidl::Array<scenic::EventPtr> events) {
  for (auto& event : events) {
    events_.push_back(std::move(event));
  }
}

ftl::RefPtr<ftl::TaskRunner> SessionThreadedTest::TaskRunner() const {
  return thread_.TaskRunner();
}

void SessionThreadedTest::SetUp() {
  thread_.Run();
  ftl::AutoResetWaitableEvent setup_latch;
  TaskRunner()->PostTask([this, &setup_latch]() {
    SessionTest::SetUp();
    setup_latch.Signal();
  });
  setup_latch.Wait();
}

void SessionThreadedTest::TearDown() {
  TaskRunner()->PostTask([this]() {
    SessionTest::TearDown();
    mtl::MessageLoop::GetCurrent()->QuitNow();
  });
  thread_.Join();
}

void SessionThreadedTest::PostTaskSync(ftl::Closure callback) {
  ftl::AutoResetWaitableEvent latch;
  PostTask(latch, callback);
  latch.Wait();
}

void SessionThreadedTest::PostTask(ftl::AutoResetWaitableEvent& latch,
                                   ftl::Closure callback) {
  FTL_DCHECK(callback);
  TaskRunner()->PostTask([&latch, callback]() {
    callback();
    latch.Signal();
  });
}

}  // namespace test
}  // namespace scene_manager

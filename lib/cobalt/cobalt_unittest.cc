// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/cobalt/cobalt.h"

#include "lib/fxl/macros.h"
#include "lib/fxl/logging.h"
#include "peridot/lib/gtest/test_with_message_loop.h"

namespace cobalt {

constexpr int32_t kFakeCobaltProjectId = 1;
constexpr int32_t kFakeCobaltMetricId = 2;
constexpr int32_t kFakeCobaltEncodingId = 3;
constexpr int32_t kFakeCobaltEvent = 4;

class FakeTaskRunner : public fxl::TaskRunner {
 public:
  inline static fxl::RefPtr<FakeTaskRunner> Create() {
    return AdoptRef(new FakeTaskRunner());
  }

  void PostTask(fxl::Closure task) override {}

  void PostTaskForTime(fxl::Closure task,
                       fxl::TimePoint target_time) override {}

  void PostDelayedTask(fxl::Closure task, fxl::TimeDelta delay) override {}

  bool RunsTasksOnCurrentThread() override {
    runs_task_on_current_thread_called = true;
    return true;
  }

  bool runs_task_on_current_thread_called = false;
};

class CobaltTest : public gtest::TestWithMessageLoop {
 public:
  CobaltTest() :
    app_context_(app::ApplicationContext::CreateFromStartupInfo()),
    task_runner_(FakeTaskRunner::Create()) {}
  ~CobaltTest() override {}

  app::ApplicationContext* app_context() {
    return app_context_.get();
  }

  fxl::RefPtr<FakeTaskRunner> task_runner() {
    return task_runner_;
  }

 private:
  std::unique_ptr<app::ApplicationContext> app_context_;
  fxl::RefPtr<FakeTaskRunner> task_runner_;
  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltTest);
};

TEST_F(CobaltTest, InitializeCobalt) {
  CobaltContext* cobalt_context = nullptr;
  auto ac = InitializeCobalt(task_runner(), app_context(), kFakeCobaltProjectId,
                             kFakeCobaltMetricId, kFakeCobaltEncodingId,
                             &cobalt_context);
  EXPECT_NE(cobalt_context, nullptr);
  ac.call();
  EXPECT_EQ(cobalt_context, nullptr);
}

TEST_F(CobaltTest, ReportEvent) {
  CobaltContext* cobalt_context = nullptr;
  auto ac = InitializeCobalt(task_runner(), app_context(), kFakeCobaltProjectId,
                             kFakeCobaltMetricId, kFakeCobaltEncodingId,
                             &cobalt_context);
  ReportEvent(kFakeCobaltEvent, cobalt_context);
  // TODO(miguelfrde): this is not exactly testing what we want. We would like
  // to test if cobalt AddIndexObservation is called properly. We could
  // accomplish this with a custom ApplicationContext that allows us to mock
  // cobalt encoder.
  EXPECT_TRUE(task_runner()->runs_task_on_current_thread_called);
}

}  // namespace cobalt

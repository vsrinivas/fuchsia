// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/test_runner/lib/reporting/reporter.h"

#include "application/lib/app/application_context.h"
#include "apps/test_runner/lib/reporting/results_queue.h"
#include "apps/test_runner/services/test_runner.fidl.h"
#include "apps/tracing/lib/trace/provider.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/tasks/message_loop_handler.h"

namespace test_runner {

void NullTestRunner::Identify(const ::fidl::String& program_name) {}

void NullTestRunner::ReportResult(TestResultPtr result) {}

void NullTestRunner::Fail(const ::fidl::String& log_message) {}

void NullTestRunner::Done(const DoneCallback& callback) {}

void NullTestRunner::Teardown(const std::function<void()>& callback) {
  callback();
}

void NullTestRunner::WillTerminate(double withinSeconds) {}

void NullTestRunner::SetTestPointCount(int64_t count) {}

void NullTestRunner::PassTestPoint() {}

Reporter::Reporter(std::string identity, ResultsQueue* queue)
    : identity_(std::move(identity)),
      queue_(queue) {}

Reporter::~Reporter() {}

void Reporter::OnHandleReady(mx_handle_t handle, mx_signals_t pending) {
  while(!queue_->Empty()) {
    TestResultPtr result = queue_->Pop();
    if (result) {
      test_runner()->ReportResult(std::move(result));
    } else {
      FTL_DCHECK(queue_->Empty());
      Stop();
      return;
    }
  }
}

void Reporter::Start(app::ApplicationContext* context) {
  if (context->has_environment_services()) {
    tracing::InitializeTracer(context, {identity_});
    test_runner_ = context
        ->ConnectToEnvironmentService<test_runner::TestRunner>();
    test_runner_.set_connection_error_handler([this]() {
      test_runner_ = nullptr;
    });
  }

  test_runner()->Identify(identity_);

  mtl::MessageLoop::GetCurrent()->AddHandler(
    this, queue_->event()->get(), ResultsQueue::kSignal);
}

void Reporter::Stop() {
  test_runner()->Teardown([] {
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  });
}

}  // namespace test_runner

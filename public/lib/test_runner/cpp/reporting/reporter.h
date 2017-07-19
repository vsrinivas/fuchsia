// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TEST_RUNNER_LIB_REPORTER_H_
#define APPS_TEST_RUNNER_LIB_REPORTER_H_

#include "application/lib/app/application_context.h"
#include "apps/test_runner/lib/reporting/results_queue.h"
#include "apps/test_runner/services/test_runner.fidl.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/tasks/message_loop_handler.h"

namespace test_runner {

// Handles TestRunner calls when the TestRunner service isn't running.
class NullTestRunner : public TestRunner {
 public:
  void Identify(const ::fidl::String& program_name,
                const IdentifyCallback& callback) override;
  void ReportResult(TestResultPtr result) override;
  void Fail(const ::fidl::String& log_message) override;
  void Done(const DoneCallback& callback) override;
  void Teardown(const std::function<void()>& callback) override;
  void WillTerminate(double withinSeconds) override;
  void SetTestPointCount(int64_t count) override;
  void PassTestPoint() override;
};

// Pulls tests results from a ResultsQueue and reports them to the TestRunner
// FIDL service.
class Reporter : public mtl::MessageLoopHandler {
 public:
  Reporter(std::string identity, ResultsQueue* queue);

  ~Reporter();

  // MessageLoopHandler override.
  // Gets called when the queue is ready to read. Pops TestResultPtr instances
  // from the queue until empty, and reports them to the TestRunner service.
  void OnHandleReady(mx_handle_t handle, mx_signals_t pending,
                     uint64_t count) override;

  // Attempts to connect to TestRunner FIDL service. If successful, registers an
  // event handler for when the queue is ready to read, so that incoming test
  // results will be reported.
  void Start(app::ApplicationContext* context);

  // Return whether the connection to TestRunner was successful.
  bool connected();

 private:
  TestRunner* test_runner() {
    return test_runner_ ? test_runner_.get() : &null_test_runner_;
  }

  // Reports teardown to the TestRunner service, waits for acknowledgement, and
  // then quits the message loop.
  void Stop();

  std::string identity_;
  ResultsQueue* queue_;
  TestRunnerPtr test_runner_;
  NullTestRunner null_test_runner_;
  bool connected_{};
};

}  // namespace test_runner

#endif  // APPS_TEST_RUNNER_LIB_REPORTER_H_

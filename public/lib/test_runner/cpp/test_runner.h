// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TEST_RUNNER_TEST_RUNNER_H_
#define APPS_TEST_RUNNER_TEST_RUNNER_H_

#include <memory>
#include "lib/app/cpp/application_context.h"
#include "apps/test_runner/lib/scope.h"
#include "apps/test_runner/lib/test_runner_store_impl.h"
#include "apps/test_runner/services/test_runner.fidl.h"
#include "lib/fxl/tasks/one_shot_timer.h"

namespace test_runner {

class TestRunObserver {
 public:
  virtual void SendMessage(const std::string& test_id,
                           const std::string& operation,
                           const std::string& msg) = 0;
  virtual void Teardown(const std::string& test_id, bool success) = 0;
  virtual ~TestRunObserver();
};

class TestRunContext;

// Implements the TestRunner service which is available in the
// ApplicationEnvironment of the test processes. Calls made to this service are
// forwarded to and handled by TestRunContext.
class TestRunnerImpl : public TestRunner {
 public:
  TestRunnerImpl(fidl::InterfaceRequest<TestRunner> request,
                 TestRunContext* test_run_context);

  const std::string& program_name() const;

  bool waiting_for_termination() const;

  // Will be called if this |TestRunner| is waiting for termination while
  // another one calls |Teardown|. If this is called then when this does
  // terminate it will call |Teardown|.
  void TeardownAfterTermination();

  // Indicates if there are test points that haven't been passed yet.
  bool TestPointsRemaining() { return remaining_test_points_ > 0; }

 private:
  // |TestRunner|
  void Identify(const fidl::String& program_name,
                const IdentifyCallback& callback) override;
  // |TestRunner|
  void ReportResult(TestResultPtr result) override;
  // |TestRunner|
  void Fail(const fidl::String& log_message) override;
  // |TestRunner|
  void Done(const DoneCallback& callback) override;
  // |TestRunner|
  void Teardown(const TeardownCallback& callback) override;
  // |TestRunner|
  void WillTerminate(double withinSeconds) override;
  // |TestRunner|
  void SetTestPointCount(int64_t count) override;
  // |TestRunner|
  void PassTestPoint() override;

  fidl::Binding<TestRunner> binding_;
  TestRunContext* const test_run_context_;
  std::string program_name_ = "UNKNOWN";
  bool waiting_for_termination_ = false;
  fxl::OneShotTimer termination_timer_;
  bool teardown_after_termination_ = false;
  int64_t remaining_test_points_ = -1;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestRunnerImpl);
};

// TestRunContext represents a single run of a test program. Given a test
// program to run, it runs it in a new ApplicationEnvironment and provides the
// environment a TestRunner service to report completion. When tests are done,
// their completion is reported back to TestRunObserver (which is responsible
// for deleting TestRunContext). If the child application stops without
// reporting anything, we declare the test a failure.
class TestRunContext {
 public:
  TestRunContext(std::shared_ptr<app::ApplicationContext> app_context,
                 TestRunObserver* connection,
                 const std::string& test_id,
                 const std::string& url,
                 const std::vector<std::string>& args);

  // Called from TestRunnerImpl, the actual implemention of |TestRunner|.
  void StopTrackingClient(TestRunnerImpl* client, bool crashed);
  void ReportResult(TestResultPtr result);
  void Fail(const fidl::String& log_message);
  void Teardown(TestRunnerImpl* teardown_client);

 private:
  app::ApplicationControllerPtr child_app_controller_;
  std::unique_ptr<Scope> child_env_scope_;

  TestRunObserver* const test_runner_connection_;
  std::vector<std::unique_ptr<TestRunnerImpl>> test_runner_clients_;
  TestRunnerStoreImpl test_runner_store_;

  // This is a tag that we use to identify the test that was run. For now, it
  // helps distinguish between multiple test outputs to the device log.
  const std::string test_id_;
  bool success_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestRunContext);
};

}  // namespace test_runner

#endif  // APPS_TEST_RUNNER_TEST_RUNNER_H_

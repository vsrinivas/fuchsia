// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_TEST_RUNNER_TEST_RUNNER_H_
#define APPS_MODULAR_TEST_RUNNER_TEST_RUNNER_H_

#include <memory>
#include "application/lib/app/application_context.h"
#include "apps/modular/lib/fidl/scope.h"
#include "apps/modular/services/test_runner/test_runner.fidl.h"
#include "apps/modular/src/test_runner/test_runner_store_impl.h"
#include "lib/ftl/tasks/one_shot_timer.h"

namespace modular {
namespace testing {

class TestRunObserver {
 public:
  virtual void SendMessage(const std::string& test_id,
                           const std::string& operation,
                           const std::string& msg) = 0;
  virtual void Teardown(const std::string& test_id, bool success) = 0;
};

class TestRunContext;

// Implements the TestRunner service which is available in the
// ApplicationEnvironment of the test processes. Calls made to this service are
// forwarded to and handled by TestRunContext.
class TestRunnerImpl : public testing::TestRunner {
 public:
  TestRunnerImpl(fidl::InterfaceRequest<testing::TestRunner> request,
                 TestRunContext* test_run_context);

  const std::string& test_name() const;

  bool waiting_for_termination() const;

  // Will be called if this |TestRunner| is waiting for termination while
  // another one calls |Teardown|. If this is called then when this does
  // terminate it will call |Teardown|.
  void TeardownAfterTermination();

 private:
  // |TestRunner|
  void Identify(const fidl::String& test_name) override;
  // |TestRunner|
  void Fail(const fidl::String& log_message) override;
  // |TestRunner|
  void Done() override;
  // |TestRunner|
  void Teardown() override;
  // |TestRunner|
  void WillTerminate(double withinSeconds) override;

  fidl::Binding<testing::TestRunner> binding_;
  TestRunContext* const test_run_context_;
  std::string test_name_ = "UNKNOWN";
  bool waiting_for_termination_ = false;
  ftl::OneShotTimer termination_timer_;
  bool teardown_after_termination_ = false;

  FTL_DISALLOW_COPY_AND_ASSIGN(TestRunnerImpl);
};

// TestRunContext represents a single run of a test. Given a test to run, it
// runs it in a new ApplicationEnvironment and provides the environment a
// TestRunner service to report completion. When tests are done, their
// completion is reported back to TestRunObserver (which is responsible for
// deleting TestRunContext). If the child application stops without reporting
// anything, we declare the test a failure.
class TestRunContext {
 public:
  TestRunContext(std::shared_ptr<app::ApplicationContext> app_context,
                 TestRunObserver* connection,
                 const std::string& test_id,
                 const std::string& url,
                 const std::vector<std::string>& args);

  // Called from TestRunnerImpl, the actual implemention of |TestRunner|.
  void StopTrackingClient(TestRunnerImpl* client, bool crashed);
  void Fail(const fidl::String& log_message);
  void Teardown(TestRunnerImpl* teardown_client);

 private:
  app::ApplicationControllerPtr child_app_controller_;
  std::unique_ptr<Scope> child_env_scope_;

  TestRunObserver* const test_runner_connection_;
  std::vector<std::unique_ptr<TestRunnerImpl>> test_runner_clients_;
  testing::TestRunnerStoreImpl test_runner_store_;

  // This is a tag that we use to identify the test that was run. For now, it
  // helps distinguish between multiple test outputs to the device log.
  const std::string test_id_;
  bool success_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TestRunContext);
};

}  // namespace testing
}  // namespace modular

#endif  // APPS_MODULAR_TEST_RUNNER_TEST_RUNNER_H_

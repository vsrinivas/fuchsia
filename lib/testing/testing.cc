// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/testing/testing.h"

#include "apps/test_runner/services/test_runner.fidl.h"

namespace modular {
namespace testing {

static test_runner::TestRunnerPtr g_test_runner;
static test_runner::TestRunnerStorePtr g_test_runner_store;
static std::set<std::string> g_test_points;

void Init(app::ApplicationContext* app_context, const std::string& identity) {
  FTL_DCHECK(app_context);
  FTL_DCHECK(!g_test_runner.is_bound());
  FTL_DCHECK(!g_test_runner_store.is_bound());

  g_test_runner =
      app_context->ConnectToEnvironmentService<test_runner::TestRunner>();
  g_test_runner.set_connection_error_handler([] {
    FTL_LOG(FATAL) << "Could not connect to TestRunner. Make sure the test is "
                      "running under a TestRunner environment.";
  });
  g_test_runner->Identify(identity);
  g_test_runner->SetTestPointCount(g_test_points.size());
  g_test_runner_store =
      app_context->ConnectToEnvironmentService<test_runner::TestRunnerStore>();
}

void Fail(const std::string& log_msg) {
  if (g_test_runner.is_bound()) {
    g_test_runner->Fail(log_msg);
  }
}

void Done(const std::function<void()>& ack) {
  if (g_test_runner.is_bound()) {
    g_test_runner->Done(ack);
    g_test_runner.reset();
  } else {
    ack();
  }

  if (g_test_runner_store.is_bound())
    g_test_runner_store.reset();
}

void Done() {
  FTL_LOG(WARNING) << "Done() used in a race. Test is flaky."
                   << " Use Done with callback instead.";
  Done([]{});
}

void Teardown(const std::function<void()>& ack) {
  if (g_test_runner.is_bound()) {
    g_test_runner->Teardown(ack);
    g_test_runner.reset();
  } else {
    ack();
  }

  if (g_test_runner_store.is_bound())
    g_test_runner_store.reset();
}

void Teardown() {
  FTL_LOG(WARNING) << "Teardown() used in a race. Test is flaky."
                   << " Use Teardown with callback instead.";
  Teardown([]{});
}

void WillTerminate(double withinSeconds) {
  if (g_test_runner.is_bound()) {
    g_test_runner->WillTerminate(withinSeconds);
  }
}

test_runner::TestRunnerStore* GetStore() {
  FTL_DCHECK(g_test_runner_store.is_bound());
  return g_test_runner_store.get();
}

namespace internal {

void RegisterTestPoint(const std::string& label) {
  // Test points must have unique labels.
  FTL_CHECK(g_test_points.find(label) == g_test_points.end());

  // Test points can only be registered before Init is called.
  FTL_CHECK(!g_test_runner.is_bound());

  g_test_points.insert(label);
}

void PassTestPoint(const std::string& label) {
  // Test points can only be passed once.
  FTL_CHECK(g_test_points.find(label) != g_test_points.end());

  // Test points can only be passed after initialization.
  FTL_CHECK(g_test_runner.is_bound());

  g_test_points.erase(label);
  g_test_runner->PassTestPoint();
}

}  // namespace internal
}  // namespace testing
}  // namespace modular

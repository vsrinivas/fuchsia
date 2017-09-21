// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/testing/testing.h"

#include "lib/test_runner/fidl/test_runner.fidl.h"

namespace modular {
namespace testing {

namespace {
test_runner::TestRunnerPtr g_test_runner;
test_runner::TestRunnerStorePtr g_test_runner_store;
std::set<std::string> g_test_points;
bool g_connected;
}  // namespace

void Init(app::ApplicationContext* app_context, const std::string& identity) {
  FXL_CHECK(app_context);
  FXL_CHECK(!g_test_runner.is_bound());
  FXL_CHECK(!g_test_runner_store.is_bound());

  g_test_runner =
      app_context->ConnectToEnvironmentService<test_runner::TestRunner>();
  g_test_runner.set_connection_error_handler([] {
    if (g_connected) {
      FXL_LOG(ERROR) << "Lost connection to TestRunner. Did the active test "
                        "call Logout() while modules were still running?";
    } else {
      FXL_LOG(ERROR) << "This application must be run under test_runner.";
    }
    exit(1);
  });
  g_test_runner->Identify(identity, [] { g_connected = true; });
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
    g_test_runner->Done([ack] {
      ack();
      g_test_runner.reset();
    });
  } else {
    ack();
  }

  if (g_test_runner_store.is_bound()) {
    g_test_runner_store.reset();
  }
}

void Teardown(const std::function<void()>& ack) {
  if (g_test_runner.is_bound()) {
    g_test_runner->Teardown([ack] {
      ack();
      g_test_runner.reset();
    });
  } else {
    ack();
  }

  if (g_test_runner_store.is_bound()) {
    g_test_runner_store.reset();
  }
}

void WillTerminate(double withinSeconds) {
  if (g_test_runner.is_bound()) {
    g_test_runner->WillTerminate(withinSeconds);
  }
}

test_runner::TestRunnerStore* GetStore() {
  FXL_CHECK(g_test_runner_store.is_bound());
  return g_test_runner_store.get();
}

namespace internal {

void RegisterTestPoint(const std::string& label) {
  // Test points can only be registered before Init is called.
  FXL_CHECK(!g_test_runner.is_bound())
      << "Test Runner connection not bound. You must call "
      << "ComponentBase::TestInit() before registering "
      << "\"" << label << "\".";

  auto inserted = g_test_points.insert(label);

  // Test points must have unique labels.
  FXL_CHECK(inserted.second) << "Test points must have unique labels. "
                             << "\"" << label << "\" is repeated.";
}

void PassTestPoint(const std::string& label) {
  // Test points can only be passed after initialization.
  FXL_CHECK(g_test_runner.is_bound())
      << "Test Runner connection not bound. You must call "
      << "ComponentBase::TestInit() before \"" << label << "\".Pass() can be "
      << "called.";

  // Test points can only be passed once.
  FXL_CHECK(g_test_points.erase(label))
      << "TEST FAILED: Test point can only be passed once. "
      << "\"" << label << "\".Pass() has been called twice.";

  g_test_runner->PassTestPoint();
}

}  // namespace internal
}  // namespace testing
}  // namespace modular

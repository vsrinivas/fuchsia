// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/testing/testing.h"

#include "apps/modular/services/test_runner/test_runner.fidl.h"

namespace modular {
namespace testing {

static TestRunnerPtr g_test_runner;
static TestRunnerStorePtr g_test_runner_store;
static std::set<std::string> g_test_points;

void Init(app::ApplicationContext* app_context, const std::string& identity) {
  FTL_DCHECK(app_context);
  FTL_DCHECK(!g_test_runner.is_bound());
  FTL_DCHECK(!g_test_runner_store.is_bound());

  g_test_runner = app_context->ConnectToEnvironmentService<TestRunner>();
  g_test_runner->Identify(identity);
  g_test_runner->SetTestPointCount(g_test_points.size());
  g_test_runner_store =
      app_context->ConnectToEnvironmentService<TestRunnerStore>();
}

void Fail(const std::string& log_msg) {
  if (g_test_runner.is_bound()) {
    g_test_runner->Fail(log_msg);
  }
}

void Done() {
  if (g_test_runner.is_bound()) {
    g_test_runner->Done();
    g_test_runner.reset();
  }
  if (g_test_runner_store.is_bound())
    g_test_runner_store.reset();
}

void Teardown() {
  if (g_test_runner.is_bound()) {
    g_test_runner->Teardown();
    g_test_runner.reset();
  }
  if (g_test_runner_store.is_bound())
    g_test_runner_store.reset();
}

void WillTerminate(double withinSeconds) {
  if (g_test_runner.is_bound()) {
    g_test_runner->WillTerminate(withinSeconds);
  }
}

TestRunnerStore* GetStore() {
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

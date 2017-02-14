// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/testing/testing.h"

#include "apps/modular/services/test_runner/test_runner.fidl.h"

namespace modular {
namespace testing {

static TestRunnerPtr g_test_runner;
static TestRunnerStorePtr g_test_runner_store;

void Init(app::ApplicationContext* app_context, const std::string& identity) {
  FTL_DCHECK(app_context);
  FTL_DCHECK(!g_test_runner.is_bound());
  FTL_DCHECK(!g_test_runner_store.is_bound());

  g_test_runner = app_context->ConnectToEnvironmentService<TestRunner>();
  g_test_runner->Identify(identity);
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

TestRunnerStore* GetStore() {
  FTL_DCHECK(g_test_runner_store.is_bound());
  return g_test_runner_store.get();
}

}  // namespace testing
}  // namespace modular

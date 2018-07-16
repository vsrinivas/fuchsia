// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/testing/testing.h"

#include <set>

#include <fuchsia/testing/runner/cpp/fidl.h>
#include <lib/fxl/logging.h>

using fuchsia::testing::runner::TestRunner;
using fuchsia::testing::runner::TestRunnerPtr;
using fuchsia::testing::runner::TestRunnerStore;
using fuchsia::testing::runner::TestRunnerStorePtr;

namespace modular {
namespace testing {

namespace {
TestRunnerPtr g_test_runner;
TestRunnerStorePtr g_test_runner_store;
std::set<std::string> g_test_points;
bool g_connected;
}  // namespace

void Init(component::StartupContext* context, const std::string& identity) {
  FXL_CHECK(context);
  FXL_CHECK(!g_test_runner.is_bound());
  FXL_CHECK(!g_test_runner_store.is_bound());

  g_test_runner = context->ConnectToEnvironmentService<TestRunner>();
  g_test_runner.set_error_handler([] {
    if (g_connected) {
      FXL_LOG(ERROR) << "Lost connection to TestRunner. This indicates that "
                        "there was an observed process that was terminated "
                        "without calling TestRunner.Done().";
    } else {
      FXL_LOG(ERROR) << "This application must be run under test_runner.";
    }
    exit(1);
  });
  g_test_runner->Identify(identity, [] { g_connected = true; });
  g_test_runner->SetTestPointCount(g_test_points.size());
  g_test_runner_store = context->ConnectToEnvironmentService<TestRunnerStore>();
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
      g_test_runner.Unbind();
    });
  } else {
    ack();
  }

  if (g_test_runner_store.is_bound()) {
    g_test_runner_store.Unbind();
  }
}

void Teardown(const std::function<void()>& ack) {
  if (g_test_runner.is_bound()) {
    g_test_runner->Teardown([ack] {
      ack();
      g_test_runner.Unbind();
    });
  } else {
    ack();
  }

  if (g_test_runner_store.is_bound()) {
    g_test_runner_store.Unbind();
  }
}

TestRunnerStore* GetStore() {
  FXL_CHECK(g_test_runner_store.is_bound());
  return g_test_runner_store.get();
}

std::function<void(fidl::StringPtr)> NewBarrierClosure(
    const int limit, std::function<void()> proceed) {
  return [limit, count = std::make_shared<int>(0),
          proceed = std::move(proceed)](fidl::StringPtr value) {
    ++*count;
    if (*count == limit) {
      proceed();
    }
  };
}

void Put(const fidl::StringPtr& key, const fidl::StringPtr& value) {
  modular::testing::GetStore()->Put(key, value, [] {});
}

void Get(const fidl::StringPtr& key,
         std::function<void(fidl::StringPtr)> callback) {
  modular::testing::GetStore()->Get(key, std::move(callback));
}

void Signal(const fidl::StringPtr& condition) {
  modular::testing::GetStore()->Put(condition, condition, [] {});
}

void Await(const fidl::StringPtr& condition, std::function<void()> cont) {
  modular::testing::GetStore()->Get(
      condition, [cont = std::move(cont)](fidl::StringPtr) { cont(); });
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

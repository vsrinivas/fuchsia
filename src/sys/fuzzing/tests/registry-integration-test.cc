// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/process.h>
#include <zircon/status.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/testing/async-test.h"
#include "src/sys/fuzzing/common/testing/integration-test-base.h"
#include "src/sys/fuzzing/testing/runner.h"

namespace fuzzing {
namespace {

using fuchsia::fuzzer::ControllerPtr;
using fuchsia::fuzzer::Options;
using fuchsia::fuzzer::Registrar;
using fuchsia::fuzzer::Registry;
using fuchsia::fuzzer::RegistryPtr;

// Test fixtures.

const char* kFuzzerUrl = "an arbitrary string";

// This class maintains the component context and connection to the fuzz-registry.
class RegistryIntegrationTest : public IntegrationTestBase {
 protected:
  void SetUp() override {
    IntegrationTestBase::SetUp();
    context_ = sys::ComponentContext::Create();
  }

  // Launch a fuzzer and give it a channel to register itself with the fuzz-registry.
  void Register() {
    // Connect a channel to the fuzz-registry.
    fidl::InterfaceHandle<Registrar> handle;
    auto status = context_->svc()->Connect(handle.NewRequest());
    ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);
    auto result = Start("/pkg/bin/component_fuzzing_test_fuzzer", handle.TakeChannel());
    ASSERT_TRUE(result.is_ok());
  }

  // Promises to connect the |controller| once a fuzzer is registered.
  ZxPromise<> Connect(ControllerPtr* controller, zx::duration timeout) {
    auto status = context_->svc()->Connect(registry_.NewRequest());
    if (status != ZX_OK) {
      return fpromise::make_promise([status]() -> ZxResult<> { return fpromise::error(status); });
    }
    Bridge<zx_status_t> bridge;
    registry_->Connect(kFuzzerUrl, controller->NewRequest(), timeout.get(),
                       bridge.completer.bind());
    return bridge.consumer.promise().then(
        [](Result<zx_status_t>& result) { return AsZxResult(result); });
  }

  // Promises to stop a fuzzer if running.
  ZxPromise<> Disconnect() {
    Bridge<zx_status_t> bridge;
    registry_->Disconnect(kFuzzerUrl, bridge.completer.bind());
    return bridge.consumer.promise()
        .then([](Result<zx_status_t>& result) { return AsZxResult(result); })
        .and_then(AwaitTermination());
  }

 private:
  std::unique_ptr<sys::ComponentContext> context_;
  RegistryPtr registry_;
};

}  // namespace

// Unit tests

TEST_F(RegistryIntegrationTest, RegisterThenConnect) {
  ASSERT_NO_FATAL_FAILURE(Register());
  ControllerPtr controller;
  FUZZING_EXPECT_OK(Connect(&controller, zx::sec(1)));
  RunUntilIdle();

  // Verify connected.
  Bridge<Options> bridge;
  controller->GetOptions(bridge.completer.bind());
  FUZZING_EXPECT_OK(bridge.consumer.promise_or(fpromise::error()));
  RunUntilIdle();
  FUZZING_EXPECT_OK(Disconnect());
  RunUntilIdle();
}

TEST_F(RegistryIntegrationTest, ConnectThenRegister) {
  ControllerPtr controller;
  FUZZING_EXPECT_OK(Connect(&controller, zx::sec(1)));

  ASSERT_NO_FATAL_FAILURE(Register());
  RunUntilIdle();

  // Verify connected.
  Bridge<Options> bridge;
  controller->GetOptions(bridge.completer.bind());
  FUZZING_EXPECT_OK(bridge.consumer.promise_or(fpromise::error()));
  RunUntilIdle();

  FUZZING_EXPECT_OK(Disconnect());
  RunUntilIdle();
}

TEST_F(RegistryIntegrationTest, ConnectThenTimeout) {
  ControllerPtr controller;
  FUZZING_EXPECT_ERROR(Connect(&controller, zx::msec(1)), ZX_ERR_TIMED_OUT);
  RunUntilIdle();
}

}  // namespace fuzzing

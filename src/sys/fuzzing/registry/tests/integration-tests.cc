// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/process.h>
#include <zircon/status.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/lib/pkg_url/fuchsia_pkg_url.h"
#include "src/sys/fuzzing/common/component-context.h"
#include "src/sys/fuzzing/common/testing/async-test.h"
#include "src/sys/fuzzing/common/testing/process.h"

namespace fuzzing {
namespace {

using fuchsia::fuzzer::ControllerPtr;
using fuchsia::fuzzer::Options;
using fuchsia::fuzzer::Registrar;
using fuchsia::fuzzer::Registry;
using fuchsia::fuzzer::RegistryPtr;

// Test fixtures.

const char* kFuzzerUrl = "fuchsia-pkg://fuchsia.com/fuzz-manager-unittests#meta/fake.cm";

// This class maintains the component context and connection to the fuzz-registry.
class RegistryIntegrationTest : public AsyncTest {
 protected:
  void SetUp() override {
    AsyncTest::SetUp();
    context_ = ComponentContext::CreateWithExecutor(executor());
  }

  // Launch a fuzzer and give it a channel to register itself with the fuzz-registry.
  void Register() {
    // Connect a channel to the fuzz-registry.
    fidl::InterfaceHandle<Registrar> handle;
    ASSERT_EQ(context_->Connect(handle.NewRequest()), ZX_OK);
    std::vector<zx::channel> channels;
    channels.emplace_back(handle.TakeChannel());
    component::FuchsiaPkgUrl url;
    ASSERT_TRUE(url.Parse(kFuzzerUrl));
    ASSERT_EQ(StartProcess("fake_fuzzer_for_testing", url, std::move(channels), &process_), ZX_OK);
  }

  // Promises to connect the |controller| once a fuzzer is registered.
  ZxPromise<> Connect(ControllerPtr* controller, zx::duration timeout) {
    auto request = registry_.NewRequest(executor()->dispatcher());
    if (auto status = context_->Connect(std::move(request)); status != ZX_OK) {
      return fpromise::make_error_promise(status);
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
        .and_then(AwaitTermination(std::move(process_), executor()));
  }

  void TearDown() override {
    process_.kill();
    AsyncTest::TearDown();
  }

 private:
  std::unique_ptr<ComponentContext> context_;
  zx::process process_;
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

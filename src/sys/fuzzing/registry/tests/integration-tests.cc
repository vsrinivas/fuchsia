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

#include "src/sys/fuzzing/common/child-process.h"
#include "src/sys/fuzzing/common/testing/async-test.h"
#include "src/sys/fuzzing/common/testing/component-context.h"

namespace fuzzing {
namespace {

using fuchsia::fuzzer::ControllerPtr;
using fuchsia::fuzzer::FUZZ_MODE;
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
    context_ = ComponentContextForTest::Create(executor());
    process_ = std::make_unique<ChildProcess>(executor());
  }

  // Launch a fuzzer and give it a channel to register itself with the fuzz-registry.
  void Register() {
    process_->Reset();
    process_->AddArgs({"bin/fake_fuzzer_for_testing", kFuzzerUrl, FUZZ_MODE});

    // Connect a channel to the fuzz-registry.
    fidl::InterfaceHandle<Registrar> handle;
    ASSERT_EQ(context_->Connect(handle.NewRequest()), ZX_OK);
    process_->AddChannel(ComponentContextForTest::kRegistrarId, handle.TakeChannel());

    ASSERT_EQ(process_->Spawn(), ZX_OK);
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
        .and_then(process_->Wait())
        .and_then([](const int64_t& ignored) -> ZxResult<> { return fpromise::ok(); });
  }

  void TearDown() override {
    process_->Kill();
    AsyncTest::TearDown();
  }

 private:
  std::unique_ptr<ComponentContext> context_;
  std::unique_ptr<ChildProcess> process_;
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

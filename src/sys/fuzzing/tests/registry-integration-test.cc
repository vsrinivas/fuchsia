// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fdio/spawn.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/process.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/testing/async-test.h"
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
class RegistryIntegrationTest : public AsyncTest {
 protected:
  void SetUp() override {
    AsyncTest::SetUp();
    context_ = sys::ComponentContext::Create();
  }

  // Launch a fuzzer and give it a channel to register itself with the fuzz-registry.
  void Register() {
    // Connect a channel to the fuzz-registry.
    fidl::InterfaceHandle<Registrar> handle;
    auto status = context_->svc()->Connect(handle.NewRequest());
    ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);
    auto channel = handle.TakeChannel();

    // Spawn the new process with the startup channel.
    const char* argv[2] = {"/pkg/bin/component_fuzzing_test_fuzzer", nullptr};
    fdio_spawn_action_t actions[] = {
        {
            .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
            .h =
                {
                    .id = PA_HND(PA_USER0, 0),
                    .handle = channel.release(),
                },
        },
    };
    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
    status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, argv[0], argv, nullptr,
                            sizeof(actions) / sizeof(actions[0]), actions,
                            process_.reset_and_get_address(), err_msg);
    ASSERT_EQ(status, ZX_OK) << err_msg;
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
        .and_then([&, terminated =
                          ZxFuture<zx_packet_signal_t>()](Context& context) mutable -> ZxResult<> {
          if (!process_) {
            return fpromise::ok();
          }
          if (!terminated) {
            terminated = executor()->MakePromiseWaitHandle(zx::unowned_handle(process_.get()),
                                                           ZX_PROCESS_TERMINATED);
          }
          if (!terminated(context)) {
            return fpromise::pending();
          }
          if (terminated.is_error()) {
            return fpromise::error(terminated.error());
          }
          zx_info_process_t info;
          auto status = process_.get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr);
          if (status != ZX_OK) {
            return fpromise::error(status);
          }
          EXPECT_EQ(info.return_code, 0);
          return fpromise::ok();
        });
  }

  void TearDown() override {
    process_.kill();
    AsyncTest::TearDown();
  }

 private:
  std::unique_ptr<sys::ComponentContext> context_;
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

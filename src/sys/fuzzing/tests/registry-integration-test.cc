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
#include <thread>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/dispatcher.h"
#include "src/sys/fuzzing/common/sync-wait.h"
#include "src/sys/fuzzing/testing/runner.h"

namespace fuzzing {
namespace {

using fuchsia::fuzzer::ControllerPtr;
using fuchsia::fuzzer::Options;
using fuchsia::fuzzer::Registrar;
using fuchsia::fuzzer::Registry;
using fuchsia::fuzzer::RegistrySyncPtr;

// Test fixtures.

const char* kFuzzerUrl = "an arbitrary string";

// This class maintains the component context and connection to the fuzz-registry.
class RegistryIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override { context_ = sys::ComponentContext::Create(); }

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

  // Block until a fuzzer is registered and a controller connected to it.
  zx_status_t Connect(ControllerPtr* controller, zx::time deadline, zx_status_t* out) {
    auto status = context_->svc()->Connect(registry_.NewRequest());
    if (status != ZX_OK) {
      return status;
    }
    auto timeout = deadline - zx::clock::get_monotonic();
    return registry_->Connect(kFuzzerUrl, controller->NewRequest(dispatcher_.get()), timeout.get(),
                              out);
  }

  // Stop a fuzzer if running.
  void Disconnect() {
    zx_status_t inner;
    auto outer = registry_->Disconnect(kFuzzerUrl, &inner);
    ASSERT_EQ(outer, ZX_OK) << zx_status_get_string(outer);
    EXPECT_EQ(inner, ZX_OK) << zx_status_get_string(inner);
    Waiter waiter = [this](zx::time deadline) {
      return process_.wait_one(ZX_PROCESS_TERMINATED, deadline, nullptr);
    };
    outer = WaitFor("process to terminate", &waiter);
    EXPECT_EQ(outer, ZX_OK) << zx_status_get_string(outer);
    zx_info_process_t info;
    outer = process_.get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr);
    ASSERT_EQ(outer, ZX_OK) << zx_status_get_string(outer);
    EXPECT_EQ(info.return_code, 0);
    ShutdownDispatcher();
  }

  void ShutdownDispatcher() { dispatcher_.Shutdown(); }

  void TearDown() override { process_.kill(); }

 private:
  std::unique_ptr<sys::ComponentContext> context_;
  zx::process process_;
  RegistrySyncPtr registry_;
  Dispatcher dispatcher_;
};

}  // namespace

// Unit tests

TEST_F(RegistryIntegrationTest, RegisterThenConnect) {
  ASSERT_NO_FATAL_FAILURE(Register());

  ControllerPtr controller;
  zx_status_t inner;
  Waiter waiter = [&](zx::time deadline) { return Connect(&controller, deadline, &inner); };
  auto outer = WaitFor("connection after registering", &waiter);
  ASSERT_EQ(outer, ZX_OK) << zx_status_get_string(outer);
  EXPECT_EQ(inner, ZX_OK) << zx_status_get_string(inner);

  // Verify connected.
  SyncWait sync;
  controller->GetOptions([&](Options options) {
    EXPECT_NE(options.seed(), 0U);
    sync.Signal();
  });
  sync.WaitFor("controller to return options");

  Disconnect();
}

TEST_F(RegistryIntegrationTest, ConnectThenRegister) {
  ControllerPtr controller;
  zx_status_t inner, outer;
  std::thread t([&]() {
    Waiter waiter = [&](zx::time deadline) { return Connect(&controller, deadline, &inner); };
    outer = WaitFor("connection before registering", &waiter);
  });
  ASSERT_NO_FATAL_FAILURE(Register());
  t.join();
  ASSERT_EQ(outer, ZX_OK) << zx_status_get_string(outer);
  EXPECT_EQ(inner, ZX_OK) << zx_status_get_string(inner);

  // Verify connected.
  SyncWait sync;
  controller->GetOptions([&](Options options) {
    EXPECT_NE(options.seed(), 0U);
    sync.Signal();
  });
  sync.WaitFor("controller to return options");

  Disconnect();
}

TEST_F(RegistryIntegrationTest, ConnectThenTimeout) {
  ControllerPtr controller;
  zx_status_t inner;
  auto deadline = zx::deadline_after(zx::msec(1));
  auto outer = Connect(&controller, deadline, &inner);
  ASSERT_EQ(outer, ZX_OK) << zx_status_get_string(outer);
  EXPECT_EQ(inner, ZX_ERR_TIMED_OUT) << zx_status_get_string(inner);

  ShutdownDispatcher();
}

}  // namespace fuzzing

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/vmm_controller.h"

#include <lib/sys/cpp/testing/component_context_provider.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <src/lib/testing/loop_fixture/real_loop_fixture.h>

namespace vmm {
namespace {

using ::fuchsia::virtualization::GuestConfig;
using ::fuchsia::virtualization::GuestError;
using ::fuchsia::virtualization::GuestLifecycle_Create_Result;
using ::fuchsia::virtualization::GuestLifecycle_Run_Result;
using ::fuchsia::virtualization::GuestLifecycleSyncPtr;
using ::testing::_;
using ::testing::Return;
using ::testing::StrictMock;

class MockVmm : public Vmm {
 public:
  MOCK_METHOD(fit::result<GuestError>, Initialize,
              (GuestConfig, ::sys::ComponentContext*, async_dispatcher_t*), (override));
  MOCK_METHOD(fit::result<GuestError>, StartPrimaryVcpu,
              (fit::function<void(fit::result<GuestError>)>), (override));
  MOCK_METHOD(void, NotifyClientsShutdown, (zx_status_t), (override));
};

class VmmControllerTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    RealLoopFixture::SetUp();

    controller_loop_ = std::make_shared<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    controller_ =
        std::make_unique<VmmController>([this]() { controller_loop_->Shutdown(); },
                                        provider_.TakeContext(), controller_loop_->dispatcher());

    RunLoopUntilIdle();
  }

  sys::testing::ComponentContextProvider provider_;

  std::shared_ptr<async::Loop> controller_loop_;
  std::unique_ptr<VmmController> controller_;
};

TEST_F(VmmControllerTest, GuestLifecycleChannelDisconnected) {
  GuestLifecycleSyncPtr lifecycle;
  provider_.ConnectToPublicService(lifecycle.NewRequest());

  ASSERT_TRUE(lifecycle.is_bound());
  ASSERT_EQ(controller_loop_->GetState(), ASYNC_LOOP_RUNNABLE);

  lifecycle.Unbind();
  RunLoopUntilIdle();
  controller_loop_->RunUntilIdle();

  // The lifecycle channel being closed (such as if the guest manager goes away) should also
  // result in the VMM controller's dispatch loop entering a shutdown state.
  ASSERT_FALSE(lifecycle.is_bound());
  ASSERT_EQ(controller_loop_->GetState(), ASYNC_LOOP_SHUTDOWN);
}

TEST_F(VmmControllerTest, RecreatingNonRunningGuestDestroysVmm) {
  auto vmm = std::make_unique<StrictMock<MockVmm>>();
  controller_->ProvideVmmForTesting(std::move(vmm));

  GuestConfig cfg;  // Invalid config.
  bool create_callback_called = false;
  controller_->Create(std::move(cfg),
                      [&create_callback_called](GuestLifecycle_Create_Result result) {
                        ASSERT_TRUE(result.is_err());
                        ASSERT_EQ(result.err(), GuestError::BAD_CONFIG);
                        create_callback_called = true;
                      });

  ASSERT_TRUE(create_callback_called);

  // The first VMM was destroyed upon creating the second one, and the second one encountered an
  // error during initialization, so there's no VMM to start.
  bool run_callback_called = false;
  controller_->Run([&run_callback_called](GuestLifecycle_Run_Result result) {
    ASSERT_TRUE(result.is_err());
    ASSERT_EQ(result.err(), GuestError::NOT_CREATED);
    run_callback_called = true;
  });

  ASSERT_TRUE(run_callback_called);
}

TEST_F(VmmControllerTest, RecreatingRunningGuestRequiresStop) {
  auto vmm = std::make_unique<StrictMock<MockVmm>>();
  EXPECT_CALL(*vmm, StartPrimaryVcpu(_)).WillOnce(Return(fit::ok()));
  controller_->ProvideVmmForTesting(std::move(vmm));

  bool run_callback_called = false;
  controller_->Run(
      [&run_callback_called](GuestLifecycle_Run_Result result) { run_callback_called = true; });

  // Callback is saved as the guest is running.
  ASSERT_FALSE(run_callback_called);

  GuestConfig cfg;
  bool create_callback_called = false;
  controller_->Create(std::move(cfg),
                      [&create_callback_called](GuestLifecycle_Create_Result result) {
                        ASSERT_TRUE(result.is_err());
                        ASSERT_EQ(result.err(), GuestError::ALREADY_RUNNING);
                        create_callback_called = true;
                      });

  ASSERT_TRUE(create_callback_called);

  // Guest is still running (the controller must call Stop before creating a new VMM).
  ASSERT_FALSE(run_callback_called);
}

TEST_F(VmmControllerTest, GuestLifecycleCreateNotCalled) {
  bool run_callback_called = false;
  controller_->Run([&run_callback_called](GuestLifecycle_Run_Result result) {
    ASSERT_TRUE(result.is_err());
    ASSERT_EQ(result.err(), GuestError::NOT_CREATED);
    run_callback_called = true;
  });

  ASSERT_TRUE(run_callback_called);
}

TEST_F(VmmControllerTest, GuestLifecycleAlreadyRunning) {
  auto vmm = std::make_unique<StrictMock<MockVmm>>();
  EXPECT_CALL(*vmm, StartPrimaryVcpu(_)).WillOnce(Return(fit::ok()));
  controller_->ProvideVmmForTesting(std::move(vmm));

  bool run_callback1_called = false;
  controller_->Run(
      [&run_callback1_called](GuestLifecycle_Run_Result result) { run_callback1_called = true; });

  // First callback is saved for when the guest exits.
  ASSERT_FALSE(run_callback1_called);

  bool run_callback2_called = false;
  controller_->Run([&run_callback2_called](GuestLifecycle_Run_Result result) {
    ASSERT_TRUE(result.is_err());
    ASSERT_EQ(result.err(), GuestError::ALREADY_RUNNING);
    run_callback2_called = true;
  });

  // Second callback is immediately used to report an error.
  ASSERT_TRUE(run_callback2_called);
}

TEST_F(VmmControllerTest, RunFailureDoesntSetCallbackAndDestroysVmm) {
  auto vmm = std::make_unique<StrictMock<MockVmm>>();
  EXPECT_CALL(*vmm, StartPrimaryVcpu(_))
      .WillOnce(Return(fit::error(GuestError::VCPU_START_FAILURE)));
  controller_->ProvideVmmForTesting(std::move(vmm));

  bool run_callback1_called = false;
  controller_->Run([&run_callback1_called](GuestLifecycle_Run_Result result) {
    ASSERT_TRUE(result.is_err());
    ASSERT_EQ(result.err(), GuestError::VCPU_START_FAILURE);
    run_callback1_called = true;
  });

  // Callback is used to report a VCPU start error.
  ASSERT_TRUE(run_callback1_called);

  bool run_callback2_called = false;
  controller_->Run([&run_callback2_called](GuestLifecycle_Run_Result result) {
    ASSERT_TRUE(result.is_err());
    ASSERT_EQ(result.err(), GuestError::NOT_CREATED);
    run_callback2_called = true;
  });

  // The VM was destroyed after failing to start, so calling run again returns a not
  // created failure.
  ASSERT_TRUE(run_callback2_called);
}

TEST_F(VmmControllerTest, StopWithoutGuestCreation) {
  bool stop_callback_called = false;
  controller_->Stop([&stop_callback_called]() { stop_callback_called = true; });

  // It's fine to call Stop without a created VMM (such as if there was an error starting).
  ASSERT_TRUE(stop_callback_called);
}

TEST_F(VmmControllerTest, StopWithoutGuestRunning) {
  auto vmm = std::make_unique<StrictMock<MockVmm>>();
  controller_->ProvideVmmForTesting(std::move(vmm));

  bool stop_callback_called = false;
  controller_->Stop([&stop_callback_called]() { stop_callback_called = true; });

  // It's fine to call Stop without starting a VMM to just destroy it.
  ASSERT_TRUE(stop_callback_called);
}

TEST_F(VmmControllerTest, ForcedShutdownReturnsError) {
  auto vmm = std::make_unique<StrictMock<MockVmm>>();
  EXPECT_CALL(*vmm, StartPrimaryVcpu(_)).WillOnce(Return(fit::ok()));
  EXPECT_CALL(*vmm, NotifyClientsShutdown(ZX_ERR_INTERNAL));
  controller_->ProvideVmmForTesting(std::move(vmm));

  bool run_callback_called = false;
  GuestLifecycle_Run_Result run_result;
  controller_->Run([&run_callback_called, &run_result](GuestLifecycle_Run_Result result) {
    run_result = std::move(result);
    run_callback_called = true;
  });

  // The controller saves the callback for when the guest exits.
  ASSERT_FALSE(run_callback_called);

  bool stop_callback_called = false;
  controller_->Stop([&stop_callback_called]() { stop_callback_called = true; });

  // Stop posts a task to the dispatch loop.
  controller_loop_->RunUntilIdle();

  ASSERT_TRUE(stop_callback_called);
  ASSERT_TRUE(run_callback_called);
  ASSERT_TRUE(run_result.is_err());
  ASSERT_EQ(run_result.err(), GuestError::CONTROLLER_FORCED_HALT);
}

TEST_F(VmmControllerTest, CleanGuestInitiatedShutdownReturnsSuccess) {
  fit::function<void(fit::result<GuestError>)> captured_callback;
  auto vmm = std::make_unique<StrictMock<MockVmm>>();
  EXPECT_CALL(*vmm, StartPrimaryVcpu(_))
      .WillOnce([&captured_callback](fit::function<void(fit::result<GuestError>)> stop_callback) {
        captured_callback = std::move(stop_callback);
        return fit::ok();
      });
  EXPECT_CALL(*vmm, NotifyClientsShutdown(ZX_OK));
  controller_->ProvideVmmForTesting(std::move(vmm));

  bool run_callback1_called = false;
  GuestLifecycle_Run_Result run_result;
  controller_->Run([&run_callback1_called, &run_result](GuestLifecycle_Run_Result result) {
    run_result = std::move(result);
    run_callback1_called = true;
  });

  // The controller saves the callback for when the guest exits.
  ASSERT_FALSE(run_callback1_called);

  // This posts a task to the dispatch loop, so the guest is still running.
  captured_callback(fit::ok());
  ASSERT_FALSE(run_callback1_called);

  controller_loop_->RunUntilIdle();

  // The guest has stopped and the VCPU has reported a value.
  ASSERT_TRUE(run_callback1_called);
  ASSERT_TRUE(run_result.is_response());

  bool run_callback2_called = false;
  controller_->Run([&run_callback2_called](GuestLifecycle_Run_Result result) {
    ASSERT_TRUE(result.is_err());
    ASSERT_EQ(result.err(), GuestError::NOT_CREATED);
    run_callback2_called = true;
  });

  // Attempt to run again confirms that the VMM was destroyed.
  ASSERT_TRUE(run_callback2_called);
}

}  // namespace
}  // namespace vmm

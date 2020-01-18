// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/logging.h>

#include "garnet/bin/trace_manager/tests/trace_manager_test.h"

namespace tracing {
namespace test {

using SessionState = TraceManagerTest::SessionState;

// This only works when no other condition could cause the loop to exit.
// E.g., This doesn't work if the state is kStopping or kTerminating as the
// transition to kStopped,kTerminated will also cause the loop to exit.
template <typename T>
controller::Controller_StartTracing_Result TryStart(TraceManagerTest* fixture,
                                                    const T& interface_ptr) {
  controller::Controller_StartTracing_Result start_result;
  controller::StartOptions start_options{fixture->GetDefaultStartOptions()};
  bool start_completed = false;
  interface_ptr->StartTracing(std::move(start_options),
                              [fixture, &start_completed,
                               &start_result](controller::Controller_StartTracing_Result result) {
                                start_completed = true;
                                start_result = std::move(result);
                                fixture->QuitLoop();
                              });
  fixture->RunLoopUntilIdle();
  FXL_VLOG(2) << "Start loop done";
  EXPECT_TRUE(start_completed);
  return start_result;
}

TEST_F(TraceManagerTest, StartUninitialized) {
  ConnectToControllerService();

  controller::Controller_StartTracing_Result start_result{TryStart(this, controller())};
  EXPECT_TRUE(start_result.is_err());
  EXPECT_EQ(start_result.err(), controller::StartErrorCode::NOT_INITIALIZED);
}

template <typename T>
void TryExtraStart(TraceManagerTest* fixture, const T& interface_ptr) {
  controller::Controller_StartTracing_Result start_result{TryStart(fixture, interface_ptr)};
  EXPECT_EQ(fixture->GetSessionState(), SessionState::kStarted);
  EXPECT_TRUE(start_result.is_err());
  EXPECT_EQ(start_result.err(), controller::StartErrorCode::ALREADY_STARTED);
}

TEST_F(TraceManagerTest, ExtraStart) {
  ConnectToControllerService();

  EXPECT_TRUE(AddFakeProvider(kProvider1Pid, kProvider1Name));

  ASSERT_TRUE(InitializeSession());

  ASSERT_TRUE(StartSession());

  // Now try starting again.
  TryExtraStart(this, controller());
}

TEST_F(TraceManagerTest, StartWhileStopping) {
  ConnectToControllerService();

  EXPECT_TRUE(AddFakeProvider(kProvider1Pid, kProvider1Name));

  ASSERT_TRUE(InitializeSession());

  ASSERT_TRUE(StartSession());

  controller::StopOptions stop_options{GetDefaultStopOptions()};
  controller()->StopTracing(std::move(stop_options), []() {});
  RunLoopUntilIdle();
  // The loop will exit for the transition to kStopping.
  FXL_VLOG(2) << "Loop done, expecting session stopping";
  ASSERT_EQ(GetSessionState(), SessionState::kStopping);

  // Now try a Start while we're still in |kStopping|.
  // The provider doesn't advance state until we tell it to, so we should
  // still remain in |kStopping|.
  controller::Controller_StartTracing_Result result;
  controller::StartOptions start_options{GetDefaultStartOptions()};
  bool start_completed = false;
  controller()->StartTracing(
      std::move(start_options),
      [this, &start_completed, &result](controller::Controller_StartTracing_Result in_result) {
        start_completed = true;
        result = std::move(in_result);
        QuitLoop();
      });
  RunLoopUntilIdle();
  FXL_VLOG(2) << "Start loop done";
  EXPECT_TRUE(GetSessionState() == SessionState::kStopping);
  ASSERT_TRUE(start_completed);
  ASSERT_TRUE(result.is_err());
  EXPECT_EQ(result.err(), controller::StartErrorCode::STOPPING);
}

TEST_F(TraceManagerTest, StartWhileTerminating) {
  ConnectToControllerService();

  EXPECT_TRUE(AddFakeProvider(kProvider1Pid, kProvider1Name));

  ASSERT_TRUE(InitializeSession());

  ASSERT_TRUE(StartSession());

  ASSERT_TRUE(StopSession());

  controller::TerminateOptions options{GetDefaultTerminateOptions()};
  controller()->TerminateTracing(std::move(options), [](controller::TerminateResult result) {});
  RunLoopUntilIdle();
  ASSERT_EQ(GetSessionState(), SessionState::kTerminating);

  // Now try a Start while we're still in |kTerminating|.
  // The provider doesn't advance state until we tell it to, so we should
  // still remain in |kTerminating|.
  controller::Controller_StartTracing_Result result;
  controller::StartOptions start_options{GetDefaultStartOptions()};
  bool start_completed = false;
  controller()->StartTracing(
      std::move(start_options),
      [this, &start_completed, &result](controller::Controller_StartTracing_Result in_result) {
        start_completed = true;
        result = std::move(in_result);
        QuitLoop();
      });
  RunLoopUntilIdle();
  FXL_VLOG(2) << "Start loop done";
  EXPECT_TRUE(GetSessionState() == SessionState::kTerminating);
  ASSERT_TRUE(start_completed);
  ASSERT_TRUE(result.is_err());
  EXPECT_EQ(result.err(), controller::StartErrorCode::TERMINATING);
}

}  // namespace test
}  // namespace tracing

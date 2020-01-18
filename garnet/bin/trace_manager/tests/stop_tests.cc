// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/logging.h>

#include "garnet/bin/trace_manager/tests/trace_manager_test.h"

namespace tracing {
namespace test {

using SessionState = TraceManagerTest::SessionState;

TEST_F(TraceManagerTest, StopUninitialize) {
  // There's no error result. Mostly we want to verify we don't crash/hang.
  ConnectToControllerService();

  controller::StopOptions stop_options{GetDefaultStopOptions()};
  bool stop_completed = false;
  controller()->StopTracing(std::move(stop_options), [this, &stop_completed]() {
    stop_completed = true;
    QuitLoop();
  });
  RunLoopUntilIdle();
  FXL_VLOG(2) << "Loop done";
  EXPECT_TRUE(stop_completed);
}

template <typename T>
void TryExtraStop(TraceManagerTest* fixture, const T& interface_ptr) {
  controller::StopOptions stop_options{fixture->GetDefaultStopOptions()};
  bool stop_completed = false;
  interface_ptr->StopTracing(std::move(stop_options), [fixture, &stop_completed]() {
    stop_completed = true;
    fixture->QuitLoop();
  });
  fixture->RunLoopUntilIdle();
  FXL_VLOG(2) << "Loop done, expecting session still stopped";
  EXPECT_TRUE(stop_completed);
  EXPECT_EQ(fixture->GetSessionState(), SessionState::kStopped);
}

TEST_F(TraceManagerTest, ExtraStop) {
  ConnectToControllerService();

  EXPECT_TRUE(AddFakeProvider(kProvider1Pid, kProvider1Name));

  ASSERT_TRUE(InitializeSession());

  ASSERT_TRUE(StartSession());

  ASSERT_TRUE(StopSession());

  // Now try stopping again.
  // There's no error result. Mostly we want to verify we don't crash/hang.
  TryExtraStop(this, controller());
}

TEST_F(TraceManagerTest, StopWhileStopping) {
  ConnectToControllerService();

  FakeProvider* provider;
  EXPECT_TRUE(AddFakeProvider(kProvider1Pid, kProvider1Name, &provider));

  ASSERT_TRUE(InitializeSession());

  ASSERT_TRUE(StartSession());

  controller::StopOptions stop1_options{GetDefaultStopOptions()};
  controller()->StopTracing(std::move(stop1_options), []() {});
  RunLoopUntilIdle();
  // The loop will exit for the transition to kStopping.
  FXL_VLOG(2) << "Loop done, expecting session stopping";
  EXPECT_EQ(GetSessionState(), SessionState::kStopping);

  // Now try another Stop while we're still in |kStopping|.
  // The provider doesn't advance state until we tell it to, so we should
  // still remain in |kStopping|.
  controller::StopOptions stop2_options{GetDefaultStopOptions()};
  bool stop_completed = false;
  controller()->StopTracing(std::move(stop2_options), [this, &stop_completed]() {
    stop_completed = true;
    QuitLoop();
  });
  RunLoopUntilIdle();
  FXL_VLOG(2) << "Stop loop done";
  EXPECT_TRUE(stop_completed);
  EXPECT_TRUE(GetSessionState() == SessionState::kStopping);
}

TEST_F(TraceManagerTest, StopWhileTerminating) {
  ConnectToControllerService();

  EXPECT_TRUE(AddFakeProvider(kProvider1Pid, kProvider1Name));

  ASSERT_TRUE(InitializeSession());

  ASSERT_TRUE(StartSession());

  ASSERT_TRUE(StopSession());

  controller::TerminateOptions options{GetDefaultTerminateOptions()};
  controller()->TerminateTracing(std::move(options), [](controller::TerminateResult result) {});
  RunLoopUntilIdle();
  ASSERT_EQ(GetSessionState(), SessionState::kTerminating);

  // Now try a Stop while we're still in |kTerminating|.
  // The provider doesn't advance state until we tell it to, so we should
  // still remain in |kTerminating|.
  controller::StopOptions stop_options{GetDefaultStopOptions()};
  bool stop_completed = false;
  controller()->StopTracing(std::move(stop_options), [this, &stop_completed]() {
    stop_completed = true;
    QuitLoop();
  });
  RunLoopUntilIdle();
  FXL_VLOG(2) << "Stop loop done";
  EXPECT_TRUE(stop_completed);
  EXPECT_TRUE(GetSessionState() == SessionState::kTerminating);
}

}  // namespace test
}  // namespace tracing

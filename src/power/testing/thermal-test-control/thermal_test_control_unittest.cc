// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thermal_test_control.h"

#include <fuchsia/thermal/cpp/fidl.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <test/thermal/cpp/fidl.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

class ThermalTestControlTest : public gtest::TestLoopFixture {
 protected:
  void SetUp() override {
    TestLoopFixture::SetUp();

    thermal_test_control_ = std::make_unique<ThermalTestControl>(context_provider_.TakeContext());

    context_provider_.ConnectToPublicService(connector_.NewRequest());
    context_provider_.ConnectToPublicService(client_state_control_.NewRequest());
  }

  void ConnectClient() {
    connector_->Connect(kTestClientType, watcher_.NewRequest());
    RunLoopUntilIdle();
  }

  void SetThermalState(uint64_t state) {
    bool updated = false;
    client_state_control_->SetThermalState(kTestClientType, state,
                                           [&updated]() { updated = true; });
    RunLoopUntilIdle();
    ASSERT_EQ(updated, true);
  }

 protected:
  static constexpr char kTestClientType[] = "test";

  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<ThermalTestControl> thermal_test_control_;
  fuchsia::thermal::ClientStateConnectorPtr connector_;
  fuchsia::thermal::ClientStateWatcherPtr watcher_;
  test::thermal::ClientStateControlPtr client_state_control_;
};

TEST_F(ThermalTestControlTest, PendingRequestCompletesForStateChange) {
  std::optional<uint64_t> state_result;

  ConnectClient();

  // Initial state should be 0
  state_result.reset();
  watcher_->Watch([&](uint64_t thermal_state) { state_result = thermal_state; });
  RunLoopUntilIdle();
  ASSERT_EQ(state_result, 0);

  // Another call to `Watch` should not complete since thermal state hasn't changed
  state_result.reset();
  watcher_->Watch([&](uint64_t thermal_state) { state_result = thermal_state; });
  RunLoopUntilIdle();
  ASSERT_EQ(state_result, std::nullopt);

  // Change thermal state to 1 and verify the `Watch` request completes successfully
  SetThermalState(1);
  RunLoopUntilIdle();
  ASSERT_EQ(state_result, 1);
}

TEST_F(ThermalTestControlTest, NewRequestAfterStateChange) {
  std::optional<uint64_t> state_result;

  ConnectClient();

  // Initial state should be 0
  state_result.reset();
  watcher_->Watch([&](uint64_t thermal_state) { state_result = thermal_state; });
  RunLoopUntilIdle();
  ASSERT_EQ(state_result, 0);

  // Another call to `Watch` should not complete since thermal state hasn't changed
  state_result.reset();
  watcher_->Watch([&](uint64_t thermal_state) { state_result = thermal_state; });
  RunLoopUntilIdle();
  ASSERT_EQ(state_result, std::nullopt);

  // Change thermal state to 1 and verify the `Watch` request completes successfully
  state_result.reset();
  SetThermalState(1);
  RunLoopUntilIdle();
  ASSERT_EQ(state_result, 1);

  // Change the state again, but without a pending request nothing should happen
  state_result.reset();
  SetThermalState(2);
  RunLoopUntilIdle();
  ASSERT_EQ(state_result, std::nullopt);

  // Send a new `Watch` request which should complete immediately with the latest state
  watcher_->Watch([&](uint64_t thermal_state) { state_result = thermal_state; });
  RunLoopUntilIdle();
  ASSERT_EQ(state_result, 2);
}

TEST_F(ThermalTestControlTest, IsClientConnected) {
  // Client is initially not connected
  std::optional<bool> is_connected = std::nullopt;
  client_state_control_->IsClientTypeConnected(kTestClientType,
                                               [&](bool connected) { is_connected = connected; });
  RunLoopUntilIdle();
  ASSERT_EQ(is_connected, false);

  // Connect the client
  ConnectClient();

  // Verify `IsClientTypeConnected` now reports true
  is_connected.reset();
  client_state_control_->IsClientTypeConnected(kTestClientType,
                                               [&](bool connected) { is_connected = connected; });
  RunLoopUntilIdle();
  ASSERT_EQ(is_connected, true);

  // Disconnect the client
  watcher_.Unbind();

  // Verify `IsClientTypeConnected` now reports false
  is_connected.reset();
  client_state_control_->IsClientTypeConnected(kTestClientType,
                                               [&](bool connected) { is_connected = connected; });
  RunLoopUntilIdle();
  ASSERT_EQ(is_connected, false);
}

TEST_F(ThermalTestControlTest, NoStateChange) {
  std::optional<uint64_t> state_result;

  ConnectClient();

  // Initial state should be 0
  state_result.reset();
  watcher_->Watch([&](uint64_t thermal_state) { state_result = thermal_state; });
  RunLoopUntilIdle();
  ASSERT_EQ(state_result, 0);

  // Another call to `Watch` should not complete since thermal state hasn't changed
  state_result.reset();
  watcher_->Watch([&](uint64_t thermal_state) { state_result = thermal_state; });
  RunLoopUntilIdle();
  ASSERT_EQ(state_result, std::nullopt);

  // No change to thermal state, then verify `Watch` still has no response
  SetThermalState(0);
  RunLoopUntilIdle();
  ASSERT_EQ(state_result, std::nullopt);
}

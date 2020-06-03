// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/device/manager/c/fidl.h>
#include <fuchsia/device/manager/llcpp/fidl.h>
#include <fuchsia/device/power/test/llcpp/fidl.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <zxtest/zxtest.h>

using driver_integration_test::IsolatedDevmgr;
using llcpp::fuchsia::device::Controller;
using llcpp::fuchsia::device::DEVICE_PERFORMANCE_STATE_P0;
using llcpp::fuchsia::device::DevicePerformanceStateInfo;
using llcpp::fuchsia::device::DevicePowerState;
using llcpp::fuchsia::device::DevicePowerStateInfo;
using llcpp::fuchsia::device::MAX_DEVICE_PERFORMANCE_STATES;
using llcpp::fuchsia::device::MAX_DEVICE_POWER_STATES;
using llcpp::fuchsia::device::SystemPowerStateInfo;
using llcpp::fuchsia::device::manager::Administrator;
using llcpp::fuchsia::device::manager::MAX_SYSTEM_POWER_STATES;
using llcpp::fuchsia::device::manager::SystemPowerState;
using llcpp::fuchsia::device::power::test::TestDevice;

class PowerTestCase : public zxtest::Test {
 public:
  ~PowerTestCase() override = default;
  void SetUp() override {
    IsolatedDevmgr::Args args;
    args.load_drivers.push_back("/boot/driver/ddk-power-test.so");
    args.load_drivers.push_back("/boot/driver/ddk-power-test-child.so");

    board_test::DeviceEntry dev = {};
    dev.vid = PDEV_VID_TEST;
    dev.pid = PDEV_PID_POWER_TEST;
    dev.did = 0;
    args.device_list.push_back(dev);

    zx_status_t status = IsolatedDevmgr::Create(&args, &devmgr);
    ASSERT_OK(status);
    fbl::unique_fd parent_fd, child_fd;
    ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
        devmgr.devfs_root(), "sys/platform/11:0b:0/power-test", &parent_fd));
    ASSERT_GT(parent_fd.get(), 0);
    ASSERT_OK(
        fdio_get_service_handle(parent_fd.release(), parent_device_handle.reset_and_get_address()));
    ASSERT_NE(parent_device_handle.get(), ZX_HANDLE_INVALID);

    ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
        devmgr.devfs_root(), "sys/platform/11:0b:0/power-test/power-test-child", &child_fd));
    ASSERT_GT(child_fd.get(), 0);

    ASSERT_OK(
        fdio_get_service_handle(child_fd.release(), child_device_handle.reset_and_get_address()));
    ASSERT_NE(child_device_handle.get(), ZX_HANDLE_INVALID);
  }
  void AddChildWithPowerArgs(DevicePowerStateInfo *states, uint8_t sleep_state_count,
                             DevicePerformanceStateInfo *perf_states, uint8_t perf_state_count,
                             bool add_invisible = false) {
    auto power_states = ::fidl::VectorView(fidl::unowned_ptr(states), sleep_state_count);
    auto perf_power_states = ::fidl::VectorView(fidl::unowned_ptr(perf_states), perf_state_count);
    auto response = TestDevice::Call::AddDeviceWithPowerArgs(
        zx::unowned(child_device_handle), std::move(power_states), std::move(perf_power_states),
        add_invisible);
    ASSERT_OK(response.status());
    zx_status_t call_status = ZX_OK;
    if (response->result.is_err()) {
      call_status = response->result.err();
    }
    ASSERT_OK(call_status);

    fbl::unique_fd child2_fd;
    ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(
        devmgr.devfs_root(), "sys/platform/11:0b:0/power-test/power-test-child-2", &child2_fd));
    ASSERT_GT(child2_fd.get(), 0);
    ASSERT_OK(
        fdio_get_service_handle(child2_fd.release(), child2_device_handle.reset_and_get_address()));
    ASSERT_NE(child2_device_handle.get(), ZX_HANDLE_INVALID);
  }
  zx::channel child_device_handle;
  zx::channel parent_device_handle;
  zx::channel child2_device_handle;
  IsolatedDevmgr devmgr;
};

TEST_F(PowerTestCase, InvalidDevicePowerCaps_Less) {
  fidl::Array<DevicePowerStateInfo, 1> states;
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D1;
  states[0].is_supported = true;
  auto response = TestDevice::Call::AddDeviceWithPowerArgs(
      zx::unowned(child_device_handle), fidl::unowned_vec(states),
      ::fidl::VectorView<DevicePerformanceStateInfo>(), false);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }
  ASSERT_STATUS(call_status, ZX_ERR_INVALID_ARGS);
}

TEST_F(PowerTestCase, InvalidDevicePowerCaps_More) {
  fidl::Array<DevicePowerStateInfo, MAX_DEVICE_POWER_STATES + 1> states;
  for (uint8_t i = 0; i < MAX_DEVICE_POWER_STATES + 1; i++) {
    states[i].state_id = DevicePowerState::DEVICE_POWER_STATE_D1;
    states[i].is_supported = true;
  }
  auto response = TestDevice::Call::AddDeviceWithPowerArgs(
      zx::unowned(child_device_handle), fidl::unowned_vec(states),
      ::fidl::VectorView<DevicePerformanceStateInfo>(), false);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }

  ASSERT_STATUS(call_status, ZX_ERR_INVALID_ARGS);
}

TEST_F(PowerTestCase, InvalidDevicePowerCaps_MissingRequired) {
  fidl::Array<DevicePowerStateInfo, MAX_DEVICE_POWER_STATES> states;
  for (uint8_t i = 0; i < MAX_DEVICE_POWER_STATES; i++) {
    // Missing D0 and D3COLD
    states[i].state_id = DevicePowerState::DEVICE_POWER_STATE_D1;
    states[i].is_supported = true;
  }
  auto response = TestDevice::Call::AddDeviceWithPowerArgs(
      zx::unowned(child_device_handle), fidl::unowned_vec(states),
      ::fidl::VectorView<DevicePerformanceStateInfo>(), false);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }

  ASSERT_STATUS(call_status, ZX_ERR_INVALID_ARGS);
}

TEST_F(PowerTestCase, InvalidDevicePowerCaps_DuplicateCaps) {
  fidl::Array<DevicePowerStateInfo, MAX_DEVICE_POWER_STATES> states;
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[1].is_supported = true;
  // Repeat
  states[2].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[2].is_supported = true;
  auto response = TestDevice::Call::AddDeviceWithPowerArgs(
      zx::unowned(child_device_handle), fidl::unowned_vec(states),
      ::fidl::VectorView<DevicePerformanceStateInfo>(), false);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }

  ASSERT_STATUS(call_status, ZX_ERR_INVALID_ARGS);
}

TEST_F(PowerTestCase, AddDevicePowerCaps_Success) {
  fidl::Array<DevicePowerStateInfo, 2> states;
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[1].is_supported = true;
  auto response = TestDevice::Call::AddDeviceWithPowerArgs(
      zx::unowned(child_device_handle), fidl::unowned_vec(states),
      ::fidl::VectorView<DevicePerformanceStateInfo>(), false);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }

  ASSERT_STATUS(call_status, ZX_OK);
}

TEST_F(PowerTestCase, AddDevicePowerCaps_MakeVisible_Success) {
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;

  DevicePerformanceStateInfo perf_states[3];
  perf_states[0].state_id = DEVICE_PERFORMANCE_STATE_P0;
  perf_states[0].is_supported = true;
  perf_states[0].restore_latency = 0;
  perf_states[1].state_id = 1;
  perf_states[1].is_supported = true;
  perf_states[1].restore_latency = 100;
  perf_states[2].state_id = 2;
  perf_states[2].is_supported = true;
  perf_states[2].restore_latency = 1000;

  AddChildWithPowerArgs(states, std::size(states), perf_states, std::size(perf_states), true);

  const DevicePowerStateInfo *out_dpstates;
  auto response2 = Controller::Call::GetDevicePowerCaps(zx::unowned(child2_device_handle));
  ASSERT_OK(response2.status());
  zx_status_t call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_OK(call_status);
  out_dpstates = &response2->result.response().dpstates[0];

  ASSERT_TRUE(
      out_dpstates[static_cast<uint8_t>(DevicePowerState::DEVICE_POWER_STATE_D0)].is_supported);
  ASSERT_TRUE(
      out_dpstates[static_cast<uint8_t>(DevicePowerState::DEVICE_POWER_STATE_D1)].is_supported);
  ASSERT_EQ(
      out_dpstates[static_cast<uint8_t>(DevicePowerState::DEVICE_POWER_STATE_D1)].restore_latency,
      100);
  ASSERT_TRUE(
      out_dpstates[static_cast<uint8_t>(DevicePowerState::DEVICE_POWER_STATE_D3COLD)].is_supported);

  ASSERT_EQ(out_dpstates[static_cast<uint8_t>(DevicePowerState::DEVICE_POWER_STATE_D3COLD)]
                .restore_latency,
            1000);
  const DevicePerformanceStateInfo *out_perf_states;
  auto response = Controller::Call::GetDevicePerformanceStates(zx::unowned(child2_device_handle));
  ASSERT_OK(response.status());
  out_perf_states = &response->states[0];

  ASSERT_TRUE(out_perf_states[DEVICE_PERFORMANCE_STATE_P0].is_supported);
  ASSERT_TRUE(out_perf_states[1].is_supported);
  ASSERT_EQ(out_perf_states[1].restore_latency, 100);
  ASSERT_TRUE(out_perf_states[2].is_supported);
  ASSERT_EQ(out_perf_states[2].restore_latency, 1000);
}

TEST_F(PowerTestCase, InvalidDevicePerformanceCaps_MissingRequired) {
  fidl::Array<DevicePowerStateInfo, 2> states;
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[1].is_supported = true;

  fidl::Array<DevicePerformanceStateInfo, 10> perf_states;
  perf_states[0].state_id = 1;
  perf_states[0].is_supported = true;
  perf_states[1].state_id = 2;
  perf_states[1].is_supported = true;

  auto response = TestDevice::Call::AddDeviceWithPowerArgs(zx::unowned(child_device_handle),
                                                           fidl::unowned_vec(states),
                                                           fidl::unowned_vec(perf_states), false);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }

  ASSERT_STATUS(call_status, ZX_ERR_INVALID_ARGS);
}

TEST_F(PowerTestCase, InvalidDevicePerformanceCaps_Duplicate) {
  fidl::Array<DevicePowerStateInfo, 2> states;
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[1].is_supported = true;

  fidl::Array<DevicePerformanceStateInfo, 10> perf_states;
  perf_states[0].state_id = DEVICE_PERFORMANCE_STATE_P0;
  perf_states[0].is_supported = true;
  perf_states[1].state_id = DEVICE_PERFORMANCE_STATE_P0;
  perf_states[1].is_supported = true;
  perf_states[2].state_id = 1;
  perf_states[2].is_supported = true;

  auto response = TestDevice::Call::AddDeviceWithPowerArgs(zx::unowned(child_device_handle),
                                                           fidl::unowned_vec(states),
                                                           fidl::unowned_vec(perf_states), false);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }

  ASSERT_STATUS(call_status, ZX_ERR_INVALID_ARGS);
}

TEST_F(PowerTestCase, InvalidDevicePerformanceCaps_More) {
  fidl::Array<DevicePowerStateInfo, 2> states;
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[1].is_supported = true;

  fidl::Array<DevicePerformanceStateInfo, MAX_DEVICE_PERFORMANCE_STATES + 1> perf_states;
  for (size_t i = 0; i < (MAX_DEVICE_PERFORMANCE_STATES + 1); i++) {
    perf_states[i].state_id = static_cast<int32_t>(i);
    perf_states[i].is_supported = true;
  }
  auto response = TestDevice::Call::AddDeviceWithPowerArgs(zx::unowned(child_device_handle),
                                                           fidl::unowned_vec(states),
                                                           fidl::unowned_vec(perf_states), false);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }

  ASSERT_STATUS(call_status, ZX_ERR_INVALID_ARGS);
}

TEST_F(PowerTestCase, AddDevicePerformanceCaps_NoCaps) {
  fidl::Array<DevicePowerStateInfo, 2> states;
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[1].is_supported = true;
  auto power_states = ::fidl::unowned_vec(states);

  // This is the default case. By default, the devhost fills in the fully performance state.
  auto response = TestDevice::Call::AddDeviceWithPowerArgs(
      zx::unowned(child_device_handle), std::move(power_states),
      ::fidl::VectorView<DevicePerformanceStateInfo>(), false);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }

  ASSERT_STATUS(call_status, ZX_OK);
}

TEST_F(PowerTestCase, AddDevicePerformanceCaps_Success) {
  fidl::Array<DevicePowerStateInfo, 2> states;
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[1].is_supported = true;
  auto power_states = ::fidl::unowned_vec(states);

  fidl::Array<DevicePerformanceStateInfo, 2> perf_states;
  perf_states[0].state_id = DEVICE_PERFORMANCE_STATE_P0;
  perf_states[0].is_supported = true;
  perf_states[1].state_id = 1;
  perf_states[1].is_supported = true;
  auto performance_states = ::fidl::unowned_vec(perf_states);

  auto response = TestDevice::Call::AddDeviceWithPowerArgs(zx::unowned(child_device_handle),
                                                           std::move(power_states),
                                                           std::move(performance_states), false);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }

  ASSERT_STATUS(call_status, ZX_OK);
}

TEST_F(PowerTestCase, GetDevicePowerCaps_Success) {
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  const DevicePowerStateInfo *out_dpstates;
  auto response2 = Controller::Call::GetDevicePowerCaps(zx::unowned(child2_device_handle));
  ASSERT_OK(response2.status());
  zx_status_t call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_OK(call_status);
  out_dpstates = &response2->result.response().dpstates[0];

  ASSERT_TRUE(
      out_dpstates[static_cast<uint8_t>(DevicePowerState::DEVICE_POWER_STATE_D0)].is_supported);
  ASSERT_TRUE(
      out_dpstates[static_cast<uint8_t>(DevicePowerState::DEVICE_POWER_STATE_D1)].is_supported);
  ASSERT_EQ(
      out_dpstates[static_cast<uint8_t>(DevicePowerState::DEVICE_POWER_STATE_D1)].restore_latency,
      100);
  ASSERT_TRUE(
      out_dpstates[static_cast<uint8_t>(DevicePowerState::DEVICE_POWER_STATE_D3COLD)].is_supported);
  ASSERT_EQ(out_dpstates[static_cast<uint8_t>(DevicePowerState::DEVICE_POWER_STATE_D3COLD)]
                .restore_latency,
            1000);
}

TEST_F(PowerTestCase, GetDevicePerformanceStates_Success) {
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;

  DevicePerformanceStateInfo perf_states[3];
  perf_states[0].state_id = DEVICE_PERFORMANCE_STATE_P0;
  perf_states[0].is_supported = true;
  perf_states[0].restore_latency = 0;
  perf_states[1].state_id = 1;
  perf_states[1].is_supported = true;
  perf_states[1].restore_latency = 100;
  perf_states[2].state_id = 2;
  perf_states[2].is_supported = true;
  perf_states[2].restore_latency = 1000;

  AddChildWithPowerArgs(states, std::size(states), perf_states, std::size(perf_states));

  const DevicePerformanceStateInfo *out_dpstates;
  auto response = Controller::Call::GetDevicePerformanceStates(zx::unowned(child2_device_handle));
  ASSERT_OK(response.status());
  out_dpstates = &response->states[0];

  ASSERT_TRUE(out_dpstates[DEVICE_PERFORMANCE_STATE_P0].is_supported);
  ASSERT_TRUE(out_dpstates[1].is_supported);
  ASSERT_EQ(out_dpstates[1].restore_latency, 100);
  ASSERT_TRUE(out_dpstates[2].is_supported);
  ASSERT_EQ(out_dpstates[2].restore_latency, 1000);
}

TEST_F(PowerTestCase, SetPerformanceState_Success) {
  // Add Capabilities
  DevicePowerStateInfo states[2];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[1].is_supported = true;

  DevicePerformanceStateInfo perf_states[3];
  perf_states[0].state_id = DEVICE_PERFORMANCE_STATE_P0;
  perf_states[0].is_supported = true;
  perf_states[0].restore_latency = 0;
  perf_states[1].state_id = 1;
  perf_states[1].is_supported = true;
  perf_states[1].restore_latency = 100;
  perf_states[2].state_id = 2;
  perf_states[2].is_supported = true;
  perf_states[2].restore_latency = 1000;

  AddChildWithPowerArgs(states, std::size(states), perf_states, std::size(perf_states));

  auto perf_change_result =
      Controller::Call::SetPerformanceState(zx::unowned(child2_device_handle), 1);
  ASSERT_OK(perf_change_result.status());
  const auto &perf_change_response = perf_change_result.value();
  ASSERT_OK(perf_change_response.status);
  ASSERT_EQ(perf_change_response.out_state, 1);

  auto response2 = Controller::Call::GetCurrentPerformanceState(zx::unowned(child2_device_handle));
  ASSERT_OK(response2.status());
  ASSERT_EQ(response2->out_state, 1);
}

TEST_F(PowerTestCase, SetPerformanceStateFail_HookNotPresent) {
  // Parent does not support SetPerformanceState hook.
  auto perf_change_result =
      Controller::Call::SetPerformanceState(zx::unowned(parent_device_handle), 0);
  ASSERT_OK(perf_change_result.status());
  const auto &perf_change_response = perf_change_result.value();
  ASSERT_EQ(perf_change_response.status, ZX_ERR_NOT_SUPPORTED);
}

TEST_F(PowerTestCase, SetPerformanceStateFail_UnsupportedState) {
  // Add Capabilities
  DevicePowerStateInfo states[2];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[1].is_supported = true;

  DevicePerformanceStateInfo perf_states[2];
  perf_states[0].state_id = DEVICE_PERFORMANCE_STATE_P0;
  perf_states[0].is_supported = true;
  perf_states[0].restore_latency = 0;
  perf_states[1].state_id = 1;
  perf_states[1].is_supported = true;
  perf_states[1].restore_latency = 100;

  AddChildWithPowerArgs(states, std::size(states), perf_states, std::size(perf_states));

  auto perf_change_result =
      Controller::Call::SetPerformanceState(zx::unowned(child2_device_handle), 2);
  ASSERT_OK(perf_change_result.status());
  const auto &perf_change_response = perf_change_result.value();
  ASSERT_EQ(perf_change_response.status, ZX_ERR_INVALID_ARGS);
}

TEST_F(PowerTestCase, Suspend_Success) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  auto suspend_result = Controller::Call::Suspend(zx::unowned(child2_device_handle),
                                                  DevicePowerState::DEVICE_POWER_STATE_D3COLD);
  ASSERT_OK(suspend_result.status());
  const auto &suspend_response = suspend_result.value();
  ASSERT_OK(suspend_response.status);
  ASSERT_EQ(suspend_response.out_state, DevicePowerState::DEVICE_POWER_STATE_D3COLD);

  auto response2 = TestDevice::Call::GetCurrentDevicePowerState(zx::unowned(child2_device_handle));
  ASSERT_OK(response2.status());
  zx_status_t call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(response2->result.response().cur_state, DevicePowerState::DEVICE_POWER_STATE_D3COLD);
}

TEST_F(PowerTestCase, AutoSuspend_Enable) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  auto suspend_result = Controller::Call::ConfigureAutoSuspend(
      zx::unowned(child2_device_handle), true, DevicePowerState::DEVICE_POWER_STATE_D1);
  ASSERT_OK(suspend_result.status());
  const auto &suspend_response = suspend_result.value();
  ASSERT_OK(suspend_response.status);

  auto response2 =
      TestDevice::Call::GetCurrentDeviceAutoSuspendConfig(zx::unowned(child2_device_handle));
  ASSERT_OK(response2.status());
  zx_status_t call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(response2->result.response().enabled, true);
  ASSERT_EQ(response2->result.response().deepest_sleep_state,
            DevicePowerState::DEVICE_POWER_STATE_D1);
}

TEST_F(PowerTestCase, AutoSuspend_Disable) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  auto suspend_result = Controller::Call::ConfigureAutoSuspend(
      zx::unowned(child2_device_handle), true, DevicePowerState::DEVICE_POWER_STATE_D1);
  ASSERT_OK(suspend_result.status());
  const auto &suspend_response = suspend_result.value();
  ASSERT_OK(suspend_response.status);

  auto response2 =
      TestDevice::Call::GetCurrentDeviceAutoSuspendConfig(zx::unowned(child2_device_handle));
  ASSERT_OK(response2.status());
  zx_status_t call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(response2->result.response().enabled, true);
  ASSERT_EQ(response2->result.response().deepest_sleep_state,
            DevicePowerState::DEVICE_POWER_STATE_D1);

  suspend_result = Controller::Call::ConfigureAutoSuspend(zx::unowned(child2_device_handle), false,
                                                          DevicePowerState::DEVICE_POWER_STATE_D0);
  ASSERT_OK(suspend_result.status());
  auto &suspend_response_2 = suspend_result.value();
  ASSERT_OK(suspend_response_2.status);

  response2 =
      TestDevice::Call::GetCurrentDeviceAutoSuspendConfig(zx::unowned(child2_device_handle));
  ASSERT_OK(response2.status());
  call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }

  ASSERT_OK(call_status);
  ASSERT_EQ(response2->result.response().enabled, false);
}

TEST_F(PowerTestCase, AutoSuspend_DefaultDisabled) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  auto response2 =
      TestDevice::Call::GetCurrentDeviceAutoSuspendConfig(zx::unowned(child2_device_handle));
  ASSERT_OK(response2.status());
  zx_status_t call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(response2->result.response().enabled, false);
  ASSERT_EQ(response2->result.response().deepest_sleep_state,
            DevicePowerState::DEVICE_POWER_STATE_D0);
}

TEST_F(PowerTestCase, DeviceSuspend_AutoSuspendEnabled) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  auto auto_suspend_result = Controller::Call::ConfigureAutoSuspend(
      zx::unowned(child2_device_handle), true, DevicePowerState::DEVICE_POWER_STATE_D1);
  ASSERT_OK(auto_suspend_result.status());
  const auto &auto_suspend_response = auto_suspend_result.value();
  ASSERT_OK(auto_suspend_response.status);

  auto response2 =
      TestDevice::Call::GetCurrentDeviceAutoSuspendConfig(zx::unowned(child2_device_handle));
  ASSERT_OK(response2.status());
  zx_status_t call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(response2->result.response().enabled, true);
  ASSERT_EQ(response2->result.response().deepest_sleep_state,
            DevicePowerState::DEVICE_POWER_STATE_D1);

  auto suspend_result = Controller::Call::Suspend(zx::unowned(child2_device_handle),
                                                  DevicePowerState::DEVICE_POWER_STATE_D3COLD);
  ASSERT_OK(suspend_result.status());
  const auto &suspend_response = suspend_result.value();
  // Device suspend is not supported when auto suspend is configured.
  ASSERT_EQ(suspend_response.status, ZX_ERR_NOT_SUPPORTED);

  // Disable autosuspend and try again
  auto_suspend_result = Controller::Call::ConfigureAutoSuspend(
      zx::unowned(child2_device_handle), false, DevicePowerState::DEVICE_POWER_STATE_D0);
  ASSERT_OK(auto_suspend_result.status());
  auto &auto_suspend_response_2 = auto_suspend_result.value();
  ASSERT_OK(auto_suspend_response_2.status);

  suspend_result = Controller::Call::Suspend(zx::unowned(child2_device_handle),
                                             DevicePowerState::DEVICE_POWER_STATE_D3COLD);
  ASSERT_OK(suspend_result.status());
  ASSERT_OK(suspend_result.value().status);
}

TEST_F(PowerTestCase, SystemSuspend_AutoSuspendEnabled) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D2;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  ::fidl::Array<SystemPowerStateInfo, MAX_SYSTEM_POWER_STATES> mapping{};
  for (size_t i = 0; i < MAX_SYSTEM_POWER_STATES; i++) {
    mapping[i].dev_state = DevicePowerState::DEVICE_POWER_STATE_D2;
    mapping[i].wakeup_enable = false;
  }

  auto update_result =
      Controller::Call::UpdatePowerStateMapping(zx::unowned(child2_device_handle), mapping);
  ASSERT_OK(update_result.status());
  zx_status_t call_status = ZX_OK;
  if (update_result->result.is_err()) {
    call_status = update_result->result.err();
  }
  ASSERT_OK(call_status);

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  auto auto_suspend_result = Controller::Call::ConfigureAutoSuspend(
      zx::unowned(child2_device_handle), true, DevicePowerState::DEVICE_POWER_STATE_D2);
  ASSERT_OK(auto_suspend_result.status());
  const auto &auto_suspend_response = auto_suspend_result.value();
  ASSERT_OK(auto_suspend_response.status);

  // Verify systemsuspend overrides autosuspend
  char service_name[100];
  snprintf(service_name, sizeof(service_name), "svc/%s",
           ::llcpp::fuchsia::device::manager::Administrator::Name);
  ASSERT_OK(fdio_service_connect_at(devmgr.svc_root_dir().get(), service_name, remote.release()));
  ASSERT_NE(devmgr.svc_root_dir().get(), ZX_HANDLE_INVALID);

  auto suspend_result = Administrator::Call::Suspend(
      zx::unowned(local), ::llcpp::fuchsia::device::manager::SUSPEND_FLAG_REBOOT);
  ASSERT_OK(suspend_result.status());
  const auto &suspend_response = suspend_result.value();
  ASSERT_OK(suspend_response.status);

  // Verify the child's DdkSuspend routine gets called.
  auto child_dev_suspend_response =
      TestDevice::Call::GetCurrentDevicePowerState(zx::unowned(child2_device_handle));
  ASSERT_OK(child_dev_suspend_response.status());
  call_status = ZX_OK;
  if (child_dev_suspend_response->result.is_err()) {
    call_status = child_dev_suspend_response->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(child_dev_suspend_response->result.response().cur_state,
            DevicePowerState::DEVICE_POWER_STATE_D2);

  // Verify the parent'd DdkSuspend routine gets called.
  auto parent_dev_suspend_response =
      TestDevice::Call::GetCurrentDevicePowerState(zx::unowned(parent_device_handle));
  ASSERT_OK(parent_dev_suspend_response.status());
  call_status = ZX_OK;
  if (parent_dev_suspend_response->result.is_err()) {
    call_status = parent_dev_suspend_response->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(parent_dev_suspend_response->result.response().cur_state,
            DevicePowerState::DEVICE_POWER_STATE_D3COLD);
}

TEST_F(PowerTestCase, SelectiveResume_Success) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  auto suspend_result = Controller::Call::Suspend(zx::unowned(child2_device_handle),
                                                  DevicePowerState::DEVICE_POWER_STATE_D3COLD);
  ASSERT_OK(suspend_result.status());
  const auto &suspend_response = suspend_result.value();
  ASSERT_OK(suspend_response.status);
  ASSERT_EQ(suspend_response.out_state, DevicePowerState::DEVICE_POWER_STATE_D3COLD);

  auto response2 = TestDevice::Call::GetCurrentDevicePowerState(zx::unowned(child2_device_handle));
  ASSERT_OK(response2.status());
  zx_status_t call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(response2->result.response().cur_state, DevicePowerState::DEVICE_POWER_STATE_D3COLD);

  auto resume_result = Controller::Call::Resume(zx::unowned(child2_device_handle));
  ASSERT_OK(resume_result.status());

  const auto &resume_response = resume_result.value();
  ASSERT_OK(resume_response.status);
  ASSERT_EQ(resume_response.out_power_state, DevicePowerState::DEVICE_POWER_STATE_D0);
  ASSERT_EQ(resume_response.out_perf_state, DEVICE_PERFORMANCE_STATE_P0);

  auto response3 = TestDevice::Call::GetCurrentDevicePowerState(zx::unowned(child2_device_handle));
  ASSERT_OK(response3.status());
  call_status = ZX_OK;
  if (response3->result.is_err()) {
    call_status = response3->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(response3->result.response().cur_state, DevicePowerState::DEVICE_POWER_STATE_D0);
}

TEST_F(PowerTestCase, DefaultSystemPowerStatesMapping) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  const SystemPowerStateInfo *states_mapping;
  auto response2 = Controller::Call::GetPowerStateMapping(zx::unowned(child2_device_handle));
  ASSERT_OK(response2.status());
  zx_status_t call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_STATUS(call_status, ZX_OK);
  states_mapping = &response2->result.response().mapping[0];

  // Test Default mapping. For now, the default device power state is D3COLD and
  // wakeup_enable is false.
  for (size_t i = 0; i < MAX_SYSTEM_POWER_STATES; i++) {
    ASSERT_EQ(states_mapping[i].dev_state, DevicePowerState::DEVICE_POWER_STATE_D3COLD);
    ASSERT_FALSE(states_mapping[i].wakeup_enable);
  }
}

TEST_F(PowerTestCase, UpdatePowerStatesMapping_UnsupportedDeviceState) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  ::fidl::Array<SystemPowerStateInfo, MAX_SYSTEM_POWER_STATES> mapping{};
  for (size_t i = 0; i < MAX_SYSTEM_POWER_STATES; i++) {
    mapping[i].dev_state = DevicePowerState::DEVICE_POWER_STATE_D2;
    mapping[i].wakeup_enable = false;
  }

  auto update_result =
      Controller::Call::UpdatePowerStateMapping(zx::unowned(child2_device_handle), mapping);
  ASSERT_OK(update_result.status());
  zx_status_t call_status = ZX_OK;
  if (update_result->result.is_err()) {
    call_status = update_result->result.err();
  }
  ASSERT_EQ(call_status, ZX_ERR_INVALID_ARGS);

  const SystemPowerStateInfo *states_mapping;
  auto response2 = Controller::Call::GetPowerStateMapping(zx::unowned(child2_device_handle));
  ASSERT_OK(response2.status());
  call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_STATUS(call_status, ZX_OK);
  states_mapping = &response2->result.response().mapping[0];

  ASSERT_EQ(
      states_mapping[static_cast<uint8_t>(SystemPowerState::SYSTEM_POWER_STATE_REBOOT)].dev_state,
      DevicePowerState::DEVICE_POWER_STATE_D3COLD);
  ASSERT_FALSE(states_mapping[static_cast<uint8_t>(SystemPowerState::SYSTEM_POWER_STATE_REBOOT)]
                   .wakeup_enable);
}

TEST_F(PowerTestCase, UpdatePowerStatesMapping_UnsupportedWakeConfig) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[1].wakeup_capable = false;
  states[2].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  ::fidl::Array<SystemPowerStateInfo, MAX_SYSTEM_POWER_STATES> mapping{};
  for (size_t i = 0; i < MAX_SYSTEM_POWER_STATES; i++) {
    mapping[i].dev_state = DevicePowerState::DEVICE_POWER_STATE_D1;
    mapping[i].wakeup_enable = true;
  }

  auto update_result =
      Controller::Call::UpdatePowerStateMapping(zx::unowned(child2_device_handle), mapping);
  ASSERT_OK(update_result.status());
  zx_status_t call_status = ZX_OK;
  if (update_result->result.is_err()) {
    call_status = update_result->result.err();
  }
  ASSERT_EQ(call_status, ZX_ERR_INVALID_ARGS);

  const SystemPowerStateInfo *states_mapping;
  auto response2 = Controller::Call::GetPowerStateMapping(zx::unowned(child2_device_handle));
  ASSERT_OK(response2.status());
  call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_STATUS(call_status, ZX_OK);
  states_mapping = &response2->result.response().mapping[0];

  ASSERT_EQ(
      states_mapping[static_cast<uint8_t>(SystemPowerState::SYSTEM_POWER_STATE_REBOOT)].dev_state,
      DevicePowerState::DEVICE_POWER_STATE_D3COLD);
  ASSERT_FALSE(states_mapping[static_cast<uint8_t>(SystemPowerState::SYSTEM_POWER_STATE_REBOOT)]
                   .wakeup_enable);
}

TEST_F(PowerTestCase, UpdatePowerStatesMapping_Success) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  ::fidl::Array<SystemPowerStateInfo, MAX_SYSTEM_POWER_STATES> mapping{};
  for (size_t i = 0; i < MAX_SYSTEM_POWER_STATES; i++) {
    mapping[i].dev_state = DevicePowerState::DEVICE_POWER_STATE_D1;
    mapping[i].wakeup_enable = false;
  }

  auto update_result =
      Controller::Call::UpdatePowerStateMapping(zx::unowned(child2_device_handle), mapping);
  ASSERT_OK(update_result.status());
  zx_status_t call_status = ZX_OK;
  if (update_result->result.is_err()) {
    call_status = update_result->result.err();
  }
  ASSERT_OK(call_status);

  const SystemPowerStateInfo *states_mapping;
  auto response2 = Controller::Call::GetPowerStateMapping(zx::unowned(child2_device_handle));
  ASSERT_OK(response2.status());
  call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_STATUS(call_status, ZX_OK);
  states_mapping = &response2->result.response().mapping[0];

  ASSERT_EQ(
      states_mapping[static_cast<uint8_t>(SystemPowerState::SYSTEM_POWER_STATE_REBOOT)].dev_state,
      DevicePowerState::DEVICE_POWER_STATE_D1);
  ASSERT_FALSE(states_mapping[static_cast<uint8_t>(SystemPowerState::SYSTEM_POWER_STATE_REBOOT)]
                   .wakeup_enable);
}

TEST_F(PowerTestCase, SystemSuspend_SuspendReasonReboot) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D2;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  ::fidl::Array<SystemPowerStateInfo, MAX_SYSTEM_POWER_STATES> mapping{};
  for (size_t i = 0; i < MAX_SYSTEM_POWER_STATES; i++) {
    mapping[i].dev_state = DevicePowerState::DEVICE_POWER_STATE_D2;
    mapping[i].wakeup_enable = false;
  }

  auto update_result =
      Controller::Call::UpdatePowerStateMapping(zx::unowned(child2_device_handle), mapping);
  ASSERT_OK(update_result.status());
  zx_status_t call_status = ZX_OK;
  if (update_result->result.is_err()) {
    call_status = update_result->result.err();
  }
  ASSERT_OK(call_status);

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  char service_name[100];
  snprintf(service_name, sizeof(service_name), "svc/%s",
           ::llcpp::fuchsia::device::manager::Administrator::Name);
  ASSERT_OK(fdio_service_connect_at(devmgr.svc_root_dir().get(), service_name, remote.release()));
  ASSERT_NE(devmgr.svc_root_dir().get(), ZX_HANDLE_INVALID);

  auto suspend_result = Administrator::Call::Suspend(
      zx::unowned(local), ::llcpp::fuchsia::device::manager::SUSPEND_FLAG_REBOOT);
  ASSERT_OK(suspend_result.status());
  const auto &suspend_response = suspend_result.value();
  ASSERT_OK(suspend_response.status);

  // Verify the child's DdkSuspend routine gets called.
  auto child_dev_suspend_response =
      TestDevice::Call::GetCurrentDevicePowerState(zx::unowned(child2_device_handle));
  ASSERT_OK(child_dev_suspend_response.status());
  call_status = ZX_OK;
  if (child_dev_suspend_response->result.is_err()) {
    call_status = child_dev_suspend_response->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(child_dev_suspend_response->result.response().cur_state,
            DevicePowerState::DEVICE_POWER_STATE_D2);

  // Verify that the suspend reason is received correctly
  auto suspend_reason_response =
      TestDevice::Call::GetCurrentSuspendReason(zx::unowned(child2_device_handle));
  ASSERT_OK(suspend_reason_response.status());
  call_status = ZX_OK;
  if (suspend_reason_response->result.is_err()) {
    call_status = suspend_reason_response->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(suspend_reason_response->result.response().cur_suspend_reason,
            DEVICE_SUSPEND_REASON_REBOOT);

  // Verify the parent'd DdkSuspend routine gets called.
  auto parent_dev_suspend_response =
      TestDevice::Call::GetCurrentDevicePowerState(zx::unowned(parent_device_handle));
  ASSERT_OK(parent_dev_suspend_response.status());
  call_status = ZX_OK;
  if (parent_dev_suspend_response->result.is_err()) {
    call_status = parent_dev_suspend_response->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(parent_dev_suspend_response->result.response().cur_state,
            DevicePowerState::DEVICE_POWER_STATE_D3COLD);
}

TEST_F(PowerTestCase, SystemSuspend_SuspendReasonRebootRecovery) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D2;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  ::fidl::Array<SystemPowerStateInfo, MAX_SYSTEM_POWER_STATES> mapping{};
  for (size_t i = 0; i < MAX_SYSTEM_POWER_STATES; i++) {
    mapping[i].dev_state = DevicePowerState::DEVICE_POWER_STATE_D2;
    mapping[i].wakeup_enable = false;
  }

  auto update_result =
      Controller::Call::UpdatePowerStateMapping(zx::unowned(child2_device_handle), mapping);
  ASSERT_OK(update_result.status());
  zx_status_t call_status = ZX_OK;
  if (update_result->result.is_err()) {
    call_status = update_result->result.err();
  }
  ASSERT_OK(call_status);

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  char service_name[100];
  snprintf(service_name, sizeof(service_name), "svc/%s",
           ::llcpp::fuchsia::device::manager::Administrator::Name);
  ASSERT_OK(fdio_service_connect_at(devmgr.svc_root_dir().get(), service_name, remote.release()));
  ASSERT_NE(devmgr.svc_root_dir().get(), ZX_HANDLE_INVALID);

  auto suspend_result = Administrator::Call::Suspend(
      zx::unowned(local), ::llcpp::fuchsia::device::manager::SUSPEND_FLAG_REBOOT_RECOVERY);
  ASSERT_OK(suspend_result.status());
  const auto &suspend_response = suspend_result.value();
  ASSERT_OK(suspend_response.status);

  // Verify the child's DdkSuspend routine gets called.
  auto child_dev_suspend_response =
      TestDevice::Call::GetCurrentDevicePowerState(zx::unowned(child2_device_handle));
  ASSERT_OK(child_dev_suspend_response.status());
  call_status = ZX_OK;
  if (child_dev_suspend_response->result.is_err()) {
    call_status = child_dev_suspend_response->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(child_dev_suspend_response->result.response().cur_state,
            DevicePowerState::DEVICE_POWER_STATE_D2);

  // Verify that the suspend reason is received correctly
  auto suspend_reason_response =
      TestDevice::Call::GetCurrentSuspendReason(zx::unowned(child2_device_handle));
  ASSERT_OK(suspend_reason_response.status());
  call_status = ZX_OK;
  if (suspend_reason_response->result.is_err()) {
    call_status = suspend_reason_response->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(suspend_reason_response->result.response().cur_suspend_reason,
            DEVICE_SUSPEND_REASON_REBOOT_RECOVERY);

  // Verify the parent'd DdkSuspend routine gets called.
  auto parent_dev_suspend_response =
      TestDevice::Call::GetCurrentDevicePowerState(zx::unowned(parent_device_handle));
  ASSERT_OK(parent_dev_suspend_response.status());
  call_status = ZX_OK;
  if (parent_dev_suspend_response->result.is_err()) {
    call_status = parent_dev_suspend_response->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(parent_dev_suspend_response->result.response().cur_state,
            DevicePowerState::DEVICE_POWER_STATE_D3COLD);
}

TEST_F(PowerTestCase, SelectiveResume_AfterSetPerformanceState) {
  // Add Capabilities
  DevicePowerStateInfo states[2];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[1].is_supported = true;

  DevicePerformanceStateInfo perf_states[3];
  perf_states[0].state_id = DEVICE_PERFORMANCE_STATE_P0;
  perf_states[0].is_supported = true;
  perf_states[0].restore_latency = 0;
  perf_states[1].state_id = 1;
  perf_states[1].is_supported = true;
  perf_states[1].restore_latency = 100;
  perf_states[2].state_id = 2;
  perf_states[2].is_supported = true;
  perf_states[2].restore_latency = 1000;

  AddChildWithPowerArgs(states, std::size(states), perf_states, std::size(perf_states));

  auto perf_change_result =
      Controller::Call::SetPerformanceState(zx::unowned(child2_device_handle), 1);
  ASSERT_OK(perf_change_result.status());
  const auto &perf_change_response = perf_change_result.value();
  ASSERT_OK(perf_change_response.status);
  ASSERT_EQ(perf_change_response.out_state, 1);

  auto response2 = Controller::Call::GetCurrentPerformanceState(zx::unowned(child2_device_handle));
  ASSERT_OK(response2.status());
  ASSERT_EQ(response2->out_state, 1);

  // Suspend and resume the device. Test if device resumes to saved performance state.
  auto suspend_result = Controller::Call::Suspend(zx::unowned(child2_device_handle),
                                                  DevicePowerState::DEVICE_POWER_STATE_D3COLD);
  ASSERT_OK(suspend_result.status());
  const auto &suspend_response = suspend_result.value();
  ASSERT_OK(suspend_response.status);
  ASSERT_EQ(suspend_response.out_state, DevicePowerState::DEVICE_POWER_STATE_D3COLD);

  auto response3 = TestDevice::Call::GetCurrentDevicePowerState(zx::unowned(child2_device_handle));
  ASSERT_OK(response3.status());
  zx_status_t call_status = ZX_OK;
  if (response3->result.is_err()) {
    call_status = response3->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(response3->result.response().cur_state, DevicePowerState::DEVICE_POWER_STATE_D3COLD);

  // Resume
  auto resume_result = Controller::Call::Resume(zx::unowned(child2_device_handle));
  ASSERT_OK(resume_result.status());

  const auto &resume_response = resume_result.value();
  ASSERT_OK(resume_response.status);
  ASSERT_EQ(resume_response.out_power_state, DevicePowerState::DEVICE_POWER_STATE_D0);
  ASSERT_EQ(resume_response.out_perf_state, 1);
}

TEST_F(PowerTestCase, SelectiveResume_FailedToResumeToWorking) {
  // Add Capabilities
  DevicePowerStateInfo states[2];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[1].is_supported = true;

  DevicePerformanceStateInfo perf_states[3];
  perf_states[0].state_id = DEVICE_PERFORMANCE_STATE_P0;
  perf_states[0].is_supported = true;
  perf_states[0].restore_latency = 0;
  perf_states[1].state_id = 1;
  perf_states[1].is_supported = true;
  perf_states[1].restore_latency = 100;
  perf_states[2].state_id = 2;
  perf_states[2].is_supported = true;
  perf_states[2].restore_latency = 1000;

  AddChildWithPowerArgs(states, std::size(states), perf_states, std::size(perf_states));

  auto perf_change_result =
      Controller::Call::SetPerformanceState(zx::unowned(child2_device_handle), 1);
  ASSERT_OK(perf_change_result.status());
  const auto &perf_change_response = perf_change_result.value();
  ASSERT_OK(perf_change_response.status);
  ASSERT_EQ(perf_change_response.out_state, 1);

  auto response2 = Controller::Call::GetCurrentPerformanceState(zx::unowned(child2_device_handle));
  ASSERT_OK(response2.status());
  ASSERT_EQ(response2->out_state, 1);

  // Suspend
  auto suspend_result = Controller::Call::Suspend(zx::unowned(child2_device_handle),
                                                  DevicePowerState::DEVICE_POWER_STATE_D3COLD);
  ASSERT_OK(suspend_result.status());
  const auto &suspend_response = suspend_result.value();
  ASSERT_OK(suspend_response.status);
  ASSERT_EQ(suspend_response.out_state, DevicePowerState::DEVICE_POWER_STATE_D3COLD);

  auto response3 = TestDevice::Call::GetCurrentDevicePowerState(zx::unowned(child2_device_handle));
  ASSERT_OK(response3.status());
  zx_status_t call_status = ZX_OK;
  if (response3->result.is_err()) {
    call_status = response3->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(response3->result.response().cur_state, DevicePowerState::DEVICE_POWER_STATE_D3COLD);

  ::llcpp::fuchsia::device::power::test::TestStatusInfo info;
  info.resume_status = ZX_ERR_IO;
  info.out_power_state = static_cast<uint8_t>(DevicePowerState::DEVICE_POWER_STATE_D3COLD);
  info.out_performance_state = 1;
  auto response4 = TestDevice::Call::SetTestStatusInfo(zx::unowned(child2_device_handle), info);
  call_status = ZX_OK;
  ASSERT_OK(response4.status());
  if (response4->result.is_err()) {
    call_status = response4->result.err();
  }
  ASSERT_OK(call_status);

  // Resume
  auto resume_result = Controller::Call::Resume(zx::unowned(child2_device_handle));
  ASSERT_OK(resume_result.status());

  const auto &resume_response = resume_result.value();
  ASSERT_EQ(resume_response.status, info.resume_status);
  ASSERT_EQ(static_cast<uint8_t>(resume_response.out_power_state), info.out_power_state);
}

TEST_F(PowerTestCase, SelectiveResume_FailedToResumeToPerformanceState) {
  // Add Capabilities
  DevicePowerStateInfo states[2];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[1].is_supported = true;

  DevicePerformanceStateInfo perf_states[3];
  perf_states[0].state_id = DEVICE_PERFORMANCE_STATE_P0;
  perf_states[0].is_supported = true;
  perf_states[0].restore_latency = 0;
  perf_states[1].state_id = 1;
  perf_states[1].is_supported = true;
  perf_states[1].restore_latency = 100;
  perf_states[2].state_id = 2;
  perf_states[2].is_supported = true;
  perf_states[2].restore_latency = 1000;

  AddChildWithPowerArgs(states, std::size(states), perf_states, std::size(perf_states));

  auto perf_change_result =
      Controller::Call::SetPerformanceState(zx::unowned(child2_device_handle), 1);
  ASSERT_OK(perf_change_result.status());
  const auto &perf_change_response = perf_change_result.value();
  ASSERT_OK(perf_change_response.status);
  ASSERT_EQ(perf_change_response.out_state, 1);

  auto response2 = Controller::Call::GetCurrentPerformanceState(zx::unowned(child2_device_handle));
  ASSERT_OK(response2.status());
  ASSERT_EQ(response2->out_state, 1);

  // Suspend
  auto suspend_result = Controller::Call::Suspend(zx::unowned(child2_device_handle),
                                                  DevicePowerState::DEVICE_POWER_STATE_D3COLD);
  ASSERT_OK(suspend_result.status());
  const auto &suspend_response = suspend_result.value();
  ASSERT_OK(suspend_response.status);
  ASSERT_EQ(suspend_response.out_state, DevicePowerState::DEVICE_POWER_STATE_D3COLD);

  auto response3 = TestDevice::Call::GetCurrentDevicePowerState(zx::unowned(child2_device_handle));
  ASSERT_OK(response3.status());
  zx_status_t call_status = ZX_OK;
  if (response3->result.is_err()) {
    call_status = response3->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(response3->result.response().cur_state, DevicePowerState::DEVICE_POWER_STATE_D3COLD);

  ::llcpp::fuchsia::device::power::test::TestStatusInfo info;
  info.resume_status = ZX_ERR_IO;
  info.out_power_state = static_cast<uint8_t>(DevicePowerState::DEVICE_POWER_STATE_D0);
  // The previous performance_state set was 1.
  info.out_performance_state = 2;
  auto response4 = TestDevice::Call::SetTestStatusInfo(zx::unowned(child2_device_handle), info);
  call_status = ZX_OK;
  ASSERT_OK(response4.status());
  if (response4->result.is_err()) {
    call_status = response4->result.err();
  }
  ASSERT_OK(call_status);

  // Resume
  auto resume_result = Controller::Call::Resume(zx::unowned(child2_device_handle));
  ASSERT_OK(resume_result.status());

  const auto &resume_response = resume_result.value();
  ASSERT_EQ(resume_response.status, info.resume_status);
  ASSERT_EQ(static_cast<uint8_t>(resume_response.out_power_state), info.out_power_state);
  ASSERT_EQ(resume_response.out_perf_state, info.out_performance_state);

  // The performance state has to be updated to the state that the device resumed to.
  auto response5 = Controller::Call::GetCurrentPerformanceState(zx::unowned(child2_device_handle));
  ASSERT_OK(response5.status());
  ASSERT_EQ(response5->out_state, info.out_performance_state);
}

TEST_F(PowerTestCase, DeviceResume_AutoSuspendEnabled) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::DEVICE_POWER_STATE_D0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::DEVICE_POWER_STATE_D1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::DEVICE_POWER_STATE_D3COLD;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  auto auto_suspend_result = Controller::Call::ConfigureAutoSuspend(
      zx::unowned(child2_device_handle), true, DevicePowerState::DEVICE_POWER_STATE_D1);
  ASSERT_OK(auto_suspend_result.status());
  const auto &auto_suspend_response = auto_suspend_result.value();
  ASSERT_OK(auto_suspend_response.status);

  auto response2 =
      TestDevice::Call::GetCurrentDeviceAutoSuspendConfig(zx::unowned(child2_device_handle));
  ASSERT_OK(response2.status());
  zx_status_t call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(response2->result.response().enabled, true);
  ASSERT_EQ(response2->result.response().deepest_sleep_state,
            DevicePowerState::DEVICE_POWER_STATE_D1);

  auto resume_result = Controller::Call::Resume(zx::unowned(child2_device_handle));
  ASSERT_OK(resume_result.status());
  // Device resume is not supported when auto suspend is configured.
  ASSERT_EQ(resume_result.value().status, ZX_ERR_NOT_SUPPORTED);

  // Disable autosuspend and try again
  auto_suspend_result = Controller::Call::ConfigureAutoSuspend(
      zx::unowned(child2_device_handle), false, DevicePowerState::DEVICE_POWER_STATE_D0);
  ASSERT_OK(auto_suspend_result.status());
  auto &auto_suspend_response_2 = auto_suspend_result.value();
  ASSERT_OK(auto_suspend_response_2.status);

  auto suspend_result = Controller::Call::Suspend(zx::unowned(child2_device_handle),
                                                  DevicePowerState::DEVICE_POWER_STATE_D3COLD);
  ASSERT_OK(suspend_result.status());
  ASSERT_OK(suspend_result.value().status);

  resume_result = Controller::Call::Resume(zx::unowned(child2_device_handle));
  ASSERT_OK(resume_result.status());
  ASSERT_OK(resume_result.value().status);
}

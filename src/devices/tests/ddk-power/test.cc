// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device.manager/cpp/wire.h>
#include <fidl/fuchsia.device.power.test/cpp/wire.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.power.statecontrol/cpp/wire.h>
#include <fidl/fuchsia.process.lifecycle/cpp/wire.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/service/llcpp/service.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <zxtest/zxtest.h>

using driver_integration_test::IsolatedDevmgr;
using fuchsia_device::Controller;
using fuchsia_device::wire::DevicePerformanceStateInfo;
using fuchsia_device::wire::DevicePowerState;
using fuchsia_device::wire::DevicePowerStateInfo;
using fuchsia_device::wire::kDevicePerformanceStateP0;
using fuchsia_device::wire::kMaxDevicePerformanceStates;
using fuchsia_device::wire::kMaxDevicePowerStates;
using fuchsia_device::wire::SystemPowerStateInfo;
using fuchsia_device_power_test::TestDevice;
using fuchsia_hardware_power_statecontrol::wire::kMaxSystemPowerStates;
using fuchsia_hardware_power_statecontrol::wire::SystemPowerState;
namespace device_manager_fidl = fuchsia_device_manager;
namespace lifecycle_fidl = fuchsia_process_lifecycle;

class PowerTestCase : public zxtest::Test {
 public:
  ~PowerTestCase() override = default;
  void SetUp() override {
    IsolatedDevmgr::Args args;
    args.no_exit_after_suspend = true;

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
    auto power_states =
        ::fidl::VectorView<DevicePowerStateInfo>::FromExternal(states, sleep_state_count);
    auto perf_power_states =
        ::fidl::VectorView<DevicePerformanceStateInfo>::FromExternal(perf_states, perf_state_count);
    auto response = fidl::WireCall<TestDevice>(zx::unowned(child_device_handle))
                        ->AddDeviceWithPowerArgs(power_states, perf_power_states, add_invisible);
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

  void WaitForDeviceSuspendCompletion(zx::unowned_channel device_chan) {
    auto response =
        fidl::WireCall<TestDevice>(zx::unowned(device_chan))->GetSuspendCompletionEvent();
    ASSERT_OK(response.status());
    zx_status_t call_status = ZX_OK;
    if (response->result.is_err()) {
      call_status = response->result.err();
    }
    ASSERT_OK(call_status);
    zx::event event(std::move(response->result.mutable_response().event));
    zx_signals_t signals;
    ASSERT_OK(event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), &signals));
  }

  void SetTerminationSystemState(SystemPowerState state) {
    ASSERT_NE(devmgr.svc_root_dir().channel(), ZX_HANDLE_INVALID);
    auto svc = service::ConnectAt<fuchsia_io::Directory>(devmgr.svc_root_dir(), "svc");
    ASSERT_OK(svc.status_value());
    auto local = service::ConnectAt<device_manager_fidl::SystemStateTransition>(*svc);
    ASSERT_OK(local.status_value());
    auto system_state_transition_client = fidl::BindSyncClient(std::move(*local));
    auto resp = system_state_transition_client->SetTerminationSystemState(state);
    ASSERT_OK(resp.status());
    ASSERT_FALSE(resp->result.is_err());
  }
  zx::channel child_device_handle;
  zx::channel parent_device_handle;
  zx::channel child2_device_handle;
  IsolatedDevmgr devmgr;
};

TEST_F(PowerTestCase, InvalidDevicePowerCaps_Less) {
  std::array<DevicePowerStateInfo, 1> states;
  states[0].state_id = DevicePowerState::kDevicePowerStateD1;
  states[0].is_supported = true;
  auto response =
      fidl::WireCall<TestDevice>(zx::unowned(child_device_handle))
          ->AddDeviceWithPowerArgs(fidl::VectorView<DevicePowerStateInfo>::FromExternal(states),
                                   ::fidl::VectorView<DevicePerformanceStateInfo>(), false);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }
  ASSERT_STATUS(call_status, ZX_ERR_INVALID_ARGS);
}

TEST_F(PowerTestCase, InvalidDevicePowerCaps_More) {
  std::array<DevicePowerStateInfo, kMaxDevicePowerStates + 1> states;
  for (uint8_t i = 0; i < kMaxDevicePowerStates + 1; i++) {
    states[i].state_id = DevicePowerState::kDevicePowerStateD1;
    states[i].is_supported = true;
  }
  auto response =
      fidl::WireCall<TestDevice>(zx::unowned(child_device_handle))
          ->AddDeviceWithPowerArgs(fidl::VectorView<DevicePowerStateInfo>::FromExternal(states),
                                   ::fidl::VectorView<DevicePerformanceStateInfo>(), false);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }

  ASSERT_STATUS(call_status, ZX_ERR_INVALID_ARGS);
}

TEST_F(PowerTestCase, InvalidDevicePowerCaps_MissingRequired) {
  std::array<DevicePowerStateInfo, kMaxDevicePowerStates> states;
  for (uint8_t i = 0; i < kMaxDevicePowerStates; i++) {
    // Missing D0 and D3COLD
    states[i].state_id = DevicePowerState::kDevicePowerStateD1;
    states[i].is_supported = true;
  }
  auto response =
      fidl::WireCall<TestDevice>(zx::unowned(child_device_handle))
          ->AddDeviceWithPowerArgs(fidl::VectorView<DevicePowerStateInfo>::FromExternal(states),
                                   ::fidl::VectorView<DevicePerformanceStateInfo>(), false);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }

  ASSERT_STATUS(call_status, ZX_ERR_INVALID_ARGS);
}

TEST_F(PowerTestCase, InvalidDevicePowerCaps_DuplicateCaps) {
  std::array<DevicePowerStateInfo, kMaxDevicePowerStates> states;
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[1].is_supported = true;
  // Repeat
  states[2].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[2].is_supported = true;
  auto response =
      fidl::WireCall<TestDevice>(zx::unowned(child_device_handle))
          ->AddDeviceWithPowerArgs(fidl::VectorView<DevicePowerStateInfo>::FromExternal(states),
                                   ::fidl::VectorView<DevicePerformanceStateInfo>(), false);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }

  ASSERT_STATUS(call_status, ZX_ERR_INVALID_ARGS);
}

TEST_F(PowerTestCase, AddDevicePowerCaps_Success) {
  std::array<DevicePowerStateInfo, 2> states;
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[1].is_supported = true;
  auto response =
      fidl::WireCall<TestDevice>(zx::unowned(child_device_handle))
          ->AddDeviceWithPowerArgs(fidl::VectorView<DevicePowerStateInfo>::FromExternal(states),
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
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::kDevicePowerStateD1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;

  DevicePerformanceStateInfo perf_states[3];
  perf_states[0].state_id = kDevicePerformanceStateP0;
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
  auto response2 =
      fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->GetDevicePowerCaps();
  ASSERT_OK(response2.status());
  zx_status_t call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_OK(call_status);
  out_dpstates = &response2->result.response().dpstates[0];

  ASSERT_TRUE(
      out_dpstates[static_cast<uint8_t>(DevicePowerState::kDevicePowerStateD0)].is_supported);
  ASSERT_TRUE(
      out_dpstates[static_cast<uint8_t>(DevicePowerState::kDevicePowerStateD1)].is_supported);
  ASSERT_EQ(
      out_dpstates[static_cast<uint8_t>(DevicePowerState::kDevicePowerStateD1)].restore_latency,
      100);
  ASSERT_TRUE(
      out_dpstates[static_cast<uint8_t>(DevicePowerState::kDevicePowerStateD3Cold)].is_supported);

  ASSERT_EQ(
      out_dpstates[static_cast<uint8_t>(DevicePowerState::kDevicePowerStateD3Cold)].restore_latency,
      1000);
  const DevicePerformanceStateInfo *out_perf_states;
  auto response =
      fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->GetDevicePerformanceStates();
  ASSERT_OK(response.status());
  out_perf_states = &response->states[0];

  ASSERT_TRUE(out_perf_states[kDevicePerformanceStateP0].is_supported);
  ASSERT_TRUE(out_perf_states[1].is_supported);
  ASSERT_EQ(out_perf_states[1].restore_latency, 100);
  ASSERT_TRUE(out_perf_states[2].is_supported);
  ASSERT_EQ(out_perf_states[2].restore_latency, 1000);
}

TEST_F(PowerTestCase, InvalidDevicePerformanceCaps_MissingRequired) {
  std::array<DevicePowerStateInfo, 2> states;
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[1].is_supported = true;

  std::array<DevicePerformanceStateInfo, 10> perf_states;
  perf_states[0].state_id = 1;
  perf_states[0].is_supported = true;
  perf_states[1].state_id = 2;
  perf_states[1].is_supported = true;

  auto response =
      fidl::WireCall<TestDevice>(zx::unowned(child_device_handle))
          ->AddDeviceWithPowerArgs(
              fidl::VectorView<DevicePowerStateInfo>::FromExternal(states),
              fidl::VectorView<DevicePerformanceStateInfo>::FromExternal(perf_states), false);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }

  ASSERT_STATUS(call_status, ZX_ERR_INVALID_ARGS);
}

TEST_F(PowerTestCase, InvalidDevicePerformanceCaps_Duplicate) {
  std::array<DevicePowerStateInfo, 2> states;
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[1].is_supported = true;

  std::array<DevicePerformanceStateInfo, 10> perf_states;
  perf_states[0].state_id = kDevicePerformanceStateP0;
  perf_states[0].is_supported = true;
  perf_states[1].state_id = kDevicePerformanceStateP0;
  perf_states[1].is_supported = true;
  perf_states[2].state_id = 1;
  perf_states[2].is_supported = true;

  auto response =
      fidl::WireCall<TestDevice>(zx::unowned(child_device_handle))
          ->AddDeviceWithPowerArgs(
              fidl::VectorView<DevicePowerStateInfo>::FromExternal(states),
              fidl::VectorView<DevicePerformanceStateInfo>::FromExternal(perf_states), false);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }

  ASSERT_STATUS(call_status, ZX_ERR_INVALID_ARGS);
}

TEST_F(PowerTestCase, InvalidDevicePerformanceCaps_More) {
  std::array<DevicePowerStateInfo, 2> states;
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[1].is_supported = true;

  std::array<DevicePerformanceStateInfo, kMaxDevicePerformanceStates + 1> perf_states;
  for (size_t i = 0; i < (kMaxDevicePerformanceStates + 1); i++) {
    perf_states[i].state_id = static_cast<int32_t>(i);
    perf_states[i].is_supported = true;
  }
  auto response =
      fidl::WireCall<TestDevice>(zx::unowned(child_device_handle))
          ->AddDeviceWithPowerArgs(
              fidl::VectorView<DevicePowerStateInfo>::FromExternal(states),
              fidl::VectorView<DevicePerformanceStateInfo>::FromExternal(perf_states), false);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }

  ASSERT_STATUS(call_status, ZX_ERR_INVALID_ARGS);
}

TEST_F(PowerTestCase, AddDevicePerformanceCaps_NoCaps) {
  std::array<DevicePowerStateInfo, 2> states;
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[1].is_supported = true;
  auto power_states = ::fidl::VectorView<DevicePowerStateInfo>::FromExternal(states);

  // This is the default case. By default, the devhost fills in the fully performance state.
  auto response =
      fidl::WireCall<TestDevice>(zx::unowned(child_device_handle))
          ->AddDeviceWithPowerArgs(std::move(power_states),
                                   ::fidl::VectorView<DevicePerformanceStateInfo>(), false);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }

  ASSERT_STATUS(call_status, ZX_OK);
}

TEST_F(PowerTestCase, AddDevicePerformanceCaps_Success) {
  std::array<DevicePowerStateInfo, 2> states;
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[1].is_supported = true;
  auto power_states = ::fidl::VectorView<DevicePowerStateInfo>::FromExternal(states);

  std::array<DevicePerformanceStateInfo, 2> perf_states;
  perf_states[0].state_id = kDevicePerformanceStateP0;
  perf_states[0].is_supported = true;
  perf_states[1].state_id = 1;
  perf_states[1].is_supported = true;
  auto performance_states =
      ::fidl::VectorView<DevicePerformanceStateInfo>::FromExternal(perf_states);

  auto response =
      fidl::WireCall<TestDevice>(zx::unowned(child_device_handle))
          ->AddDeviceWithPowerArgs(std::move(power_states), std::move(performance_states), false);
  ASSERT_OK(response.status());
  zx_status_t call_status = ZX_OK;
  if (response->result.is_err()) {
    call_status = response->result.err();
  }

  ASSERT_STATUS(call_status, ZX_OK);
}

TEST_F(PowerTestCase, GetDevicePowerCaps_Success) {
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::kDevicePowerStateD1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  const DevicePowerStateInfo *out_dpstates;
  auto response2 =
      fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->GetDevicePowerCaps();
  ASSERT_OK(response2.status());
  zx_status_t call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_OK(call_status);
  out_dpstates = &response2->result.response().dpstates[0];

  ASSERT_TRUE(
      out_dpstates[static_cast<uint8_t>(DevicePowerState::kDevicePowerStateD0)].is_supported);
  ASSERT_TRUE(
      out_dpstates[static_cast<uint8_t>(DevicePowerState::kDevicePowerStateD1)].is_supported);
  ASSERT_EQ(
      out_dpstates[static_cast<uint8_t>(DevicePowerState::kDevicePowerStateD1)].restore_latency,
      100);
  ASSERT_TRUE(
      out_dpstates[static_cast<uint8_t>(DevicePowerState::kDevicePowerStateD3Cold)].is_supported);
  ASSERT_EQ(
      out_dpstates[static_cast<uint8_t>(DevicePowerState::kDevicePowerStateD3Cold)].restore_latency,
      1000);
}

TEST_F(PowerTestCase, GetDevicePerformanceStates_Success) {
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::kDevicePowerStateD1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;

  DevicePerformanceStateInfo perf_states[3];
  perf_states[0].state_id = kDevicePerformanceStateP0;
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
  auto response =
      fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->GetDevicePerformanceStates();
  ASSERT_OK(response.status());
  out_dpstates = &response->states[0];

  ASSERT_TRUE(out_dpstates[kDevicePerformanceStateP0].is_supported);
  ASSERT_TRUE(out_dpstates[1].is_supported);
  ASSERT_EQ(out_dpstates[1].restore_latency, 100);
  ASSERT_TRUE(out_dpstates[2].is_supported);
  ASSERT_EQ(out_dpstates[2].restore_latency, 1000);
}

TEST_F(PowerTestCase, SetPerformanceState_Success) {
  // Add Capabilities
  DevicePowerStateInfo states[2];
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[1].is_supported = true;

  DevicePerformanceStateInfo perf_states[3];
  perf_states[0].state_id = kDevicePerformanceStateP0;
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
      fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->SetPerformanceState(1);
  ASSERT_OK(perf_change_result.status());
  const auto &perf_change_response = perf_change_result.value();
  ASSERT_OK(perf_change_response.status);
  ASSERT_EQ(perf_change_response.out_state, 1);

  auto response2 =
      fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->GetCurrentPerformanceState();
  ASSERT_OK(response2.status());
  ASSERT_EQ(response2->out_state, 1);
}

TEST_F(PowerTestCase, SetPerformanceStateFail_HookNotPresent) {
  // Parent does not support SetPerformanceState hook.
  auto perf_change_result =
      fidl::WireCall<Controller>(zx::unowned(parent_device_handle))->SetPerformanceState(0);
  ASSERT_OK(perf_change_result.status());
  const auto &perf_change_response = perf_change_result.value();
  ASSERT_EQ(perf_change_response.status, ZX_ERR_NOT_SUPPORTED);
}

TEST_F(PowerTestCase, SetPerformanceStateFail_UnsupportedState) {
  // Add Capabilities
  DevicePowerStateInfo states[2];
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[1].is_supported = true;

  DevicePerformanceStateInfo perf_states[2];
  perf_states[0].state_id = kDevicePerformanceStateP0;
  perf_states[0].is_supported = true;
  perf_states[0].restore_latency = 0;
  perf_states[1].state_id = 1;
  perf_states[1].is_supported = true;
  perf_states[1].restore_latency = 100;

  AddChildWithPowerArgs(states, std::size(states), perf_states, std::size(perf_states));

  auto perf_change_result =
      fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->SetPerformanceState(2);
  ASSERT_OK(perf_change_result.status());
  const auto &perf_change_response = perf_change_result.value();
  ASSERT_EQ(perf_change_response.status, ZX_ERR_INVALID_ARGS);
}

TEST_F(PowerTestCase, Suspend_Success) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::kDevicePowerStateD1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  auto suspend_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))
                            ->Suspend(DevicePowerState::kDevicePowerStateD3Cold);
  ASSERT_OK(suspend_result.status());
  const auto &suspend_response = suspend_result.value();
  ASSERT_OK(suspend_response.status);
  ASSERT_EQ(suspend_response.out_state, DevicePowerState::kDevicePowerStateD3Cold);

  auto response2 =
      fidl::WireCall<TestDevice>(zx::unowned(child2_device_handle))->GetCurrentDevicePowerState();
  ASSERT_OK(response2.status());
  zx_status_t call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(response2->result.response().cur_state, DevicePowerState::kDevicePowerStateD3Cold);
}

TEST_F(PowerTestCase, AutoSuspend_Enable) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::kDevicePowerStateD1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  auto suspend_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))
                            ->ConfigureAutoSuspend(true, DevicePowerState::kDevicePowerStateD1);
  ASSERT_OK(suspend_result.status());
  const auto &suspend_response = suspend_result.value();
  ASSERT_OK(suspend_response.status);

  auto response2 = fidl::WireCall<TestDevice>(zx::unowned(child2_device_handle))
                       ->GetCurrentDeviceAutoSuspendConfig();
  ASSERT_OK(response2.status());
  zx_status_t call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(response2->result.response().enabled, true);
  ASSERT_EQ(response2->result.response().deepest_sleep_state,
            DevicePowerState::kDevicePowerStateD1);
}

TEST_F(PowerTestCase, AutoSuspend_Disable) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::kDevicePowerStateD1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  {
    auto suspend_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))
                              ->ConfigureAutoSuspend(true, DevicePowerState::kDevicePowerStateD1);
    ASSERT_OK(suspend_result.status());
    const auto &suspend_response = suspend_result.value();
    ASSERT_OK(suspend_response.status);
  }

  {
    auto response = fidl::WireCall<TestDevice>(zx::unowned(child2_device_handle))
                        ->GetCurrentDeviceAutoSuspendConfig();
    ASSERT_OK(response.status());
    zx_status_t call_status = ZX_OK;
    if (response->result.is_err()) {
      call_status = response->result.err();
    }
    ASSERT_OK(call_status);
    ASSERT_EQ(response->result.response().enabled, true);
    ASSERT_EQ(response->result.response().deepest_sleep_state,
              DevicePowerState::kDevicePowerStateD1);
  }

  {
    auto suspend_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))
                              ->ConfigureAutoSuspend(false, DevicePowerState::kDevicePowerStateD0);
    ASSERT_OK(suspend_result.status());
    auto &suspend_response = suspend_result.value();
    ASSERT_OK(suspend_response.status);
  }

  {
    auto response = fidl::WireCall<TestDevice>(zx::unowned(child2_device_handle))
                        ->GetCurrentDeviceAutoSuspendConfig();
    ASSERT_OK(response.status());
    auto call_status = ZX_OK;
    if (response->result.is_err()) {
      call_status = response->result.err();
    }

    ASSERT_OK(call_status);
    ASSERT_EQ(response->result.response().enabled, false);
  }
}

TEST_F(PowerTestCase, AutoSuspend_DefaultDisabled) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::kDevicePowerStateD1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  auto response2 = fidl::WireCall<TestDevice>(zx::unowned(child2_device_handle))
                       ->GetCurrentDeviceAutoSuspendConfig();
  ASSERT_OK(response2.status());
  zx_status_t call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(response2->result.response().enabled, false);
  ASSERT_EQ(response2->result.response().deepest_sleep_state,
            DevicePowerState::kDevicePowerStateD0);
}

TEST_F(PowerTestCase, DeviceSuspend_AutoSuspendEnabled) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::kDevicePowerStateD1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  auto auto_suspend_result =
      fidl::WireCall<Controller>(zx::unowned(child2_device_handle))
          ->ConfigureAutoSuspend(true, DevicePowerState::kDevicePowerStateD1);
  ASSERT_OK(auto_suspend_result.status());
  const auto &auto_suspend_response = auto_suspend_result.value();
  ASSERT_OK(auto_suspend_response.status);

  auto response2 = fidl::WireCall<TestDevice>(zx::unowned(child2_device_handle))
                       ->GetCurrentDeviceAutoSuspendConfig();
  ASSERT_OK(response2.status());
  zx_status_t call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(response2->result.response().enabled, true);
  ASSERT_EQ(response2->result.response().deepest_sleep_state,
            DevicePowerState::kDevicePowerStateD1);

  {
    auto suspend_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))
                              ->Suspend(DevicePowerState::kDevicePowerStateD3Cold);
    ASSERT_OK(suspend_result.status());
    const auto &suspend_response = suspend_result.value();
    // Device suspend is not supported when auto suspend is configured.
    ASSERT_EQ(suspend_response.status, ZX_ERR_NOT_SUPPORTED);
  }

  {
    // Disable autosuspend and try again
    auto auto_suspend_result =
        fidl::WireCall<Controller>(zx::unowned(child2_device_handle))
            ->ConfigureAutoSuspend(false, DevicePowerState::kDevicePowerStateD0);
    ASSERT_OK(auto_suspend_result.status());
    auto &auto_suspend_response = auto_suspend_result.value();
    ASSERT_OK(auto_suspend_response.status);

    auto suspend_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))
                              ->Suspend(DevicePowerState::kDevicePowerStateD3Cold);
    ASSERT_OK(suspend_result.status());
    ASSERT_OK(suspend_result.value().status);
  }
}

TEST_F(PowerTestCase, SystemSuspend_AutoSuspendEnabled) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::kDevicePowerStateD2;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  ::fidl::Array<SystemPowerStateInfo, kMaxSystemPowerStates> mapping{};
  for (size_t i = 0; i < kMaxSystemPowerStates; i++) {
    mapping[i].dev_state = DevicePowerState::kDevicePowerStateD2;
    mapping[i].wakeup_enable = false;
  }

  auto update_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))
                           ->UpdatePowerStateMapping(mapping);
  ASSERT_OK(update_result.status());
  zx_status_t call_status = ZX_OK;
  if (update_result->result.is_err()) {
    call_status = update_result->result.err();
  }
  ASSERT_OK(call_status);

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  auto auto_suspend_result =
      fidl::WireCall<Controller>(zx::unowned(child2_device_handle))
          ->ConfigureAutoSuspend(true, DevicePowerState::kDevicePowerStateD2);
  ASSERT_OK(auto_suspend_result.status());
  const auto &auto_suspend_response = auto_suspend_result.value();
  ASSERT_OK(auto_suspend_response.status);

  // Verify systemsuspend overrides autosuspend
  ASSERT_NE(devmgr.component_lifecycle_svc().channel(), ZX_HANDLE_INVALID);
  auto result = fidl::WireCall<lifecycle_fidl::Lifecycle>(devmgr.component_lifecycle_svc())->Stop();
  ASSERT_OK(result.status());

  // Wait till child2's suspend event is called.
  WaitForDeviceSuspendCompletion(zx::unowned(child2_device_handle));

  auto child_dev_suspend_response =
      fidl::WireCall<TestDevice>(zx::unowned(child2_device_handle))->GetCurrentDevicePowerState();
  ASSERT_OK(child_dev_suspend_response.status());
  call_status = ZX_OK;
  if (child_dev_suspend_response->result.is_err()) {
    call_status = child_dev_suspend_response->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(child_dev_suspend_response->result.response().cur_state,
            DevicePowerState::kDevicePowerStateD2);

  // Wait till parent's suspend event is called.
  WaitForDeviceSuspendCompletion(zx::unowned(parent_device_handle));
  auto parent_dev_suspend_response =
      fidl::WireCall<TestDevice>(zx::unowned(parent_device_handle))->GetCurrentDevicePowerState();
  ASSERT_OK(parent_dev_suspend_response.status());
  call_status = ZX_OK;
  if (parent_dev_suspend_response->result.is_err()) {
    call_status = parent_dev_suspend_response->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(parent_dev_suspend_response->result.response().cur_state,
            DevicePowerState::kDevicePowerStateD3Cold);
}

TEST_F(PowerTestCase, SelectiveResume_Success) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::kDevicePowerStateD1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  auto suspend_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))
                            ->Suspend(DevicePowerState::kDevicePowerStateD3Cold);
  ASSERT_OK(suspend_result.status());
  const auto &suspend_response = suspend_result.value();
  ASSERT_OK(suspend_response.status);
  ASSERT_EQ(suspend_response.out_state, DevicePowerState::kDevicePowerStateD3Cold);

  auto response2 =
      fidl::WireCall<TestDevice>(zx::unowned(child2_device_handle))->GetCurrentDevicePowerState();
  ASSERT_OK(response2.status());
  zx_status_t call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(response2->result.response().cur_state, DevicePowerState::kDevicePowerStateD3Cold);

  auto resume_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->Resume();
  ASSERT_OK(resume_result.status());

  const auto &resume_response = resume_result.value();
  ASSERT_OK(resume_response.status);
  ASSERT_EQ(resume_response.out_power_state, DevicePowerState::kDevicePowerStateD0);
  ASSERT_EQ(resume_response.out_perf_state, kDevicePerformanceStateP0);

  auto response3 =
      fidl::WireCall<TestDevice>(zx::unowned(child2_device_handle))->GetCurrentDevicePowerState();
  ASSERT_OK(response3.status());
  call_status = ZX_OK;
  if (response3->result.is_err()) {
    call_status = response3->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(response3->result.response().cur_state, DevicePowerState::kDevicePowerStateD0);
}

TEST_F(PowerTestCase, DefaultSystemPowerStatesMapping) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::kDevicePowerStateD1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  const SystemPowerStateInfo *states_mapping;
  auto response2 =
      fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->GetPowerStateMapping();
  ASSERT_OK(response2.status());
  zx_status_t call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_STATUS(call_status, ZX_OK);
  states_mapping = &response2->result.response().mapping[0];

  // Test Default mapping. For now, the default device power state is D3COLD and
  // wakeup_enable is false.
  for (size_t i = 0; i < kMaxSystemPowerStates; i++) {
    ASSERT_EQ(states_mapping[i].dev_state, DevicePowerState::kDevicePowerStateD3Cold);
    ASSERT_FALSE(states_mapping[i].wakeup_enable);
  }
}

TEST_F(PowerTestCase, UpdatePowerStatesMapping_UnsupportedDeviceState) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::kDevicePowerStateD1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  ::fidl::Array<SystemPowerStateInfo, kMaxSystemPowerStates> mapping{};
  for (size_t i = 0; i < kMaxSystemPowerStates; i++) {
    mapping[i].dev_state = DevicePowerState::kDevicePowerStateD2;
    mapping[i].wakeup_enable = false;
  }

  auto update_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))
                           ->UpdatePowerStateMapping(mapping);
  ASSERT_OK(update_result.status());
  zx_status_t call_status = ZX_OK;
  if (update_result->result.is_err()) {
    call_status = update_result->result.err();
  }
  ASSERT_EQ(call_status, ZX_ERR_INVALID_ARGS);

  const SystemPowerStateInfo *states_mapping;
  auto response2 =
      fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->GetPowerStateMapping();
  ASSERT_OK(response2.status());
  call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_STATUS(call_status, ZX_OK);
  states_mapping = &response2->result.response().mapping[0];

  ASSERT_EQ(states_mapping[static_cast<uint8_t>(SystemPowerState::kReboot)].dev_state,
            DevicePowerState::kDevicePowerStateD3Cold);
  ASSERT_FALSE(states_mapping[static_cast<uint8_t>(SystemPowerState::kReboot)].wakeup_enable);
}

TEST_F(PowerTestCase, UpdatePowerStatesMapping_UnsupportedWakeConfig) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::kDevicePowerStateD1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[1].wakeup_capable = false;
  states[2].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  ::fidl::Array<SystemPowerStateInfo, kMaxSystemPowerStates> mapping{};
  for (size_t i = 0; i < kMaxSystemPowerStates; i++) {
    mapping[i].dev_state = DevicePowerState::kDevicePowerStateD1;
    mapping[i].wakeup_enable = true;
  }

  auto update_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))
                           ->UpdatePowerStateMapping(mapping);
  ASSERT_OK(update_result.status());
  zx_status_t call_status = ZX_OK;
  if (update_result->result.is_err()) {
    call_status = update_result->result.err();
  }
  ASSERT_EQ(call_status, ZX_ERR_INVALID_ARGS);

  const SystemPowerStateInfo *states_mapping;
  auto response2 =
      fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->GetPowerStateMapping();
  ASSERT_OK(response2.status());
  call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_STATUS(call_status, ZX_OK);
  states_mapping = &response2->result.response().mapping[0];

  ASSERT_EQ(states_mapping[static_cast<uint8_t>(SystemPowerState::kReboot)].dev_state,
            DevicePowerState::kDevicePowerStateD3Cold);
  ASSERT_FALSE(states_mapping[static_cast<uint8_t>(SystemPowerState::kReboot)].wakeup_enable);
}

TEST_F(PowerTestCase, UpdatePowerStatesMapping_Success) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::kDevicePowerStateD1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  ::fidl::Array<SystemPowerStateInfo, kMaxSystemPowerStates> mapping{};
  for (size_t i = 0; i < kMaxSystemPowerStates; i++) {
    mapping[i].dev_state = DevicePowerState::kDevicePowerStateD1;
    mapping[i].wakeup_enable = false;
  }

  auto update_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))
                           ->UpdatePowerStateMapping(mapping);
  ASSERT_OK(update_result.status());
  zx_status_t call_status = ZX_OK;
  if (update_result->result.is_err()) {
    call_status = update_result->result.err();
  }
  ASSERT_OK(call_status);

  const SystemPowerStateInfo *states_mapping;
  auto response2 =
      fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->GetPowerStateMapping();
  ASSERT_OK(response2.status());
  call_status = ZX_OK;
  if (response2->result.is_err()) {
    call_status = response2->result.err();
  }
  ASSERT_STATUS(call_status, ZX_OK);
  states_mapping = &response2->result.response().mapping[0];

  ASSERT_EQ(states_mapping[static_cast<uint8_t>(SystemPowerState::kReboot)].dev_state,
            DevicePowerState::kDevicePowerStateD1);
  ASSERT_FALSE(states_mapping[static_cast<uint8_t>(SystemPowerState::kReboot)].wakeup_enable);
}

TEST_F(PowerTestCase, SystemSuspend_SuspendReasonReboot) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::kDevicePowerStateD2;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  ::fidl::Array<SystemPowerStateInfo, kMaxSystemPowerStates> mapping{};
  for (size_t i = 0; i < kMaxSystemPowerStates; i++) {
    mapping[i].dev_state = DevicePowerState::kDevicePowerStateD2;
    mapping[i].wakeup_enable = false;
  }

  auto update_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))
                           ->UpdatePowerStateMapping(mapping);
  ASSERT_OK(update_result.status());
  zx_status_t call_status = ZX_OK;
  if (update_result->result.is_err()) {
    call_status = update_result->result.err();
  }
  ASSERT_OK(call_status);
  SetTerminationSystemState(SystemPowerState::kReboot);

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  ASSERT_NE(devmgr.component_lifecycle_svc().channel(), ZX_HANDLE_INVALID);
  auto result = fidl::WireCall<lifecycle_fidl::Lifecycle>(devmgr.component_lifecycle_svc())->Stop();
  ASSERT_OK(result.status());

  // Wait till child2's suspend event is called.
  WaitForDeviceSuspendCompletion(zx::unowned(child2_device_handle));

  auto child_dev_suspend_response =
      fidl::WireCall<TestDevice>(zx::unowned(child2_device_handle))->GetCurrentDevicePowerState();
  ASSERT_OK(child_dev_suspend_response.status());
  call_status = ZX_OK;
  if (child_dev_suspend_response->result.is_err()) {
    call_status = child_dev_suspend_response->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(child_dev_suspend_response->result.response().cur_state,
            DevicePowerState::kDevicePowerStateD2);

  // Verify that the suspend reason is received correctly
  auto suspend_reason_response =
      fidl::WireCall<TestDevice>(zx::unowned(child2_device_handle))->GetCurrentSuspendReason();
  ASSERT_OK(suspend_reason_response.status());
  call_status = ZX_OK;
  if (suspend_reason_response->result.is_err()) {
    call_status = suspend_reason_response->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(suspend_reason_response->result.response().cur_suspend_reason,
            DEVICE_SUSPEND_REASON_REBOOT);

  // Wait till parent's suspend event is called.
  WaitForDeviceSuspendCompletion(zx::unowned(parent_device_handle));

  auto parent_dev_suspend_response =
      fidl::WireCall<TestDevice>(zx::unowned(parent_device_handle))->GetCurrentDevicePowerState();
  ASSERT_OK(parent_dev_suspend_response.status());
  call_status = ZX_OK;
  if (parent_dev_suspend_response->result.is_err()) {
    call_status = parent_dev_suspend_response->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(parent_dev_suspend_response->result.response().cur_state,
            DevicePowerState::kDevicePowerStateD3Cold);
}

TEST_F(PowerTestCase, SystemSuspend_SuspendReasonRebootRecovery) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::kDevicePowerStateD2;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  ::fidl::Array<SystemPowerStateInfo, kMaxSystemPowerStates> mapping{};
  for (size_t i = 0; i < kMaxSystemPowerStates; i++) {
    mapping[i].dev_state = DevicePowerState::kDevicePowerStateD2;
    mapping[i].wakeup_enable = false;
  }

  auto update_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))
                           ->UpdatePowerStateMapping(mapping);
  ASSERT_OK(update_result.status());
  zx_status_t call_status = ZX_OK;
  if (update_result->result.is_err()) {
    call_status = update_result->result.err();
  }
  ASSERT_OK(call_status);

  SetTerminationSystemState(SystemPowerState::kRebootRecovery);

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  ASSERT_NE(devmgr.component_lifecycle_svc().channel(), ZX_HANDLE_INVALID);
  auto result = fidl::WireCall<lifecycle_fidl::Lifecycle>(devmgr.component_lifecycle_svc())->Stop();
  ASSERT_OK(result.status());

  // Wait till child2's suspend event is called.
  WaitForDeviceSuspendCompletion(zx::unowned(child2_device_handle));

  auto child_dev_suspend_response =
      fidl::WireCall<TestDevice>(zx::unowned(child2_device_handle))->GetCurrentDevicePowerState();
  ASSERT_OK(child_dev_suspend_response.status());
  call_status = ZX_OK;
  if (child_dev_suspend_response->result.is_err()) {
    call_status = child_dev_suspend_response->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(child_dev_suspend_response->result.response().cur_state,
            DevicePowerState::kDevicePowerStateD2);

  auto suspend_reason_response =
      fidl::WireCall<TestDevice>(zx::unowned(child2_device_handle))->GetCurrentSuspendReason();
  ASSERT_OK(suspend_reason_response.status());
  call_status = ZX_OK;
  if (suspend_reason_response->result.is_err()) {
    call_status = suspend_reason_response->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(suspend_reason_response->result.response().cur_suspend_reason,
            DEVICE_SUSPEND_REASON_REBOOT_RECOVERY);

  // Wait till parent's suspend event is called.
  WaitForDeviceSuspendCompletion(zx::unowned(parent_device_handle));
  auto parent_dev_suspend_response =
      fidl::WireCall<TestDevice>(zx::unowned(parent_device_handle))->GetCurrentDevicePowerState();
  ASSERT_OK(parent_dev_suspend_response.status());
  call_status = ZX_OK;
  if (parent_dev_suspend_response->result.is_err()) {
    call_status = parent_dev_suspend_response->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(parent_dev_suspend_response->result.response().cur_state,
            DevicePowerState::kDevicePowerStateD3Cold);
}

TEST_F(PowerTestCase, SelectiveResume_AfterSetPerformanceState) {
  // Add Capabilities
  DevicePowerStateInfo states[2];
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[1].is_supported = true;

  DevicePerformanceStateInfo perf_states[3];
  perf_states[0].state_id = kDevicePerformanceStateP0;
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
      fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->SetPerformanceState(1);
  ASSERT_OK(perf_change_result.status());
  const auto &perf_change_response = perf_change_result.value();
  ASSERT_OK(perf_change_response.status);
  ASSERT_EQ(perf_change_response.out_state, 1);

  auto response2 =
      fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->GetCurrentPerformanceState();
  ASSERT_OK(response2.status());
  ASSERT_EQ(response2->out_state, 1);

  // Suspend and resume the device. Test if device resumes to saved performance state.
  auto suspend_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))
                            ->Suspend(DevicePowerState::kDevicePowerStateD3Cold);
  ASSERT_OK(suspend_result.status());
  const auto &suspend_response = suspend_result.value();
  ASSERT_OK(suspend_response.status);
  ASSERT_EQ(suspend_response.out_state, DevicePowerState::kDevicePowerStateD3Cold);

  auto response3 =
      fidl::WireCall<TestDevice>(zx::unowned(child2_device_handle))->GetCurrentDevicePowerState();
  ASSERT_OK(response3.status());
  zx_status_t call_status = ZX_OK;
  if (response3->result.is_err()) {
    call_status = response3->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(response3->result.response().cur_state, DevicePowerState::kDevicePowerStateD3Cold);

  // Resume
  auto resume_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->Resume();
  ASSERT_OK(resume_result.status());

  const auto &resume_response = resume_result.value();
  ASSERT_OK(resume_response.status);
  ASSERT_EQ(resume_response.out_power_state, DevicePowerState::kDevicePowerStateD0);
  ASSERT_EQ(resume_response.out_perf_state, 1);
}

TEST_F(PowerTestCase, SelectiveResume_FailedToResumeToWorking) {
  // Add Capabilities
  DevicePowerStateInfo states[2];
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[1].is_supported = true;

  DevicePerformanceStateInfo perf_states[3];
  perf_states[0].state_id = kDevicePerformanceStateP0;
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
      fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->SetPerformanceState(1);
  ASSERT_OK(perf_change_result.status());
  const auto &perf_change_response = perf_change_result.value();
  ASSERT_OK(perf_change_response.status);
  ASSERT_EQ(perf_change_response.out_state, 1);

  auto response2 =
      fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->GetCurrentPerformanceState();
  ASSERT_OK(response2.status());
  ASSERT_EQ(response2->out_state, 1);

  // Suspend
  auto suspend_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))
                            ->Suspend(DevicePowerState::kDevicePowerStateD3Cold);
  ASSERT_OK(suspend_result.status());
  const auto &suspend_response = suspend_result.value();
  ASSERT_OK(suspend_response.status);
  ASSERT_EQ(suspend_response.out_state, DevicePowerState::kDevicePowerStateD3Cold);

  auto response3 =
      fidl::WireCall<TestDevice>(zx::unowned(child2_device_handle))->GetCurrentDevicePowerState();
  ASSERT_OK(response3.status());
  zx_status_t call_status = ZX_OK;
  if (response3->result.is_err()) {
    call_status = response3->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(response3->result.response().cur_state, DevicePowerState::kDevicePowerStateD3Cold);

  fuchsia_device_power_test::wire::TestStatusInfo info;
  info.resume_status = ZX_ERR_IO;
  info.out_power_state = static_cast<uint8_t>(DevicePowerState::kDevicePowerStateD3Cold);
  info.out_performance_state = 1;
  auto response4 =
      fidl::WireCall<TestDevice>(zx::unowned(child2_device_handle))->SetTestStatusInfo(info);
  call_status = ZX_OK;
  ASSERT_OK(response4.status());
  if (response4->result.is_err()) {
    call_status = response4->result.err();
  }
  ASSERT_OK(call_status);

  // Resume
  auto resume_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->Resume();
  ASSERT_OK(resume_result.status());

  const auto &resume_response = resume_result.value();
  ASSERT_EQ(resume_response.status, info.resume_status);
  ASSERT_EQ(static_cast<uint8_t>(resume_response.out_power_state), info.out_power_state);
}

TEST_F(PowerTestCase, SelectiveResume_FailedToResumeToPerformanceState) {
  // Add Capabilities
  DevicePowerStateInfo states[2];
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[1].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[1].is_supported = true;

  DevicePerformanceStateInfo perf_states[3];
  perf_states[0].state_id = kDevicePerformanceStateP0;
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
      fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->SetPerformanceState(1);
  ASSERT_OK(perf_change_result.status());
  const auto &perf_change_response = perf_change_result.value();
  ASSERT_OK(perf_change_response.status);
  ASSERT_EQ(perf_change_response.out_state, 1);

  auto response2 =
      fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->GetCurrentPerformanceState();
  ASSERT_OK(response2.status());
  ASSERT_EQ(response2->out_state, 1);

  // Suspend
  auto suspend_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))
                            ->Suspend(DevicePowerState::kDevicePowerStateD3Cold);
  ASSERT_OK(suspend_result.status());
  const auto &suspend_response = suspend_result.value();
  ASSERT_OK(suspend_response.status);
  ASSERT_EQ(suspend_response.out_state, DevicePowerState::kDevicePowerStateD3Cold);

  auto response3 =
      fidl::WireCall<TestDevice>(zx::unowned(child2_device_handle))->GetCurrentDevicePowerState();
  ASSERT_OK(response3.status());
  zx_status_t call_status = ZX_OK;
  if (response3->result.is_err()) {
    call_status = response3->result.err();
  }
  ASSERT_OK(call_status);
  ASSERT_EQ(response3->result.response().cur_state, DevicePowerState::kDevicePowerStateD3Cold);

  fuchsia_device_power_test::wire::TestStatusInfo info;
  info.resume_status = ZX_ERR_IO;
  info.out_power_state = static_cast<uint8_t>(DevicePowerState::kDevicePowerStateD0);
  // The previous performance_state set was 1.
  info.out_performance_state = 2;
  auto response4 =
      fidl::WireCall<TestDevice>(zx::unowned(child2_device_handle))->SetTestStatusInfo(info);
  call_status = ZX_OK;
  ASSERT_OK(response4.status());
  if (response4->result.is_err()) {
    call_status = response4->result.err();
  }
  ASSERT_OK(call_status);

  // Resume
  auto resume_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->Resume();
  ASSERT_OK(resume_result.status());

  const auto &resume_response = resume_result.value();
  ASSERT_EQ(resume_response.status, info.resume_status);
  ASSERT_EQ(static_cast<uint8_t>(resume_response.out_power_state), info.out_power_state);
  ASSERT_EQ(resume_response.out_perf_state, info.out_performance_state);

  // The performance state has to be updated to the state that the device resumed to.
  auto response5 =
      fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->GetCurrentPerformanceState();
  ASSERT_OK(response5.status());
  ASSERT_EQ(response5->out_state, info.out_performance_state);
}

TEST_F(PowerTestCase, DeviceResume_AutoSuspendEnabled) {
  // Add Capabilities
  DevicePowerStateInfo states[3];
  states[0].state_id = DevicePowerState::kDevicePowerStateD0;
  states[0].is_supported = true;
  states[0].restore_latency = 0;
  states[1].state_id = DevicePowerState::kDevicePowerStateD1;
  states[1].is_supported = true;
  states[1].restore_latency = 100;
  states[2].state_id = DevicePowerState::kDevicePowerStateD3Cold;
  states[2].is_supported = true;
  states[2].restore_latency = 1000;
  AddChildWithPowerArgs(states, std::size(states), nullptr, 0);

  {
    auto auto_suspend_result =
        fidl::WireCall<Controller>(zx::unowned(child2_device_handle))
            ->ConfigureAutoSuspend(true, DevicePowerState::kDevicePowerStateD1);
    ASSERT_OK(auto_suspend_result.status());
    const auto &auto_suspend_response = auto_suspend_result.value();
    ASSERT_OK(auto_suspend_response.status);
  }

  {
    auto response = fidl::WireCall<TestDevice>(zx::unowned(child2_device_handle))
                        ->GetCurrentDeviceAutoSuspendConfig();
    ASSERT_OK(response.status());
    zx_status_t call_status = ZX_OK;
    if (response->result.is_err()) {
      call_status = response->result.err();
    }
    ASSERT_OK(call_status);
    ASSERT_EQ(response->result.response().enabled, true);
    ASSERT_EQ(response->result.response().deepest_sleep_state,
              DevicePowerState::kDevicePowerStateD1);
  }

  {
    auto resume_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->Resume();
    ASSERT_OK(resume_result.status());
    // Device resume is not supported when auto suspend is configured.
    ASSERT_EQ(resume_result.value().status, ZX_ERR_NOT_SUPPORTED);
  }

  // Disable autosuspend and try again
  {
    auto auto_suspend_result =
        fidl::WireCall<Controller>(zx::unowned(child2_device_handle))
            ->ConfigureAutoSuspend(false, DevicePowerState::kDevicePowerStateD0);
    ASSERT_OK(auto_suspend_result.status());
    auto &auto_suspend_response = auto_suspend_result.value();
    ASSERT_OK(auto_suspend_response.status);
  }

  {
    auto suspend_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))
                              ->Suspend(DevicePowerState::kDevicePowerStateD3Cold);
    ASSERT_OK(suspend_result.status());
    ASSERT_OK(suspend_result.value().status);
  }

  {
    auto resume_result = fidl::WireCall<Controller>(zx::unowned(child2_device_handle))->Resume();
    ASSERT_OK(resume_result.status());
    ASSERT_OK(resume_result.value().status);
  }
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/power/test/llcpp/fidl.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/alloc_checker.h>

using llcpp::fuchsia::device::DevicePerformanceStateInfo;
using llcpp::fuchsia::device::DevicePowerState;
using llcpp::fuchsia::device::DevicePowerStateInfo;
using llcpp::fuchsia::device::power::test::TestDevice;

class TestPowerDriver;
using DeviceType =
    ddk::Device<TestPowerDriver, ddk::UnbindableNew, ddk::Suspendable, ddk::Messageable>;
class TestPowerDriver : public DeviceType,
                        public ddk::EmptyProtocol<ZX_PROTOCOL_TEST_POWER_CHILD>,
                        public TestDevice::Interface {
 public:
  TestPowerDriver(zx_device_t* parent) : DeviceType(parent) {}
  zx_status_t Bind();
  void DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }
  zx_status_t DdkSuspend(uint32_t flags) {
    // Set current_power_state to indicate that the suspend is called.
    current_power_state_ = DevicePowerState::DEVICE_POWER_STATE_D1;
    return ZX_OK;
  }
  void AddDeviceWithPowerArgs(::fidl::VectorView<DevicePowerStateInfo> info,
                              ::fidl::VectorView<DevicePerformanceStateInfo> perf_states,
                              bool add_invisible,
                              AddDeviceWithPowerArgsCompleter::Sync completer) override;

  void GetCurrentDevicePowerState(GetCurrentDevicePowerStateCompleter::Sync completer) override;
  void GetCurrentDevicePerformanceState(
      GetCurrentDevicePerformanceStateCompleter::Sync completer) override;
  void GetCurrentDeviceAutoSuspendConfig(
      GetCurrentDeviceAutoSuspendConfigCompleter::Sync completer) override;
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    DdkTransaction transaction(txn);
    ::llcpp::fuchsia::device::power::test::TestDevice::Dispatch(this, msg, &transaction);
    return transaction.Status();
  }

 private:
  DevicePowerState current_power_state_ = DevicePowerState::DEVICE_POWER_STATE_D0;
  uint32_t current_performance_state_ = 0;
  bool auto_suspend_enabled_ = false;
  DevicePowerState deepest_autosuspend_sleep_state_ = DevicePowerState::DEVICE_POWER_STATE_D0;
};

zx_status_t TestPowerDriver::Bind() { return DdkAdd("power-test"); }

void TestPowerDriver::AddDeviceWithPowerArgs(
    ::fidl::VectorView<DevicePowerStateInfo> info,
    ::fidl::VectorView<DevicePerformanceStateInfo> perf_states,
    bool add_invisible,
    AddDeviceWithPowerArgsCompleter::Sync completer) {
  ::llcpp::fuchsia::device::power::test::TestDevice_AddDeviceWithPowerArgs_Result response;
  response.set_err(ZX_ERR_NOT_SUPPORTED);
  completer.Reply(std::move(response));
}

void TestPowerDriver::GetCurrentDevicePowerState(
    GetCurrentDevicePowerStateCompleter::Sync completer) {
  ::llcpp::fuchsia::device::power::test::TestDevice_GetCurrentDevicePowerState_Result result;
  result.set_response(
      llcpp::fuchsia::device::power::test::TestDevice_GetCurrentDevicePowerState_Response{
          .cur_state = current_power_state_,
      });

  completer.Reply(std::move(result));
}

void TestPowerDriver::GetCurrentDevicePerformanceState(
    GetCurrentDevicePerformanceStateCompleter::Sync completer) {
  ::llcpp::fuchsia::device::power::test::TestDevice_GetCurrentDevicePerformanceState_Result result;
  result.set_response(
      llcpp::fuchsia::device::power::test::TestDevice_GetCurrentDevicePerformanceState_Response{
          .cur_state = static_cast<int32_t>(current_performance_state_),
      });

  completer.Reply(std::move(result));
}

void TestPowerDriver::GetCurrentDeviceAutoSuspendConfig(
    GetCurrentDeviceAutoSuspendConfigCompleter::Sync completer) {
  ::llcpp::fuchsia::device::power::test::TestDevice_GetCurrentDeviceAutoSuspendConfig_Result result;
  result.set_response(
      llcpp::fuchsia::device::power::test::TestDevice_GetCurrentDeviceAutoSuspendConfig_Response{
          .enabled = auto_suspend_enabled_,
          .deepest_sleep_state = static_cast<llcpp::fuchsia::device::DevicePowerState>(
              deepest_autosuspend_sleep_state_),
      });

  completer.Reply(std::move(result));
}

zx_status_t test_power_hook_bind(void* ctx, zx_device_t* device) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<TestPowerDriver>(&ac, device);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto status = dev->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

static zx_driver_ops_t test_power_hook_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = test_power_hook_bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(TestPower, test_power_hook_driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_POWER_TEST),
ZIRCON_DRIVER_END(TestPower)
    // clang-format on

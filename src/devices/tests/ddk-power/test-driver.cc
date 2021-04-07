// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/power/test/llcpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/alloc_checker.h>

#include "src/devices/tests/ddk-power/test-power-bind.h"

using fuchsia_device::wire::DevicePerformanceStateInfo;
using fuchsia_device::wire::DevicePowerState;
using fuchsia_device::wire::DevicePowerStateInfo;
using fuchsia_device_power_test::TestDevice;

class TestPowerDriver;
using DeviceType =
    ddk::Device<TestPowerDriver, ddk::Unbindable, ddk::Suspendable, ddk::Messageable>;
class TestPowerDriver : public DeviceType,
                        public ddk::EmptyProtocol<ZX_PROTOCOL_TEST_POWER_CHILD>,
                        public fidl::WireInterface<TestDevice> {
 public:
  TestPowerDriver(zx_device_t* parent) : DeviceType(parent) {}
  zx_status_t Bind();
  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }
  void DdkSuspend(ddk::SuspendTxn txn) {
    current_power_state_ = static_cast<DevicePowerState>(txn.requested_state());
    suspend_complete_event_.signal(0, ZX_USER_SIGNAL_0);
    txn.Reply(ZX_OK, txn.requested_state());
  }
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
    DdkTransaction transaction(txn);
    fidl::WireDispatch<fuchsia_device_power_test::TestDevice>(this, msg, &transaction);
    return transaction.Status();
  }

  void GetSuspendCompletionEvent(GetSuspendCompletionEventCompleter::Sync& completer) override {
    zx::event complete;
    zx_status_t status =
        suspend_complete_event_.duplicate(ZX_RIGHT_WAIT | ZX_RIGHT_TRANSFER, &complete);
    if (status != ZX_OK) {
      completer.ReplyError(status);
    } else {
      completer.ReplySuccess(std::move(complete));
    }
  }

  void AddDeviceWithPowerArgs(::fidl::VectorView<DevicePowerStateInfo> info,
                              ::fidl::VectorView<DevicePerformanceStateInfo> perf_states,
                              bool add_invisible,
                              AddDeviceWithPowerArgsCompleter::Sync& completer) override;

  void GetCurrentDevicePowerState(GetCurrentDevicePowerStateCompleter::Sync& completer) override;
  void GetCurrentSuspendReason(GetCurrentSuspendReasonCompleter::Sync& completer) override;
  void GetCurrentDeviceAutoSuspendConfig(
      GetCurrentDeviceAutoSuspendConfigCompleter::Sync& completer) override;
  void SetTestStatusInfo(fuchsia_device_power_test::wire::TestStatusInfo status_info,
                         SetTestStatusInfoCompleter::Sync& completer) override;

 private:
  DevicePowerState current_power_state_ = DevicePowerState::DEVICE_POWER_STATE_D0;
  bool auto_suspend_enabled_ = false;
  DevicePowerState deepest_autosuspend_sleep_state_ = DevicePowerState::DEVICE_POWER_STATE_D0;
  zx_status_t reply_suspend_status_ = ZX_OK;
  zx_status_t reply_resume_status_ = ZX_OK;
  zx::event suspend_complete_event_;
};

zx_status_t TestPowerDriver::Bind() {
  zx_status_t status = zx::event::create(0, &suspend_complete_event_);
  if (status != ZX_OK) {
    return status;
  }
  return DdkAdd("power-test");
}

void TestPowerDriver::AddDeviceWithPowerArgs(
    ::fidl::VectorView<DevicePowerStateInfo> info,
    ::fidl::VectorView<DevicePerformanceStateInfo> perf_states, bool add_invisible,
    AddDeviceWithPowerArgsCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void TestPowerDriver::GetCurrentDevicePowerState(
    GetCurrentDevicePowerStateCompleter::Sync& completer) {
  completer.ReplySuccess(current_power_state_);
}

void TestPowerDriver::GetCurrentDeviceAutoSuspendConfig(
    GetCurrentDeviceAutoSuspendConfigCompleter::Sync& completer) {
  completer.ReplySuccess(auto_suspend_enabled_,
                         static_cast<DevicePowerState>(deepest_autosuspend_sleep_state_));
}
void TestPowerDriver::SetTestStatusInfo(fuchsia_device_power_test::wire::TestStatusInfo status_info,
                                        SetTestStatusInfoCompleter::Sync& completer) {
  reply_suspend_status_ = status_info.suspend_status;
  reply_resume_status_ = status_info.resume_status;
  completer.ReplySuccess();
}

void TestPowerDriver::GetCurrentSuspendReason(GetCurrentSuspendReasonCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
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

ZIRCON_DRIVER(TestPower, test_power_hook_driver_ops, "zircon", "0.1");

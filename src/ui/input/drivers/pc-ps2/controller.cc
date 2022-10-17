// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "controller.h"

#include <lib/ddk/driver.h>
#include <lib/ddk/hw/inout.h>
#include <lib/fit/defer.h>
#include <lib/zx/time.h>

#include "src/ui/input/drivers/pc-ps2/commands.h"
#include "src/ui/input/drivers/pc-ps2/device.h"
#include "src/ui/input/drivers/pc-ps2/i8042_bind.h"
#include "src/ui/input/drivers/pc-ps2/registers.h"

#ifdef PS2_TEST
uint8_t TEST_inp(uint16_t port);
void TEST_outp(uint16_t port, uint8_t data);
#define inp TEST_inp
#define outp TEST_outp
#endif  // PS2_TEST

namespace i8042 {

// Delay between checking status register for in/out buf full, in microseconds.
constexpr zx::duration kStatusPollDelay = zx::usec(10);
// Number of |kStatusPollDelayUs| to wait before giving up.
constexpr size_t kStatusPollTimeout = 500;

// Maximum number of bytes we need to read to flush the internal i8042 buffer.
constexpr size_t kMaxBufferLength = 32;

zx_status_t Controller::Bind(void *ctx, zx_device_t *parent) {
  auto dev = std::make_unique<Controller>(parent);

  zx_status_t status = dev->DdkAdd(ddk::DeviceAddArgs("i8042").set_flags(DEVICE_ADD_NON_BINDABLE));
  if (status == ZX_OK) {
    // The DDK will manage our memory.
    __UNUSED auto unused = dev.release();
  }
  return status;
}

void Controller::DdkInit(ddk::InitTxn txn) {
  init_thread_ = std::thread([this, txn = std::move(txn)]() mutable {
    zx_status_t status;
    auto cancel = fit::defer([&txn, &status]() {
      zxlogf(ERROR, "init status: %s", zx_status_get_string(status));
      txn.Reply(status);
    });

    // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
    if (get_root_resource() != ZX_HANDLE_INVALID) {
      // TODO(simonshields): We should use ACPI to get these resources.
      // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
      status = zx_ioports_request(get_root_resource(), kCommandReg, 1);
      if (status != ZX_OK) {
        return;
      }
      // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
      status = zx_ioports_request(get_root_resource(), kDataReg, 1);
      if (status != ZX_OK) {
        return;
      }
    }

    // First, disable both devices, and flush to get the hardware back to a known-good state.
    auto ctrl_status = SendControllerCommand(kCmdPort1Disable, {});
    if (ctrl_status.is_error()) {
      status = ctrl_status.status_value();
      zxlogf(INFO, "Port 1 disable failed: %s", ctrl_status.status_string());
      return;
    }
    ctrl_status = SendControllerCommand(kCmdPort2Disable, {});
    if (ctrl_status.is_error()) {
      zxlogf(INFO, "Port 2 disable failed: %s", ctrl_status.status_string());
      status = ctrl_status.status_value();
      return;
    }
    Flush();

    auto cfg = SendControllerCommand(kCmdReadCtl, {});
    if (cfg.is_error() || cfg->empty()) {
      status = cfg.is_error() ? cfg.status_value() : ZX_ERR_IO;
      zxlogf(INFO, "reading Control failed: %s", zx_status_get_string(status));
      return;
    }

    ControlReg ctrl;
    ctrl.set_reg_value(cfg.value()[0]);
    if (ctrl.auxdis()) {
      zxlogf(INFO, "Second port present!");
      // We have a second port.
      has_port2_ = true;
    }

    ctrl.set_kbdint(0).set_auxint(0).set_xlate(0);
    ctrl_status =
        SendControllerCommand(kCmdWriteCtl, cpp20::span<uint8_t>(ctrl.reg_value_ptr(), 1));
    if (ctrl_status.is_error()) {
      zxlogf(INFO, "Writing control failed: %s", zx_status_get_string(status));
      status = ctrl_status.status_value();
      return;
    }

    auto test_result = SendControllerCommand(kCmdSelfTest, {});
    if (test_result.is_error() || test_result->empty()) {
      status = test_result.is_error() ? test_result.status_value() : ZX_ERR_IO;
      zxlogf(INFO, "Send self-test failed: %s", zx_status_get_string(status));
      return;
    }
    uint8_t data = test_result.value()[0];
    if (data != 0x55) {
      zxlogf(ERROR, "Controller self-test failed: 0x%0x", data);
      status = ZX_ERR_INTERNAL;
      return;
    }

    test_result = SendControllerCommand(kCmdPort1Test, {});
    if (test_result.is_error() || test_result->empty()) {
      status = test_result.is_error() ? test_result.status_value() : ZX_ERR_IO;
      zxlogf(INFO, "Send port 1 self-test failed: %s", zx_status_get_string(status));
      return;
    }
    data = test_result.value()[0];
    if (data != 0x00) {
      zxlogf(ERROR, "Port 1 self-test failed: 0x%0x", data);
      status = ZX_ERR_INTERNAL;
      return;
    }

    if (has_port2_) {
      test_result = SendControllerCommand(kCmdPort2Test, {});
      if (test_result.is_error() || test_result->empty()) {
        status = test_result.is_error() ? test_result.status_value() : ZX_ERR_IO;
        zxlogf(INFO, "Send port 2 self-test failed: %s, disabling", zx_status_get_string(status));
        has_port2_ = false;
      }
    }
    if (has_port2_) {
      data = test_result.value()[0];
      if (data != 0x00) {
        zxlogf(ERROR, "Port 2 self-test failed: 0x%0x", data);
        has_port2_ = false;
      }
    }

    // Turn on translation, and re-enable the devices.
    ctrl.set_xlate(true);
    ctrl.set_kbddis(false).set_kbdint(true);
    if (has_port2_) {
      ctrl.set_auxdis(false).set_auxint(true);
    }

    ctrl_status =
        SendControllerCommand(kCmdWriteCtl, cpp20::span<uint8_t>(ctrl.reg_value_ptr(), 1));
    if (ctrl_status.is_error()) {
      status = ctrl_status.status_value();
      zxlogf(INFO, "Re-enabling devices failed: %s", zx_status_get_string(status));
      return;
    }

    cancel.cancel();
    txn.Reply(ZX_OK);

    // Failure here won't fail everything else.
    zx_status_t bind_status = I8042Device::Bind(this, Port::kPort1);
    if (bind_status != ZX_OK) {
      zxlogf(WARNING, "Failed to bind Port 1: %s", zx_status_get_string(bind_status));
    }

    if (has_port2_) {
      bind_status = I8042Device::Bind(this, Port::kPort2);
      if (bind_status != ZX_OK) {
        zxlogf(WARNING, "Failed to bind Port 2: %s", zx_status_get_string(bind_status));
      }
    }

    sync_completion_signal(&added_children_);
  });
}

zx::result<std::vector<uint8_t>> Controller::SendControllerCommand(
    Command command, cpp20::span<const uint8_t> data) {
  if (data.size() != command.param_count) {
    zxlogf(ERROR, "%s: Wrong parameter count: wanted %u, got %zu", __func__, command.param_count,
           data.size());
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  if (!WaitWrite()) {
    return zx::error(ZX_ERR_TIMED_OUT);
  }
  outp(kCommandReg, command.cmd);

  // Write parameters.
  for (size_t i = 0; i < command.param_count; i++) {
    if (!WaitWrite()) {
      return zx::error(ZX_ERR_TIMED_OUT);
    }
    outp(kDataReg, data[i]);
  }

  // Read back result.
  std::vector<uint8_t> ret;
  ret.reserve(command.response_count);
  for (size_t i = 0; i < command.response_count; i++) {
    if (!WaitRead()) {
      zxlogf(INFO, "%s: timeout reading response, got %zu bytes", __func__, i);
      break;
    }

    ret.emplace_back(ReadData());
  }

  return zx::ok(std::move(ret));
}

zx::result<std::vector<uint8_t>> Controller::SendDeviceCommand(Command command, Port port) {
  if (command.param_count != 0) {
    zxlogf(ERROR, "Sending parameters to device not supported.");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  // When writing to port 2, we need to tell the controller we're addressing it.
  if (port == Port::kPort2) {
    if (!WaitWrite()) {
      return zx::error(ZX_ERR_TIMED_OUT);
    }

    outp(kCommandReg, kCmdWriteAux.cmd);
  }

  if (!WaitWrite()) {
    return zx::error(ZX_ERR_TIMED_OUT);
  }
  outp(kDataReg, command.cmd);

  std::vector<uint8_t> ret;
  ret.reserve(command.response_count);
  for (size_t i = 0; i < command.response_count; i++) {
    if (!WaitRead()) {
      zxlogf(DEBUG, "Read failed, got %zu bytes", i);
      break;
    }

    ret.emplace_back(ReadData());
  }

  return zx::ok(ret);
}

StatusReg Controller::ReadStatus() {
  StatusReg reg;
  uint8_t data = inp(kStatusReg);
  reg.set_reg_value(data);
  return reg;
}

uint8_t Controller::ReadData() { return inp(kDataReg); }

bool Controller::WaitWrite() {
  size_t i = 0;
  while (ReadStatus().ibf() && i < kStatusPollTimeout) {
    zx::nanosleep(zx::deadline_after(kStatusPollDelay));
    i++;
  }
  return i != kStatusPollTimeout;
}

bool Controller::WaitRead() {
  size_t i = 0;
  while (!ReadStatus().obf() && i < kStatusPollTimeout) {
    zx::nanosleep(zx::deadline_after(kStatusPollDelay));
    i++;
  }
  return i != kStatusPollTimeout;
}

void Controller::Flush() {
  size_t i = 0;
  while (ReadStatus().obf() && i < kMaxBufferLength) {
    ReadData();
    i++;
    usleep(10);
  }
}

}  // namespace i8042

static zx_driver_ops_t i8042_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = i8042::Controller::Bind,
};

ZIRCON_DRIVER(i8042, i8042_driver_ops, "zircon", "0.1");

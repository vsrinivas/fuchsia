// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/clockimpl/cpp/banjo.h>
#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>

#include <memory>

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>

#include "src/devices/bus/drivers/platform/test/test-clock-bind.h"

#define DRIVER_NAME "test-clock"

class TestClockDevice;
using DeviceType = ddk::Device<TestClockDevice>;

class TestClockDevice : public DeviceType,
                        public ddk::ClockImplProtocol<TestClockDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(zx_device_t* parent);

  explicit TestClockDevice(zx_device_t* parent) : DeviceType(parent) {}

  zx_status_t Create(std::unique_ptr<TestClockDevice>* out);
  zx_status_t Init();

  // Methods required by the ddk mixins
  void DdkRelease();

  zx_status_t ClockImplEnable(uint32_t clock_id);
  zx_status_t ClockImplDisable(uint32_t clock_id);
  zx_status_t ClockImplIsEnabled(uint32_t id, bool* out_enabled);

  zx_status_t ClockImplSetRate(uint32_t id, uint64_t hz);
  zx_status_t ClockImplQuerySupportedRate(uint32_t id, uint64_t max_rate, uint64_t* out_best_rate);
  zx_status_t ClockImplGetRate(uint32_t id, uint64_t* out_current_rate);

  zx_status_t ClockImplSetInput(uint32_t id, uint32_t idx);
  zx_status_t ClockImplGetNumInputs(uint32_t id, uint32_t* out);
  zx_status_t ClockImplGetInput(uint32_t id, uint32_t* out);

 private:
  static constexpr uint32_t kMinClock = 1;
  static constexpr uint32_t kMaxClock = 8;
};

zx_status_t TestClockDevice::Init() {
  pbus_protocol_t pbus;
  auto status = device_get_protocol(parent(), ZX_PROTOCOL_PBUS, &pbus);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_PBUS not available %d", __func__, status);
    return status;
  }
  clock_impl_protocol_t clock_proto = {
      .ops = &clock_impl_protocol_ops_,
      .ctx = this,
  };
  status = pbus_register_protocol(&pbus, ZX_PROTOCOL_CLOCK_IMPL,
                                  reinterpret_cast<uint8_t*>(&clock_proto), sizeof(clock_proto));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s pbus_register_protocol failed %d", __func__, status);
    return status;
  }
  return ZX_OK;
}

zx_status_t TestClockDevice::Create(zx_device_t* parent) {
  auto dev = std::make_unique<TestClockDevice>(parent);
  pdev_protocol_t pdev;
  zx_status_t status;

  zxlogf(INFO, "TestClockDevice::Create: %s", DRIVER_NAME);

  status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not get ZX_PROTOCOL_PDEV", __func__);
    return status;
  }

  status = dev->DdkAdd("test-clock");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed: %d", __func__, status);
    return status;
  }
  // devmgr is now in charge of dev.
  auto ptr = dev.release();

  return ptr->Init();
}

void TestClockDevice::DdkRelease() { delete this; }

zx_status_t TestClockDevice::ClockImplEnable(uint32_t clock_id) {
  if (clock_id < kMinClock || clock_id > kMaxClock) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t TestClockDevice::ClockImplDisable(uint32_t clock_id) {
  if (clock_id < kMinClock || clock_id > kMaxClock) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t TestClockDevice::ClockImplIsEnabled(uint32_t clock_id, bool* out_enabled) {
  if (clock_id < kMinClock || clock_id > kMaxClock) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t TestClockDevice::ClockImplQuerySupportedRate(uint32_t id, uint64_t max_rate,
                                                         uint64_t* out_best_rate) {
  if (id < kMinClock || id > kMaxClock) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t TestClockDevice::ClockImplGetRate(uint32_t id, uint64_t* out_current_rate) {
  if (id < kMinClock || id > kMaxClock) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t TestClockDevice::ClockImplSetRate(uint32_t clock_id, uint64_t hz) {
  if (clock_id < kMinClock || clock_id > kMaxClock) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t TestClockDevice::ClockImplSetInput(uint32_t id, uint32_t idx) {
  if (id < kMinClock || id > kMaxClock) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t TestClockDevice::ClockImplGetNumInputs(uint32_t id, uint32_t* out) {
  if (id < kMinClock || id > kMaxClock) {
    return ZX_ERR_INVALID_ARGS;
  }
  *out = 0;
  return ZX_OK;
}

zx_status_t TestClockDevice::ClockImplGetInput(uint32_t id, uint32_t* out) {
  if (id < kMinClock || id > kMaxClock) {
    return ZX_ERR_INVALID_ARGS;
  }
  *out = 0;
  return ZX_OK;
}

namespace {

zx_status_t test_clock_bind(void* ctx, zx_device_t* parent) {
  return TestClockDevice::Create(parent);
}

constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t driver_ops = {};
  driver_ops.version = DRIVER_OPS_VERSION;
  driver_ops.bind = test_clock_bind;
  return driver_ops;
}();

}  // namespace

ZIRCON_DRIVER(test_clock, driver_ops, "zircon", "0.1");

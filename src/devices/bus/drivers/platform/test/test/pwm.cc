// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <fuchsia/hardware/pwm/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>

#include <cstdlib>
#include <memory>

#include <ddktl/device.h>

#include "src/devices/bus/drivers/platform/test/test-pwm-bind.h"

#define DRIVER_NAME "test-pwm"

namespace pwm {

namespace {

struct mode_config_magic {
  uint32_t magic;
};

struct mode_config {
  uint32_t mode;
  union {
    struct mode_config_magic magic;
  };
};

}  // namespace

class TestPwmDevice;
using DeviceType = ddk::Device<TestPwmDevice>;

class TestPwmDevice : public DeviceType,
                      public ddk::PwmImplProtocol<TestPwmDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(zx_device_t* parent);

  explicit TestPwmDevice(zx_device_t* parent) : DeviceType(parent) {}

  // Methods required by the ddk mixins
  void DdkRelease();

  zx_status_t PwmImplGetConfig(uint32_t idx, pwm_config_t* out_config);
  zx_status_t PwmImplSetConfig(uint32_t idx, const pwm_config_t* config);
  zx_status_t PwmImplEnable(uint32_t idx);
  zx_status_t PwmImplDisable(uint32_t idx);
};

zx_status_t TestPwmDevice::Create(zx_device_t* parent) {
  auto dev = std::make_unique<TestPwmDevice>(parent);
  zx_status_t status;

  zxlogf(INFO, "TestPwmDevice::Create: %s ", DRIVER_NAME);

  status = dev->DdkAdd("test-pwm");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed: %d", __func__, status);
    return status;
  }
  // devmgr is now in charge of dev.
  __UNUSED auto unused = dev.release();

  return ZX_OK;
}

zx_status_t TestPwmDevice::PwmImplGetConfig(uint32_t idx, pwm_config_t* out_config) {
  if (idx || !out_config || (out_config->mode_config_size != sizeof(mode_config))) {
    return ZX_ERR_INVALID_ARGS;
  }

  out_config->polarity = false;
  out_config->period_ns = 1000;
  out_config->duty_cycle = 39.0;
  auto mode_cfg = reinterpret_cast<mode_config*>(out_config->mode_config_buffer);
  mode_cfg->mode = 0;
  mode_cfg->magic.magic = 12345;

  return ZX_OK;
}

zx_status_t TestPwmDevice::PwmImplSetConfig(uint32_t idx, const pwm_config_t* config) {
  if (idx) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (config->polarity || (config->period_ns != 1000) || (config->duty_cycle != 39.0) ||
      (config->mode_config_size != sizeof(mode_config)) ||
      (reinterpret_cast<const mode_config*>(config->mode_config_buffer)->mode != 0) ||
      (reinterpret_cast<const mode_config*>(config->mode_config_buffer)->magic.magic != 12345)) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t TestPwmDevice::PwmImplEnable(uint32_t idx) {
  if (idx) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t TestPwmDevice::PwmImplDisable(uint32_t idx) {
  if (idx) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

void TestPwmDevice::DdkRelease() { delete this; }

zx_status_t test_pwm_bind(void* ctx, zx_device_t* parent) { return TestPwmDevice::Create(parent); }

constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t driver_ops = {};
  driver_ops.version = DRIVER_OPS_VERSION;
  driver_ops.bind = test_pwm_bind;
  return driver_ops;
}();

}  // namespace pwm

ZIRCON_DRIVER(test_pwm, pwm::driver_ops, "zircon", "0.1");

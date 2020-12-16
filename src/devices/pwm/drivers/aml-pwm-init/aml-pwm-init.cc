// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-pwm-init.h"

#include <unistd.h>

#include <ddk/metadata/init-step.h>
#include <ddktl/protocol/composite.h>
#include <fbl/alloc_checker.h>

#include "src/devices/pwm/drivers/aml-pwm-init/aml-pwm-init-bind.h"

namespace pwm_init {

zx_status_t PwmInitDevice::Create(void* ctx, zx_device_t* parent) {
  zx_status_t status;

  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "PwmInitDevice::Could not get composite protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  ddk::PwmProtocolClient pwm(composite, "pwm");
  ddk::GpioProtocolClient wifi_gpio(composite, "gpio-wifi");
  ddk::GpioProtocolClient bt_gpio(composite, "gpio-bt");
  if (!pwm.is_valid() || !wifi_gpio.is_valid() || !bt_gpio.is_valid()) {
    zxlogf(ERROR, "%s: could not get fragments", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<PwmInitDevice> dev(new (&ac) PwmInitDevice(parent, pwm, wifi_gpio, bt_gpio));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = dev->Init()) != ZX_OK) {
    zxlogf(ERROR, "%s: could not initialize PWM for bluetooth and SDIO", __func__);
    return status;
  }

  zx_device_prop_t props[] = {
      {BIND_INIT_STEP, 0, BIND_INIT_STEP_PWM},
  };
  status = dev->DdkAdd(ddk::DeviceAddArgs("aml-pwm-init")
                           .set_flags(DEVICE_ADD_ALLOW_MULTI_COMPOSITE)
                           .set_props(props));
  if (status != ZX_OK) {
    return status;
  }

  // dev is now owned by devmgr.
  __UNUSED auto ptr = dev.release();

  return ZX_OK;
}

zx_status_t PwmInitDevice::Init() {
  zx_status_t status = ZX_OK;

  // Configure SOC_WIFI_LPO_32k768 pin for PWM_E
  if (((status = wifi_gpio_.SetAltFunction(1)) != ZX_OK)) {
    zxlogf(ERROR, "%s: could not initialize GPIO for WIFI", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  if ((status = pwm_.Enable()) != ZX_OK) {
    zxlogf(ERROR, "%s: Could not enable PWM", __func__);
    return status;
  }
  aml_pwm::mode_config two_timer = {
      .mode = aml_pwm::TWO_TIMER,
      .two_timer =
          {
              .period_ns2 = 30052,
              .duty_cycle2 = 50.0,
              .timer1 = 0x0a,
              .timer2 = 0x0a,
          },
  };
  pwm_config_t init_cfg = {
      .polarity = false,
      .period_ns = 30053,
      .duty_cycle = static_cast<float>(49.931787176),
      .mode_config_buffer = &two_timer,
      .mode_config_size = sizeof(two_timer),
  };
  if ((status = pwm_.SetConfig(&init_cfg)) != ZX_OK) {
    zxlogf(ERROR, "%s: Could not initialize PWM", __func__);
    return status;
  }

  // set GPIO to reset Bluetooth module
  if ((status = bt_gpio_.ConfigOut(0)) != ZX_OK) {
    zxlogf(ERROR, "%s: Could not initialize GPIO for Bluetooth", __func__);
    return status;
  }
  usleep(10 * 1000);
  if ((status = bt_gpio_.Write(1)) != ZX_OK) {
    zxlogf(ERROR, "%s: Could not initialize GPIO for Bluetooth", __func__);
    return status;
  }
  usleep(100 * 1000);

  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = PwmInitDevice::Create;
  return ops;
}();

}  // namespace pwm_init

ZIRCON_DRIVER(pwm_init, pwm_init::driver_ops, "zircon", "0.1");

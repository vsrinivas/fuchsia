// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-pwm-init.h"

#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/metadata/init-step.h>
#include <ddktl/protocol/composite.h>
#include <fbl/alloc_checker.h>

namespace pwm_init {

zx_status_t PwmInitDevice::Create(void* ctx, zx_device_t* parent) {
  zx_status_t status;

  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "PwmInitDevice::Could not get composite protocol\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_device_t* components[COMPONENT_COUNT];
  size_t component_count;
  composite.GetComponents(components, countof(components), &component_count);
  if (component_count != COMPONENT_COUNT) {
    zxlogf(ERROR, "PwmInitDevice: Could not get components\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  ddk::PwmProtocolClient pwm(components[COMPONENT_PWM]);
  ddk::GpioProtocolClient wifi_gpio(components[COMPONENT_WIFI_GPIO]);
  ddk::GpioProtocolClient bt_gpio(components[COMPONENT_BT_GPIO]);
  if (!pwm.is_valid() || !wifi_gpio.is_valid() || !bt_gpio.is_valid()) {
    zxlogf(ERROR, "%s: could not get components\n", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<PwmInitDevice> dev(new (&ac) PwmInitDevice(parent, pwm, wifi_gpio, bt_gpio));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = dev->Init()) != ZX_OK) {
    zxlogf(ERROR, "%s: could not initialize PWM for bluetooth and SDIO\n", __func__);
    return status;
  }

  zx_device_prop_t props[] = {
      {BIND_INIT_STEP, 0, BIND_INIT_STEP_PWM},
  };
  status = dev->DdkAdd("aml-pwm-init", DEVICE_ADD_ALLOW_MULTI_COMPOSITE, props, countof(props));
  if (status != ZX_OK) {
    return status;
  }

  // dev is now owned by devmgr.
  __UNUSED auto ptr = dev.release();

  return ZX_OK;
}

zx_status_t PwmInitDevice::Init() { return ZX_OK; }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = PwmInitDevice::Create;
  return ops;
}();

}  // namespace pwm_init

// clang-format off
ZIRCON_DRIVER_BEGIN(pwm_init, pwm_init::driver_ops, "zircon", "0.1", 5)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_PWM_INIT),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_T931),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
ZIRCON_DRIVER_END(pwm_init)
    // clang-format on

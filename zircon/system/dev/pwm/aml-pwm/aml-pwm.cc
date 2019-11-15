// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-pwm.h"

#include <lib/device-protocol/pdev.h>

#include <ddk/binding.h>
#include <ddk/protocol/platform/bus.h>
#include <fbl/alloc_checker.h>

namespace pwm {

zx_status_t AmlPwmDevice::Create(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  std::unique_ptr<AmlPwmDevice> device(new (&ac) AmlPwmDevice(parent));
  if (!ac.check()) {
    zxlogf(ERROR, "%s: device object alloc failed\n", __func__);
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = ZX_OK;
  if ((status = device->Init(parent)) != ZX_OK) {
    zxlogf(ERROR, "%s: Init failed\n", __func__);
    return status;
  }

  if ((status = device->DdkAdd("aml-pwm-device", 0, nullptr, 0, ZX_PROTOCOL_PWM_IMPL, nullptr,
                               ZX_HANDLE_INVALID, nullptr, 0)) != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed\n", __func__);
    return status;
  }

  __UNUSED auto* unused = device.release();

  return ZX_OK;
}

zx_status_t AmlPwmDevice::Init(zx_device_t* parent) {
  zx_status_t status = ZX_OK;

  ddk::PDev pdev(parent);
  if ((status = pdev.MapMmio(MMIO_AB, &(mmios_[MMIO_AB]))) != ZX_OK) {
    zxlogf(ERROR, "%s: MapMmio failed\n", __func__);
    return status;
  }
  if ((status = pdev.MapMmio(MMIO_CD, &(mmios_[MMIO_CD]))) != ZX_OK) {
    zxlogf(ERROR, "%s: MapMmio failed\n", __func__);
    return status;
  }
  if ((status = pdev.MapMmio(MMIO_EF, &(mmios_[MMIO_EF]))) != ZX_OK) {
    zxlogf(ERROR, "%s: MapMmio failed\n", __func__);
    return status;
  }
  if ((status = pdev.MapMmio(MMIO_AO_AB, &(mmios_[MMIO_AO_AB]))) != ZX_OK) {
    zxlogf(ERROR, "%s: MapMmio failed\n", __func__);
    return status;
  }
  if ((status = pdev.MapMmio(MMIO_AO_CD, &(mmios_[MMIO_AO_CD]))) != ZX_OK) {
    zxlogf(ERROR, "%s: MapMmio failed\n", __func__);
    return status;
  }

  return ZX_OK;
}

void AmlPwmDevice::ShutDown() {}

zx_status_t AmlPwmDevice::PwmImplSetConfig(uint32_t idx, const pwm_config_t* config) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t AmlPwmDevice::PwmImplEnable(uint32_t idx) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t AmlPwmDevice::PwmImplDisable(uint32_t idx) { return ZX_ERR_NOT_SUPPORTED; }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = AmlPwmDevice::Create;
  return ops;
}();

}  // namespace pwm

// clang-format off
ZIRCON_DRIVER_BEGIN(pwm, pwm::driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_T931),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_PWM),
ZIRCON_DRIVER_END(pwm)
    // clang-format on

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pwm.h"

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <fbl/alloc_checker.h>

#include "src/devices/pwm/drivers/pwm/pwm-bind.h"

namespace pwm {

zx_status_t PwmDevice::Create(void* ctx, zx_device_t* parent) {
  pwm_impl_protocol_t pwm_proto;
  auto status = device_get_protocol(parent, ZX_PROTOCOL_PWM_IMPL, &pwm_proto);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: device_get_protocol failed %d", __FILE__, status);
    return status;
  }

  size_t metadata_size;
  status = device_get_metadata_size(parent, DEVICE_METADATA_PWM_IDS, &metadata_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: device_get_metadata_size failed %d", __FILE__, status);
    return status;
  }
  auto pwm_count = metadata_size / sizeof(pwm_id_t);

  fbl::AllocChecker ac;
  std::unique_ptr<pwm_id_t[]> pwm_ids(new (&ac) pwm_id_t[pwm_count]);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  size_t actual;
  status =
      device_get_metadata(parent, DEVICE_METADATA_PWM_IDS, pwm_ids.get(), metadata_size, &actual);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: device_get_metadata failed %d", __FILE__, status);
    return status;
  }
  if (actual != metadata_size) {
    zxlogf(ERROR, "%s: device_get_metadata size error %d", __FILE__, status);
    return ZX_ERR_INTERNAL;
  }

  for (uint32_t i = 0; i < pwm_count; i++) {
    auto pwm_id = pwm_ids[i];
    fbl::AllocChecker ac;
    std::unique_ptr<PwmDevice> dev(new (&ac) PwmDevice(parent, &pwm_proto, pwm_id));
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }

    char name[20];
    snprintf(name, sizeof(name), "pwm-%u", pwm_id.id);
    zx_device_prop_t props[] = {
        {BIND_PWM_ID, 0, pwm_id.id},
    };

    status = dev->DdkAdd(
        ddk::DeviceAddArgs(name).set_flags(DEVICE_ADD_ALLOW_MULTI_COMPOSITE).set_props(props));
    if (status != ZX_OK) {
      return status;
    }

    // dev is now owned by devmgr.
    __UNUSED auto ptr = dev.release();
  }

  return ZX_OK;
}

zx_status_t PwmDevice::PwmGetConfig(pwm_config_t* out_config) {
  return pwm_.GetConfig(id_.id, out_config);
}

zx_status_t PwmDevice::PwmSetConfig(const pwm_config_t* config) {
  if (id_.protect) {
    return ZX_ERR_ACCESS_DENIED;
  }
  return pwm_.SetConfig(id_.id, config);
}

zx_status_t PwmDevice::PwmEnable() {
  if (id_.protect) {
    return ZX_ERR_ACCESS_DENIED;
  }
  return pwm_.Enable(id_.id);
}

zx_status_t PwmDevice::PwmDisable() {
  if (id_.protect) {
    return ZX_ERR_ACCESS_DENIED;
  }
  return pwm_.Disable(id_.id);
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = PwmDevice::Create;
  return ops;
}();

}  // namespace pwm

ZIRCON_DRIVER(pwm, pwm::driver_ops, "zircon", "0.1");

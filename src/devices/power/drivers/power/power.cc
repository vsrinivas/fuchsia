// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power.h"

#include <zircon/types.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/power.h>
#include <ddk/platform-defs.h>
#include <ddktl/protocol/composite.h>
#include <ddktl/protocol/powerimpl.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>

namespace power {

zx_status_t PowerDevice::PowerEnablePowerDomain() { return power_.EnablePowerDomain(index_); }

zx_status_t PowerDevice::PowerDisablePowerDomain() { return power_.DisablePowerDomain(index_); }

zx_status_t PowerDevice::PowerGetPowerDomainStatus(power_domain_status_t* out_status) {
  return power_.GetPowerDomainStatus(index_, out_status);
}

zx_status_t PowerDevice::PowerGetSupportedVoltageRange(uint32_t* min_voltage,
                                                       uint32_t* max_voltage) {
  return power_.GetSupportedVoltageRange(index_, min_voltage, max_voltage);
}

zx_status_t PowerDevice::PowerRequestVoltage(uint32_t voltage, uint32_t* actual_voltage) {
  return power_.RequestVoltage(index_, voltage, actual_voltage);
}

zx_status_t PowerDevice::PowerGetCurrentVoltage(uint32_t index, uint32_t* current_voltage) {
  return power_.GetCurrentVoltage(index_, current_voltage);
}

zx_status_t PowerDevice::PowerWritePmicCtrlReg(uint32_t reg_addr, uint32_t value) {
  return power_.WritePmicCtrlReg(index_, reg_addr, value);
}

zx_status_t PowerDevice::PowerReadPmicCtrlReg(uint32_t reg_addr, uint32_t* out_value) {
  return power_.ReadPmicCtrlReg(index_, reg_addr, out_value);
}

void PowerDevice::DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }

void PowerDevice::DdkRelease() { delete this; }

zx_status_t PowerDevice::Create(void* ctx, zx_device_t* parent) {
  size_t metadata_size;

  zx_status_t status =
      device_get_metadata_size(parent, DEVICE_METADATA_POWER_DOMAINS, &metadata_size);
  if (status != ZX_OK) {
    return status;
  }
  auto count = metadata_size / sizeof(power_domain_t);

  fbl::AllocChecker ac;
  std::unique_ptr<power_domain_t[]> power_domains(new (&ac) power_domain_t[count]);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  size_t actual;
  status = device_get_metadata(parent, DEVICE_METADATA_POWER_DOMAINS, power_domains.get(),
                               metadata_size, &actual);
  if (status != ZX_OK) {
    return status;
  }
  if (actual != metadata_size) {
    return ZX_ERR_INTERNAL;
  }

  if (count != 1) {
    return ZX_ERR_INTERNAL;
  }

  auto index = power_domains[0].index;
  char name[20];
  snprintf(name, sizeof(name), "power-%u", index);

  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "%s could not get composite protocoln", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint32_t parent_count = composite.GetComponentCount();
  zx_device_t* components[parent_count];
  composite.GetComponents(components, parent_count, &actual);
  if (actual != parent_count) {
    zxlogf(ERROR, "%s could not get composite components\n", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // POWER_IMPL_PROTOCOL is always the first component
  ddk::PowerImplProtocolClient power(components[0]);
  if (!power.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_POWER_IMPL not available\n", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  std::unique_ptr<PowerDevice> dev(new (&ac) PowerDevice(parent, power, index));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_device_prop_t props[] = {
      {BIND_POWER_DOMAIN, 0, index},
  };

  status = dev->DdkAdd(name, DEVICE_ADD_ALLOW_MULTI_COMPOSITE, props, countof(props));
  if (status != ZX_OK) {
    return status;
  }

  // dev is now owned by devmgr.
  __UNUSED auto ptr = dev.release();

  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = PowerDevice::Create;
  return ops;
}();

}  // namespace power

// clang-format off
ZIRCON_DRIVER_BEGIN(generic-power, power::driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_MATCH_IF(EQ, BIND_POWER_DOMAIN_COMPOSITE, PDEV_DID_POWER_DOMAIN_COMPOSITE),
ZIRCON_DRIVER_END(gerneric-power)
//clang-format on

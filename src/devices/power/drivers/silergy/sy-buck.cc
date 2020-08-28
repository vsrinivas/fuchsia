// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sy-buck.h"

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <hwreg/i2c.h>

#include "sy-buck-regs.h"

namespace silergy {

zx_status_t SyBuck::VregSetVoltageStep(uint32_t step) {
  if (step >= kNumSteps) {
    zxlogf(ERROR, "%s: Requested step out of range step = %u, max = %u", __func__, step, kNumSteps);
    return ZX_ERR_OUT_OF_RANGE;
  }

  auto vsel = VselReg::Get(vsel_).FromValue(0);
  zx_status_t st = vsel.ReadFrom(i2c_);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: failed to read vsel reg, st = %d", __func__, st);
    return st;
  }

  st = vsel.set_n_sel(static_cast<uint8_t>(step)).WriteTo(i2c_);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: failed to write vsel reg, st = %d", __func__, st);
    return st;
  }

  return ZX_OK;
}

uint32_t SyBuck::VregGetVoltageStep() { return current_step_; }

void SyBuck::VregGetRegulatorParams(vreg_params_t* out_params) {
  if (!out_params) {
    return;
  }

  out_params->min_uv = kMinVoltageUv;
  out_params->num_steps = kNumSteps;
  out_params->step_size_uv = kVoltageStepUv;
}

zx_status_t SyBuck::Init() {
  zx_status_t st;

  auto id1 = Id1Reg::Get().FromValue(0);
  auto id2 = Id2Reg::Get().FromValue(0);
  auto vsel = VselReg::Get(vsel_).FromValue(0);

  st = id1.ReadFrom(i2c_);
  if (st != ZX_OK) {
    zxlogf(WARNING, "failed to read id1 from i2c, st = %u", st);
    return st;
  }

  st = id2.ReadFrom(i2c_);
  if (st != ZX_OK) {
    zxlogf(WARNING, "failed to read id2 from i2c, st = %u", st);
    return st;
  }

  st = vsel.ReadFrom(i2c_);
  if (st != ZX_OK) {
    zxlogf(WARNING, "failed to read vsel from i2c, st = %u", st);
    return st;
  }

  zxlogf(INFO, "sybuck init vendor = %u, die_id = %u, die_rev = %u, vsel = %u", id1.vendor(),
         id1.die_id(), id2.die_rev(), static_cast<uint32_t>(vsel_));

  current_step_ = vsel.n_sel();

  return ZX_OK;
}

zx_status_t SyBuck::Create(void* ctx, zx_device_t* parent) {
  zx_status_t st;
  zxlogf(DEBUG, "%s: Binding SyBuck", __func__);

  ddk::I2cProtocolClient i2c(parent);
  if (!i2c.is_valid()) {
    zxlogf(ERROR, "%s: SyBuck failed to get i2c channel", __func__);
    return ZX_ERR_INTERNAL;
  }

  auto device = std::make_unique<SyBuck>(parent, std::move(i2c));

  st = device->Init();
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to init device, st = %u", __func__, st);
    return st;
  }

  st = device->DdkAdd(ddk::DeviceAddArgs("silergy-sy-buck"));
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed, st = %d", __func__, st);
    return st;
  }

  __UNUSED auto ptr = device.release();

  return st;
}

}  // namespace silergy

static constexpr zx_driver_ops_t sy_buck_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = silergy::SyBuck::Create;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(sybuck, sy_buck_driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_SILERGY),
BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_SILERGY_SYBUCK),
ZIRCON_DRIVER_END(sybuck)

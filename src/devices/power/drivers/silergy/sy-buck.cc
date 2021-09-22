// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sy-buck.h"

#include <fidl/fuchsia.hardware.i2c/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <memory>

#include <hwreg/i2c.h>

#include "src/devices/power/drivers/silergy/sy-buck-bind.h"
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

  // Determine which i2c Bus/Address this device is attached to.
  size_t metadata_size = 0;
  st = device_get_metadata_size(parent, DEVICE_METADATA_I2C_CHANNELS, &metadata_size);
  if (st != ZX_OK) {
    zxlogf(ERROR, "%s: device_get_metadata_size failed %d", __func__, st);
    return ZX_ERR_INTERNAL;
  }

  auto buffer_deleter = std::make_unique<uint8_t[]>(metadata_size);
  auto buffer = buffer_deleter.get();

  size_t actual;
  st = device_get_metadata(parent, DEVICE_METADATA_I2C_CHANNELS, buffer, metadata_size, &actual);
  if (st != ZX_OK || actual != metadata_size) {
    zxlogf(ERROR, "%s: device_get_metadata failed %d", __func__, st);
    return ZX_ERR_INTERNAL;
  }

  fidl::DecodedMessage<fuchsia_hardware_i2c::wire::I2CBusMetadata> decoded(
      fidl::internal::kLLCPPEncodedWireFormatVersion, buffer, metadata_size);
  if (!decoded.ok()) {
    zxlogf(ERROR, "%s: Failed to deserialize metadata.", __func__);
    return ZX_ERR_INTERNAL;
  }

  fuchsia_hardware_i2c::wire::I2CBusMetadata* metadata = decoded.PrimaryObject();
  if (!metadata->has_channels() || metadata->channels().count() != 1) {
    zxlogf(ERROR, "%s: sybuck expects exactly one i2c channel passed as metadata. ", __func__);
    return ZX_ERR_INTERNAL;
  }

  fuchsia_hardware_i2c::wire::I2CChannel& channel = metadata->channels()[0];

  ddk::I2cProtocolClient i2c(parent, "i2c");
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

  zx_device_prop_t props[] = {
      {BIND_I2C_BUS_ID, 0, channel.has_bus_id() ? channel.bus_id() : 0},
      {BIND_I2C_ADDRESS, 0, static_cast<uint32_t>(channel.has_address() ? channel.address() : 0)},
  };

  st = device->DdkAdd(ddk::DeviceAddArgs("silergy-sy-buck").set_props(props));
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

ZIRCON_DRIVER(sybuck, sy_buck_driver_ops, "zircon", "0.1");

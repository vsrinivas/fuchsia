// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/power/drivers/fusb302/fusb302.h"

#include <lib/ddk/debug.h>

#include <fbl/alloc_checker.h>

#include "registers.h"
#include "src/devices/power/drivers/fusb302/fusb302-bind.h"

namespace fusb302 {

namespace {

// Sleep after setting measure bits and before taking measurements to give time to hardware to
// react.
auto constexpr kSleep = zx::usec(250);

}  // namespace

zx_status_t Fusb302::Init() {
  // Device ID
  auto device_id = DeviceIdReg::Get().FromValue(0);
  auto status = device_id.ReadFrom(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read from power delivery unit. %d", status);
    return status;
  }
  device_id_ = inspect_.GetRoot().CreateChild("DeviceId");
  device_id_.CreateUint("VersionId", device_id.version_id(), &inspect_);
  zxlogf(INFO, "version id: 0x%x", device_id.version_id());
  device_id_.CreateUint("ProductId", device_id.product_id(), &inspect_);
  zxlogf(INFO, "product id: 0x%x", device_id.product_id());
  device_id_.CreateUint("RevisionId", device_id.revision_id(), &inspect_);
  zxlogf(INFO, "revision id: 0x%x", device_id.revision_id());

  // Power and Data Role
  auto switches1 = Switches1Reg::Get().FromValue(0);
  status = switches1.ReadFrom(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read from power delivery unit. %d", status);
    return status;
  }
  power_role_ =
      inspect_.GetRoot().CreateString("PowerRole", switches1.power_role() ? "Source" : "Sink");
  zxlogf(INFO, "Currently acting as power %s", switches1.power_role() ? "source" : "sink");
  data_role_ =
      inspect_.GetRoot().CreateString("DataRole", switches1.data_role() ? "Source" : "Sink");
  zxlogf(INFO, "Currently acting as data %s", switches1.data_role() ? "source" : "sink");

  // Measure VBUS
  auto power = PowerReg::Get().FromValue(0);
  status = power.ReadFrom(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read from power delivery unit. %d", status);
    return status;
  }
  status = power.set_pwr2(1).WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write to power delivery unit. %d", status);
    return status;
  }
  auto switches0 = Switches0Reg::Get().FromValue(0);
  status = switches0.ReadFrom(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read from power delivery unit. %d", status);
    return status;
  }
  status = switches0.set_meas_cc2(0).set_meas_cc1(0).WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write to power delivery unit. %d", status);
    return status;
  }
  auto measure = MeasureReg::Get().FromValue(0);
  status = measure.ReadFrom(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read from power delivery unit. %d", status);
    return status;
  }
  status = measure.set_meas_vbus(1).WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write to power delivery unit. %d", status);
    return status;
  }
  zx::nanosleep(zx::deadline_after(kSleep));
  status = measure.ReadFrom(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read from power delivery unit. %d", status);
    return status;
  }
  meas_vbus_ = inspect_.GetRoot().CreateDouble("VBUS Voltage (Volts)",
                                               measure.mdac() * kVbusMeasureVoltageStep);
  zxlogf(INFO, "Measured VBUS voltage is %f V", measure.mdac() * kVbusMeasureVoltageStep);

  // Measure CC1
  cc1_ = inspect_.GetRoot().CreateChild("CC1");
  status = measure.ReadFrom(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read from power delivery unit. %d", status);
    return status;
  }
  status = measure.set_meas_vbus(0).WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write to power delivery unit. %d", status);
    return status;
  }
  status = switches0.ReadFrom(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read from power delivery unit. %d", status);
    return status;
  }
  status = switches0.set_meas_cc2(0).set_meas_cc1(1).WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write to power delivery unit. %d", status);
    return status;
  }
  zx::nanosleep(zx::deadline_after(kSleep));
  status = measure.ReadFrom(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read from power delivery unit. %d", status);
    return status;
  }
  meas_cc1_ = cc1_.CreateDouble("CC1 Voltage (Volts)", measure.mdac() * kCcMeasureVoltageStep);
  zxlogf(INFO, "Measured CC1 voltage is %f V", measure.mdac() * kCcMeasureVoltageStep);
  // Get BC Level for CC1
  auto status0 = Status0Reg::Get().FromValue(0);
  status = status0.ReadFrom(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read from power delivery unit. %d", status);
    return status;
  }
  bc_lvl_cc1_ = cc1_.CreateString("Battery Charging Level", bc_level[status0.bc_lvl()]);
  zxlogf(INFO, "CC1 Battery Charging Level is %s", bc_level[status0.bc_lvl()].c_str());

  // Measure CC2
  cc2_ = inspect_.GetRoot().CreateChild("CC2");
  status = switches0.ReadFrom(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read from power delivery unit. %d", status);
    return status;
  }
  status = switches0.set_meas_cc2(1).set_meas_cc1(0).WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write to power delivery unit. %d", status);
    return status;
  }
  zx::nanosleep(zx::deadline_after(kSleep));
  status = measure.ReadFrom(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read from power delivery unit. %d", status);
    return status;
  }
  meas_cc2_ = cc2_.CreateDouble("CC2 Voltage (Volts)", measure.mdac() * kCcMeasureVoltageStep);
  zxlogf(INFO, "Measured CC2 voltage is %f V", measure.mdac() * kCcMeasureVoltageStep);
  // Get BC Level for CC2
  status = status0.ReadFrom(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read from power delivery unit. %d", status);
    return status;
  }
  bc_lvl_cc2_ = cc2_.CreateString("Battery Charging Level", bc_level[status0.bc_lvl()]);
  zxlogf(INFO, "CC2 Battery Charging Level is %s", bc_level[status0.bc_lvl()].c_str());

  // Power off Measure Block
  power = PowerReg::Get().FromValue(0);
  status = power.ReadFrom(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read from power delivery unit. %d", status);
    return status;
  }
  status = power.set_pwr2(0).WriteTo(i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write to power delivery unit. %d", status);
    return status;
  }

  return ZX_OK;
}

zx_status_t Fusb302::Create(void* context, zx_device_t* parent) {
  ddk::I2cChannel i2c(parent, "i2c");
  if (!i2c.is_valid()) {
    zxlogf(ERROR, "Failed to get I2C");
    return ZX_ERR_INTERNAL;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<Fusb302> device(new (&ac) Fusb302(parent, i2c));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  auto status = device->Init();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Init failed, status = %d", status);
  }

  status =
      device->DdkAdd(ddk::DeviceAddArgs("usb-pd").set_inspect_vmo(device->inspect_.DuplicateVmo()));
  if (status != ZX_OK) {
    zxlogf(ERROR, "DdkAdd failed, status = %d", status);
  }

  // Let device runner take ownership of this object.
  __UNUSED auto* dummy = device.release();

  return ZX_OK;
}

}  // namespace fusb302

static constexpr zx_driver_ops_t fusb302_driver_ops = []() {
  zx_driver_ops_t result = {};
  result.version = DRIVER_OPS_VERSION;
  result.bind = fusb302::Fusb302::Create;
  return result;
}();

// clang-format off
ZIRCON_DRIVER(fusb302, fusb302_driver_ops, "zircon", "0.1");


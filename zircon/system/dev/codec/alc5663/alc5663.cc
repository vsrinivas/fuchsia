// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "alc5663.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/i2c.h>
#include <ddktl/protocol/i2c.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/device-protocol/i2c.h>
#include <sys/types.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include "alc5663_registers.h"

namespace audio::alc5663 {

Alc5663Device::Alc5663Device(zx_device_t* parent, ddk::I2cChannel channel)
    : DeviceType(parent), client_(channel) {}

zx_status_t Alc5663Device::Bind(fbl::unique_ptr<Alc5663Device> device) {
  // Initialise the hardware.
  zx_status_t status = device->InitializeDevice();
  if (status != ZX_OK) {
    zxlogf(ERROR, "alc5663: failed to initialize hardware: %s\n", zx_status_get_string(status));
    return status;
  }

  // Add the device.
  status = device->DdkAdd("alc5663");
  if (status != ZX_OK) {
    zxlogf(ERROR, "alc5663: could not add device: %s\n", zx_status_get_string(status));
    return status;
  }

  (void)device.release();  // `dev` will be deleted when DdkRelease() is called.
  return ZX_OK;
}

zx_status_t Alc5663Device::InitializeDevice() {
  // Reset the device.
  zx_status_t status = WriteRegister(&client_, ResetAndDeviceIdReg{});
  if (status != ZX_OK) {
    zxlogf(ERROR, "alc5663: Could not reset device.\n");
    return status;
  }

  // Verify the ALC5663 is the right revision.
  ResetAndDeviceIdReg reset_reg{};
  status = ReadRegister(&client_, &reset_reg);
  if (status != ZX_OK) {
    zxlogf(ERROR, "alc5663: Could not read device ID.\n");
    return status;
  }
  if (reset_reg.device_id() != 0) {
    zxlogf(ERROR, "alc5663: Unsupported device revision.\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Power on everything.
  //
  // TODO(ZX-1538): Only turn on subsystems as/if they are needed.
  status = MapRegister<PowerManagementControl1Reg>(&client_, [](auto reg) {
    return reg.set_en_i2s1(1)
        .set_pow_dac_l_1(1)
        .set_pow_dac_r_1(1)
        .set_pow_ldo_adcref(1)
        .set_pow_adc_l(1);
  });
  if (status != ZX_OK) {
    return status;
  }

  status = MapRegister<PowerManagementControl2Reg>(
      &client_, [](auto reg) { return reg.set_pow_adc_filter(1).set_pow_dac_stereo1_filter(1); });
  if (status != ZX_OK) {
    return status;
  }

  status = MapRegister<PowerManagementControl3Reg>(&client_, [](auto reg) {
    return reg.set_pow_vref1(1)
        .set_pow_vref2(1)
        .set_pow_main_bias(1)
        .set_pow_bg_bias(1)
        .set_en_l_hp(1)
        .set_en_r_hp(1);
  });
  if (status != ZX_OK) {
    return status;
  }

  status = MapRegister<PowerManagementControl4Reg>(&client_, [](auto reg) {
    return reg.set_pow_bst1(1).set_pow_micbias1(1).set_pow_micbias2(1).set_pow_recmix1(1);
  });
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

void Alc5663Device::Shutdown() {
  // Reset the device.
  //
  // TODO(dgreenway): Power down the device.
  zx_status_t status = WriteRegister(&client_, ResetAndDeviceIdReg{});
  if (status != ZX_OK) {
    zxlogf(WARN, "alc5663: Failed to reset the device during shutdown.\n");
  }
}

void Alc5663Device::DdkUnbind() {}

void Alc5663Device::DdkRelease() { delete this; }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = [](void* /*ctx*/, zx_device_t* parent) {
    // Get access to the I2C protocol.
    ddk::I2cChannel channel(parent);
    if (!channel.is_valid()) {
      zxlogf(ERROR, "alc5663: could not get I2C protocol from parent device.\n");
      // Failure to create the client typically indicates the device is not
      // supported.
      return ZX_ERR_NOT_SUPPORTED;
    }

    // Create the codec device.
    fbl::AllocChecker ac;
    auto device = fbl::unique_ptr<Alc5663Device>(new (&ac) Alc5663Device(parent, channel));
    if (!ac.check()) {
      zxlogf(ERROR, "alc5663: out of memory attempting to allocate device\n");
      return ZX_ERR_NO_MEMORY;
    }

    return Alc5663Device::Bind(std::move(device));
  };
  return ops;
}();

}  // namespace audio::alc5663

// clang-format off
ZIRCON_DRIVER_BEGIN(alc5663, audio::alc5663::driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_ACPI_HID_0_3, 0x31304543), // '10EC' (Realtek)
    BI_MATCH_IF(EQ, BIND_ACPI_HID_4_7, 0x35363633), // '5663'
ZIRCON_DRIVER_END(alc5663)
    // clang-format on

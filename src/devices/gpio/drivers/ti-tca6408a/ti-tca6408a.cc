// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ti-tca6408a.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>

#include "src/devices/gpio/drivers/ti-tca6408a/ti-tca6408a-bind.h"

namespace {

// Arbitrary values for I2C retries.
constexpr uint8_t kI2cRetries = 10;
constexpr zx::duration kI2cRetryDelay = zx::usec(1);

}  // namespace

namespace gpio {

zx_status_t TiTca6408a::Create(void* ctx, zx_device_t* parent) {
  ddk::I2cChannel i2c(parent, "i2c");
  if (!i2c.is_valid()) {
    zxlogf(ERROR, "Failed to get I2C channel");
    return ZX_ERR_NO_RESOURCES;
  }

  {
    // Clear the polarity inversion register.
    const uint8_t write_buf[2] = {static_cast<uint8_t>(Register::kPolarityInversion), 0};
    i2c.WriteSyncRetries(write_buf, sizeof(write_buf), kI2cRetries, kI2cRetryDelay);
  }

  uint32_t pin_index_offset = 0;
  size_t actual = 0;
  zx_status_t status = device_get_metadata(parent, DEVICE_METADATA_PRIVATE, &pin_index_offset,
                                           sizeof(pin_index_offset), &actual);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to get metadata: %s", zx_status_get_string(status));
    return status;
  }

  if (actual != sizeof(pin_index_offset)) {
    zxlogf(ERROR, "Unexpected metadata size: got %zu, expected %zu", actual,
           sizeof(pin_index_offset));
    return ZX_ERR_INTERNAL;
  }

  auto dev = std::make_unique<TiTca6408a>(parent, std::move(i2c), pin_index_offset);
  if ((status = dev->DdkAdd("ti-tca6408a")) != ZX_OK) {
    zxlogf(ERROR, "DdkAdd failed: %s", zx_status_get_string(status));
    return status;
  }

  __UNUSED auto* _ = dev.release();

  return ZX_OK;
}

zx_status_t TiTca6408a::GpioImplConfigIn(uint32_t index, uint32_t flags) {
  if (!IsIndexInRange(index)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (flags != GPIO_NO_PULL) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  return SetBit(Register::kConfiguration, index).status_value();
}

zx_status_t TiTca6408a::GpioImplConfigOut(uint32_t index, uint8_t initial_value) {
  if (zx_status_t status = GpioImplWrite(index, initial_value); status != ZX_OK) {
    return status;
  }
  return ClearBit(Register::kConfiguration, index).status_value();
}

zx_status_t TiTca6408a::GpioImplSetAltFunction(uint32_t index, uint64_t function) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t TiTca6408a::GpioImplSetDriveStrength(uint32_t index, uint64_t ua,
                                                 uint64_t* out_actual_ua) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t TiTca6408a::GpioImplGetDriveStrength(uint32_t index, uint64_t* ua) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t TiTca6408a::GpioImplRead(uint32_t index, uint8_t* out_value) {
  if (!IsIndexInRange(index)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  zx::status<uint8_t> value = ReadBit(Register::kInputPort, index);
  if (value.is_error()) {
    return value.status_value();
  }

  if (out_value) {
    *out_value = value.value();
  }

  return ZX_OK;
}

zx_status_t TiTca6408a::GpioImplWrite(uint32_t index, uint8_t value) {
  if (!IsIndexInRange(index)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const zx::status<> status =
      value ? SetBit(Register::kOutputPort, index) : ClearBit(Register::kOutputPort, index);
  return status.status_value();
}

zx_status_t TiTca6408a::GpioImplGetInterrupt(uint32_t index, uint32_t flags,
                                             zx::interrupt* out_irq) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t TiTca6408a::GpioImplReleaseInterrupt(uint32_t index) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t TiTca6408a::GpioImplSetPolarity(uint32_t index, gpio_polarity_t polarity) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx::status<uint8_t> TiTca6408a::ReadBit(Register reg, uint32_t index) {
  const auto bit = static_cast<uint8_t>(1 << (index - pin_index_offset_));
  const auto address = static_cast<uint8_t>(reg);

  uint8_t value = 0;
  auto status = i2c_.WriteReadSyncRetries(&address, sizeof(address), &value, sizeof(value),
                                          kI2cRetries, kI2cRetryDelay);
  if (status.status != ZX_OK) {
    zxlogf(ERROR, "Failed to read register %u: %s", address, zx_status_get_string(status.status));
    return zx::error(status.status);
  }

  return zx::ok(static_cast<uint8_t>((value & bit) ? 1 : 0));
}

zx::status<> TiTca6408a::SetBit(Register reg, uint32_t index) {
  const auto bit = static_cast<uint8_t>(1 << (index - pin_index_offset_));
  const auto address = static_cast<uint8_t>(reg);

  uint8_t value = 0;
  auto status = i2c_.WriteReadSyncRetries(&address, sizeof(address), &value, sizeof(value),
                                          kI2cRetries, kI2cRetryDelay);
  if (status.status != ZX_OK) {
    zxlogf(ERROR, "Failed to read register %u: %s", address, zx_status_get_string(status.status));
    return zx::error(status.status);
  }

  const uint8_t write_buf[2] = {address, static_cast<uint8_t>(value | bit)};
  status = i2c_.WriteSyncRetries(write_buf, sizeof(write_buf), kI2cRetries, kI2cRetryDelay);
  if (status.status != ZX_OK) {
    zxlogf(ERROR, "Failed to write register %u: %s", address, zx_status_get_string(status.status));
    return zx::error(status.status);
  }

  return zx::ok();
}

zx::status<> TiTca6408a::ClearBit(Register reg, uint32_t index) {
  const auto bit = static_cast<uint8_t>(1 << (index - pin_index_offset_));
  const auto address = static_cast<uint8_t>(reg);

  uint8_t value = 0;
  auto status = i2c_.WriteReadSyncRetries(&address, sizeof(address), &value, sizeof(value),
                                          kI2cRetries, kI2cRetryDelay);
  if (status.status != ZX_OK) {
    zxlogf(ERROR, "Failed to read register %u: %s", address, zx_status_get_string(status.status));
    return zx::error(status.status);
  }

  const uint8_t write_buf[2] = {address, static_cast<uint8_t>(value & ~bit)};
  status = i2c_.WriteSyncRetries(write_buf, sizeof(write_buf), kI2cRetries, kI2cRetryDelay);
  if (status.status != ZX_OK) {
    zxlogf(ERROR, "Failed to write register %u: %s", address, zx_status_get_string(status.status));
    return zx::error(status.status);
  }

  return zx::ok();
}

static constexpr zx_driver_ops_t ti_tca6408a_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = TiTca6408a::Create;
  return ops;
}();

}  // namespace gpio

ZIRCON_DRIVER(ti_tca6408a, gpio::ti_tca6408a_driver_ops, "zircon", "0.1");

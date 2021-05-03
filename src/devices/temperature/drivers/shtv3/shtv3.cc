// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shtv3.h"

#include <endian.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/platform-defs.h>
#include <lib/zx/time.h>

#include <ddktl/fidl.h>

#include "src/devices/temperature/drivers/shtv3/shtv3-bind.h"

namespace {

constexpr uint16_t kSoftResetCommand = 0x805d;
// The maximum reset time is 240 us.
constexpr zx::duration kResetTime = zx::usec(500);

// Clock stretching disabled, read temperature first, normal mode.
constexpr uint16_t kStartMeasurementCommand = 0x7866;

// The maximum normal mode measurement time is 12.1 ms.
constexpr int kMeasurementRetries = 15;
constexpr zx::duration kMeasurementRetryInterval = zx::msec(1);

}  // namespace

namespace temperature {

zx_status_t Shtv3Device::Create(void* ctx, zx_device_t* parent) {
  ddk::I2cChannel i2c(parent);
  if (!i2c.is_valid()) {
    zxlogf(ERROR, "Failed to get I2C protocol");
    return ZX_ERR_NO_RESOURCES;
  }

  auto dev = std::make_unique<Shtv3Device>(parent, i2c);
  zx_status_t status = dev->Init();
  if (status != ZX_OK) {
    return status;
  }

  if ((status = dev->DdkAdd("shtv3")) != ZX_OK) {
    zxlogf(ERROR, "DdkAdd failed: %d", status);
    return status;
  }

  __UNUSED auto* _ = dev.release();
  return ZX_OK;
}

zx_status_t Shtv3Device::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fidl::WireDispatch<temperature_fidl::Device>(this, msg, &transaction);
  return transaction.Status();
}

void Shtv3Device::DdkRelease() { delete this; }

void Shtv3Device::GetTemperatureCelsius(GetTemperatureCelsiusRequestView request,
                                        GetTemperatureCelsiusCompleter::Sync& completer) {
  const zx::status<float> status = ReadTemperature();
  completer.Reply(status.is_error() ? status.error_value() : ZX_OK, status.value_or(0.0f));
}

zx_status_t Shtv3Device::Init() {
  zx_status_t status = Write16(kSoftResetCommand);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to send reset command: %d", status);
    return status;
  }

  zx::nanosleep(zx::deadline_after(kResetTime));
  return ZX_OK;
}

zx::status<float> Shtv3Device::ReadTemperature() {
  zx_status_t status = Write16(kStartMeasurementCommand);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to send measurement command: %d", status);
    return zx::error(status);
  }

  // Only read the temperature measurement, skip the CRC and humidity bytes.
  zx::status<uint16_t> temp_data = zx::ok(0);
  for (int i = 0; i < kMeasurementRetries; i++) {
    if ((temp_data = Read16()).is_ok()) {
      break;
    }

    zx::nanosleep(zx::deadline_after(kMeasurementRetryInterval));
  }

  if (temp_data.is_error()) {
    zxlogf(ERROR, "Timed out waiting for temperature measurement: %d", temp_data.error_value());
    return zx::error(temp_data.error_value());
  }

  const float temp_value = static_cast<float>(temp_data.value()) * 175;
  return zx::ok((temp_value / 65536) - 45);
}

zx::status<uint16_t> Shtv3Device::Read16() {
  uint16_t value;
  zx_status_t status =
      i2c_.WriteReadSync(nullptr, 0, reinterpret_cast<uint8_t*>(&value), sizeof(value));
  if (status != ZX_OK) {
    return zx::error(status);
  }

  return zx::ok(be16toh(value));
}

zx_status_t Shtv3Device::Write16(uint16_t value) {
  value = htobe16(value);
  return i2c_.WriteSync(reinterpret_cast<uint8_t*>(&value), sizeof(value));
}

static constexpr zx_driver_ops_t shtv3_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Shtv3Device::Create;
  return ops;
}();

}  // namespace temperature

ZIRCON_DRIVER(shtv3, temperature::shtv3_driver_ops, "zircon", "0.1");

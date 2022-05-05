// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/drivers/codecs/da7219/da7219.h"

#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/i2c.h>

#include "src/devices/lib/acpi/client.h"
#include "src/media/audio/drivers/codecs/da7219/da7219-bind.h"

namespace {
// Book 0
constexpr uint8_t kRegChipId1 = 0x81;       // CHIP_ID1.
constexpr uint8_t kRegChipId2 = 0x82;       // CHIP_ID2.
constexpr uint8_t kRegChipRevision = 0x83;  // CHIP_REVISION.

}  // namespace

namespace audio {

zx_status_t Da7219::Shutdown() { return ZX_OK; }

zx::status<DriverIds> Da7219::Initialize() {
  // Check IDs.
  uint8_t chip_id1 = 0;
  zx_status_t status = ReadReg(kRegChipId1, &chip_id1);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read CHIP ID part 1 %s", zx_status_get_string(status));
    return zx::error(ZX_ERR_INTERNAL);
  }
  uint8_t chip_id2 = 0;
  status = ReadReg(kRegChipId2, &chip_id2);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read CHIP ID part 2: %s", zx_status_get_string(status));
    return zx::error(ZX_ERR_INTERNAL);
  }
  uint8_t chip_revision = 0;
  status = ReadReg(kRegChipRevision, &chip_revision);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read CHIP revision: %s", zx_status_get_string(status));
    return zx::error(ZX_ERR_INTERNAL);
  }
  constexpr uint8_t kSupportedChipId1 = 0x23;
  constexpr uint8_t kSupportedChipId2 = 0x93;
  if (chip_id1 != kSupportedChipId1 || chip_id2 != kSupportedChipId2) {
    zxlogf(ERROR, "Found not supported CHIP ids 0x%02X:0x%02X", chip_id1, chip_id2);
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  zxlogf(INFO, "Found device %02X:%02X:%02X", chip_id1, chip_id2, chip_revision);

  return zx::ok(DriverIds{
      .vendor_id = PDEV_VID_DIALOG,
      .device_id = PDEV_DID_DIALOG_DA7219,
  });
}

zx_status_t Da7219::Reset() { return ZX_ERR_NOT_SUPPORTED; }

Info Da7219::GetInfo() {
  return {.unique_id = "", .manufacturer = "Dialog", .product_name = "DA7219"};
}

zx_status_t Da7219::Stop() { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Da7219::Start() { return ZX_ERR_NOT_SUPPORTED; }

DaiSupportedFormats Da7219::GetDaiFormats() { return {}; }

GainFormat Da7219::GetGainFormat() {
  return {
      .min_gain = 0,
      .max_gain = 0,
      .gain_step = 0,
      .can_mute = false,
      .can_agc = false,
  };
}

void Da7219::SetGainState(GainState gain_state) {}

GainState Da7219::GetGainState() { return {}; }

zx::status<CodecFormatInfo> Da7219::SetDaiFormat(const DaiFormat& format) {
  CodecFormatInfo info = {};
  return zx::ok(std::move(info));
}

zx_status_t Da7219::Bind(void* ctx, zx_device_t* dev) {
  auto client = acpi::Client::Create(dev);
  if (client.is_ok()) {
    ddk::I2cChannel i2c(dev, "i2c000");
    if (!i2c.is_valid()) {
      zxlogf(ERROR, "Could not get i2c protocol");
      return ZX_ERR_NO_RESOURCES;
    }

    return SimpleCodecServer::CreateAndAddToDdk<Da7219>(dev, std::move(i2c));
  }
  return ZX_ERR_NOT_SUPPORTED;
}

void Da7219::DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }

void Da7219::DdkRelease() { delete this; }

zx_status_t Da7219::WriteReg(uint8_t reg, uint8_t value) {
  uint8_t write_buf[2];
  write_buf[0] = reg;
  write_buf[1] = value;
// #define TRACE_I2C
#ifdef TRACE_I2C
  printf("Writing register 0x%02X to value 0x%02X\n", reg, value);
#endif
  return WriteRegs(write_buf, std::size(write_buf));
}

zx_status_t Da7219::WriteRegs(uint8_t* regs, size_t count) {
  constexpr uint8_t kNumberOfRetries = 2;
  constexpr zx::duration kRetryDelay = zx::msec(1);
  auto ret = i2c_.WriteSyncRetries(regs, count, kNumberOfRetries, kRetryDelay);
  if (ret.status != ZX_OK) {
    if (count == 2) {
      zxlogf(ERROR, "I2C write reg 0x%02X error %d, %d retries", regs[0], ret.status, ret.retries);
    } else {
      zxlogf(ERROR, "I2C write error %d, %d retries", ret.status, ret.retries);
    }
  }
  return ret.status;
}

zx_status_t Da7219::ReadReg(uint8_t reg, uint8_t* value) {
  constexpr uint8_t kNumberOfRetries = 2;
  constexpr zx::duration kRetryDelay = zx::msec(1);
  ddk::I2cChannel::StatusRetries ret =
      i2c_.WriteReadSyncRetries(&reg, 1, value, 1, kNumberOfRetries, kRetryDelay);
  if (ret.status != ZX_OK) {
    zxlogf(ERROR, "I2C read reg 0x%02X error %s, %d retries", reg, zx_status_get_string(ret.status),
           ret.retries);
  }
#ifdef TRACE_I2C
  printf("Read register 0x%02X, value %02X\n", reg, *value);
#endif
  return ret.status;
}

zx_status_t Da7219::UpdateReg(uint8_t reg, uint8_t mask, uint8_t value) {
  uint8_t old_value = 0;
  auto status = ReadReg(reg, &old_value);
  if (status != ZX_OK) {
    return status;
  }
  return WriteReg(reg, (old_value & ~mask) | (value & mask));
}

static zx_driver_ops_t da7219_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Da7219::Bind;
  return ops;
}();

}  // namespace audio

ZIRCON_DRIVER(Da7219, audio::da7219_driver_ops, "zircon", "0.1");

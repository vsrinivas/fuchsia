// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "max98373.h"

#include <fuchsia/hardware/i2c/c/banjo.h>
#include <lib/ddk/platform-defs.h>
#include <lib/simple-codec/simple-codec-helper.h>

#include <algorithm>
#include <memory>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

#include "src/devices/lib/acpi/client.h"
#include "src/media/audio/drivers/codecs/max98373/max98373-bind.h"

namespace {

// clang-format off
constexpr uint16_t kRegReset                   = 0x2000;
constexpr uint16_t kRegGlobalEnable            = 0x20ff;
constexpr uint16_t kRegPcmInterfaceFormat      = 0x2024;
constexpr uint16_t kRegPcmInterfaceClockRatio  = 0x2026;
constexpr uint16_t kRegPcmInterfaceSampleRate  = 0x2027;
constexpr uint16_t kRegPcmInterfaceInput       = 0x202b;
constexpr uint16_t kRegDigitalVol              = 0x203d;
constexpr uint16_t kRegSpkPathAndDspEnable     = 0x2043;
constexpr uint16_t kRegRevId                   = 0x21ff;

constexpr uint8_t kRegSpkPathAndDspEnableSpkOn = 0x01;
constexpr uint8_t kRegGlobalEnableOn           = 0x01;
constexpr uint8_t kRegResetReset               = 0x01;
// clang-format on

}  // namespace

namespace audio {

// TODO(andresoportus): Add handling for the other formats supported by this codec.
static const std::vector<uint32_t> kSupportedNumberOfChannels = {2};
static const std::vector<SampleFormat> kSupportedSampleFormats = {SampleFormat::PCM_SIGNED};
static const std::vector<FrameFormat> kSupportedFrameFormats = {FrameFormat::I2S};
static const std::vector<uint32_t> kSupportedRates = {48'000};
static const std::vector<uint8_t> kSupportedBitsPerSlot = {32};
static const std::vector<uint8_t> kSupportedBitsPerSample = {32};
static const audio::DaiSupportedFormats kSupportedDaiFormats = {
    .number_of_channels = kSupportedNumberOfChannels,
    .sample_formats = kSupportedSampleFormats,
    .frame_formats = kSupportedFrameFormats,
    .frame_rates = kSupportedRates,
    .bits_per_slot = kSupportedBitsPerSlot,
    .bits_per_sample = kSupportedBitsPerSample,
};

int Max98373::Thread() {
  auto status = HardwareReset();
  if (status != ZX_OK && status != ZX_ERR_NOT_SUPPORTED) {  // Ok if not supported.
    return thrd_error;
  }
  return thrd_success;
}

zx_status_t Max98373::HardwareReset() {
  fbl::AutoLock lock(&lock_);
  if (codec_reset_.is_valid()) {
    codec_reset_.Write(0);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));
    codec_reset_.Write(1);
    zx_nanosleep(zx_deadline_after(ZX_MSEC(3)));
    return ZX_OK;
  }
  zxlogf(INFO, "No support for GPIO reset the codec");
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Max98373::Reset() {
  fbl::AutoLock lock(&lock_);
  auto status = WriteReg(kRegReset, kRegResetReset);
  if (status != ZX_OK) {
    return status;
  }
  zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));

  uint8_t buffer;
  status = ReadReg(kRegRevId, &buffer);
  if (status == ZX_OK && buffer != 0x43) {
    zxlogf(ERROR, "Unexpected Rev Id 0x%02X", buffer);
    return ZX_ERR_INTERNAL;
  }

  // TODO(83399): These parameters and all configuration should be done via the
  // audio hardware codec FIDL API.
  constexpr float initial_gain = -20.f;
  constexpr struct {
    uint16_t reg;
    uint8_t value;
  } kDefaults[] = {
      {kRegGlobalEnable, kRegGlobalEnableOn},
      {kRegSpkPathAndDspEnable, kRegSpkPathAndDspEnableSpkOn},
      {kRegDigitalVol, static_cast<uint8_t>(-initial_gain * 2.f)},
      {kRegPcmInterfaceInput, 0x01},       // PCM DIN enable.
      {kRegPcmInterfaceClockRatio, 0x08},  // TDM 8 channels, 256 BCLK to LRCLK.
      {kRegPcmInterfaceFormat, 0xd8},      // TDM0 mode, 32 bits words.
      {kRegPcmInterfaceSampleRate, 0x08},  // 48KHz.
  };
  for (auto& i : kDefaults) {
    auto status = WriteReg(i.reg, i.value);
    if (status != ZX_OK) {
      return status;
    }
  }

  initialized_ = true;
  zxlogf(INFO, "audio: codec max98373 initialized");
  return status;
}

zx::status<DriverIds> Max98373::Initialize() {
  auto ids = DriverIds{
      .vendor_id = PDEV_VID_MAXIM,
      .device_id = PDEV_DID_MAXIM_MAX98373,
  };
  auto thunk = [](void* arg) -> int { return reinterpret_cast<Max98373*>(arg)->Thread(); };
  int rc = thrd_create_with_name(&thread_, thunk, this, "max98373-thread");
  if (rc != thrd_success) {
    return zx::error(rc);
  }
  zx_status_t status = Reset();
  if (status != ZX_OK) {
    return zx::error(status);
  }

  return zx::ok(ids);
}

zx_status_t Max98373::Shutdown() {
  thrd_join(thread_, NULL);
  return ZX_OK;
}

zx_status_t Max98373::Create(zx_device_t* parent) {
  auto client = acpi::Client::Create(parent);
  if (client.is_ok()) {
    ddk::I2cChannel i2c(parent, "i2c000");
    if (!i2c.is_valid()) {
      zxlogf(ERROR, "Could not get i2c protocol");
      return ZX_ERR_NO_RESOURCES;
    }

    // No GPIO control.
    auto dev = SimpleCodecServer::Create<Max98373>(parent, i2c, ddk::GpioProtocolClient{});
    dev.release();  // devmgr is now in charge of the memory for dev.
    return ZX_OK;
  }

  ddk::I2cChannel i2c(parent, "i2c");
  if (!i2c.is_valid()) {
    zxlogf(ERROR, "Could not get i2c protocol");
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::GpioProtocolClient gpio(parent, "gpio-enable");
  if (!gpio.is_valid()) {
    zxlogf(ERROR, "Could not get gpio protocol");
    return ZX_ERR_NO_RESOURCES;
  }

  auto dev = SimpleCodecServer::Create<Max98373>(parent, i2c, gpio);

  // devmgr is now in charge of the memory for dev.
  dev.release();
  return ZX_OK;
}

Info Max98373::GetInfo() {
  return {.unique_id = "", .manufacturer = "Maxim", .product_name = "MAX98373"};
}

bool Max98373::IsBridgeable() { return false; }

void Max98373::SetBridgedMode(bool enable_bridged_mode) {
  // TODO(andresoportus): Add support and report true in CodecIsBridgeable.
}

DaiSupportedFormats Max98373::GetDaiFormats() { return kSupportedDaiFormats; }

zx::status<CodecFormatInfo> Max98373::SetDaiFormat(const DaiFormat& format) {
  if (!IsDaiFormatSupported(format, kSupportedDaiFormats)) {
    zxlogf(ERROR, "unsupported format");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  return zx::ok(CodecFormatInfo{});
}

GainFormat Max98373::GetGainFormat() {
  return {
      .min_gain = kMinGain,
      .max_gain = kMaxGain,
      .gain_step = kGainStep,
      .can_mute = true,
      .can_agc = false,
  };
}

void Max98373::SetGainState(GainState gain_state) {
  fbl::AutoLock lock(&lock_);
  float gain = std::clamp(gain_state.gain, kMinGain, kMaxGain);
  uint8_t gain_reg = static_cast<uint8_t>(-gain * 2.f);
  zx_status_t status = WriteReg(kRegDigitalVol, gain_reg);
  if (status != ZX_OK) {
    return;
  }
  if (gain_state.agc_enabled) {
    zxlogf(ERROR, "AGC enable not supported");
    gain_state.agc_enabled = false;
  }
  gain_state_ = gain_state;
}

GainState Max98373::GetGainState() { return gain_state_; }

zx_status_t Max98373::WriteReg(uint16_t reg, uint8_t value) {
  uint8_t write_buffer[3];
  write_buffer[0] = static_cast<uint8_t>((reg >> 8) & 0xff);
  write_buffer[1] = static_cast<uint8_t>((reg >> 0) & 0xff);
  write_buffer[2] = value;
//#define TRACE_I2C
#ifdef TRACE_I2C
  printf("Writing register 0x%02X to value 0x%02X\n", reg, value);
  auto status = i2c_.WriteSync(write_buffer, countof(write_buffer));
  if (status != ZX_OK) {
    printf("Could not I2C write %d\n", status);
    return status;
  }
  uint8_t buffer = 0;
  i2c_.WriteReadSync(write_buffer, countof(write_buffer) - 1, &buffer, 1);
  if (status != ZX_OK) {
    printf("Could not I2C read %d\n", status);
    return status;
  }
  printf("Read register just written 0x%04X, value 0x%02X\n", reg, buffer);
  return ZX_OK;
#else
  constexpr uint8_t kNumberOfRetries = 2;
  constexpr zx::duration kRetryDelay = zx::msec(1);
  auto ret =
      i2c_.WriteSyncRetries(write_buffer, countof(write_buffer), kNumberOfRetries, kRetryDelay);
  if (ret.status != ZX_OK) {
    zxlogf(ERROR, "I2C write reg 0x%02X error %d, %d retries", reg, ret.status, ret.retries);
  }
  return ret.status;
#endif
}

zx_status_t Max98373::ReadReg(uint16_t reg, uint8_t* value) {
  uint8_t write_buffer[2];
  write_buffer[0] = static_cast<uint8_t>((reg >> 8) & 0xff);
  write_buffer[1] = static_cast<uint8_t>((reg >> 0) & 0xff);
  constexpr uint8_t kNumberOfRetries = 2;
  constexpr zx::duration kRetryDelay = zx::msec(1);
  auto ret = i2c_.WriteReadSyncRetries(write_buffer, sizeof(write_buffer), value, 1,
                                       kNumberOfRetries, kRetryDelay);
  if (ret.status != ZX_OK) {
    zxlogf(ERROR, "I2C read reg 0x%02X error %d, %d retries", reg, ret.status, ret.retries);
  }
#ifdef TRACE_I2C
  printf("Read register 0x%04X, value 0x%02X\n", reg, *value);
#endif
  return ret.status;
}

zx_status_t max98373_bind(void* ctx, zx_device_t* parent) { return Max98373::Create(parent); }

static zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = max98373_bind;
  return ops;
}();

}  // namespace audio

ZIRCON_DRIVER(max98373, audio::driver_ops, "zircon", "0.1");

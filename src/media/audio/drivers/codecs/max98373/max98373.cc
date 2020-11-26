// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "max98373.h"

#include <lib/simple-codec/simple-codec-helper.h>

#include <algorithm>
#include <memory>

#include <ddk/platform-defs.h>
#include <ddk/protocol/i2c.h>
#include <ddktl/protocol/composite.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

#include "src/media/audio/drivers/codecs/max98373/max98373-bind.h"

namespace {

// clang-format off
constexpr uint16_t kRegReset                   = 0x2000;
constexpr uint16_t kRegGlobalEnable            = 0x20ff;
constexpr uint16_t kRegPcmInterfaceFormat      = 0x2024;
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
  if (status != ZX_OK) {
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
  zxlogf(ERROR, "%s Could not hardware reset the codec", __FILE__);
  return ZX_ERR_INTERNAL;
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
    zxlogf(ERROR, "%s Unexpected Rev Id 0x%02X", __FILE__, buffer);
    return ZX_ERR_INTERNAL;
  }

  constexpr float initial_gain = -20.f;
  constexpr struct {
    uint16_t reg;
    uint8_t value;
  } kDefaults[] = {
      {kRegGlobalEnable, kRegGlobalEnableOn},
      {kRegSpkPathAndDspEnable, kRegSpkPathAndDspEnableSpkOn},
      {kRegDigitalVol, static_cast<uint8_t>(-initial_gain * 2.f)},
      {kRegPcmInterfaceInput, 0x01},       // PCM DIN enable.
      {kRegPcmInterfaceFormat, 0xc0},      // I2S 32 bits. LRCLK starts low.
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
  return zx::ok(ids);
}

zx_status_t Max98373::Shutdown() {
  thrd_join(thread_, NULL);
  return ZX_OK;
}

zx_status_t Max98373::Create(zx_device_t* parent) {
  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "%s Could not get composite protocol", __FILE__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  ddk::I2cChannel i2c(composite, "i2c");
  if (!i2c.is_valid()) {
    zxlogf(ERROR, "%s Could not get i2c protocol", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::GpioProtocolClient gpio(composite, "gpio-enable");
  if (!gpio.is_valid()) {
    zxlogf(ERROR, "%s Could not get gpio protocol", __FILE__);
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

zx_status_t Max98373::SetDaiFormat(const DaiFormat& format) {
  if (!IsDaiFormatSupported(format, kSupportedDaiFormats)) {
    zxlogf(ERROR, "%s unsupported format", __FILE__);
    return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
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
    zxlogf(ERROR, "%s AGC enable not supported", __FILE__);
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
  printf("%s Writing register 0x%02X to value 0x%02X\n", __FILE__, reg, value);
  auto status = i2c_.WriteSync(write_buffer, countof(write_buffer));
  if (status != ZX_OK) {
    printf("%s Could not I2C write %d\n", __FILE__, status);
    return status;
  }
  uint8_t buffer = 0;
  i2c_.WriteReadSync(write_buffer, countof(write_buffer) - 1, &buffer, 1);
  if (status != ZX_OK) {
    printf("%s Could not I2C read %d\n", __FILE__, status);
    return status;
  }
  printf("%s Read register just written 0x%04X, value 0x%02X\n", __FILE__, reg, buffer);
  return ZX_OK;
#else
  constexpr uint8_t kNumberOfRetries = 2;
  constexpr zx::duration kRetryDelay = zx::msec(1);
  auto ret =
      i2c_.WriteSyncRetries(write_buffer, countof(write_buffer), kNumberOfRetries, kRetryDelay);
  if (ret.status != ZX_OK) {
    zxlogf(ERROR, "%s I2C write reg 0x%02X error %d, %d retries", __FILE__, reg, ret.status,
           ret.retries);
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
    zxlogf(ERROR, "%s I2C read reg 0x%02X error %d, %d retries", __FILE__, reg, ret.status,
           ret.retries);
  }
#ifdef TRACE_I2C
  printf("%s Read register 0x%04X, value 0x%02X\n", __FILE__, reg, *value);
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

ZIRCON_DRIVER(max98373, audio::driver_ops, "zircon", "0.1")

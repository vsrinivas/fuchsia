// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tas5782.h"

#include <lib/device-protocol/i2c.h>
#include <lib/simple-codec/simple-codec-helper.h>

#include <algorithm>
#include <memory>

#include <ddk/platform-defs.h>
#include <ddk/protocol/i2c.h>
#include <ddktl/protocol/composite.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

#include "src/media/audio/drivers/codecs/tas5782/ti_tas5782-bind.h"

namespace audio {

// TODO(andresoportus): Add handling for the other formats supported by this codec.
static const std::vector<uint32_t> kSupportedDaiNumberOfChannels = {2};
static const std::vector<SampleFormat> kSupportedDaiSampleFormats = {SampleFormat::PCM_SIGNED};
static const std::vector<FrameFormat> kSupportedDaiFrameFormats = {FrameFormat::I2S};
static const std::vector<uint32_t> kSupportedDaiRates = {48'000};
static const std::vector<uint8_t> kSupportedDaiBitsPerSlot = {32};
static const std::vector<uint8_t> kSupportedDaiBitsPerSample = {32};
static const audio::DaiSupportedFormats kSupportedDaiFormats = {
    .number_of_channels = kSupportedDaiNumberOfChannels,
    .sample_formats = kSupportedDaiSampleFormats,
    .frame_formats = kSupportedDaiFrameFormats,
    .frame_rates = kSupportedDaiRates,
    .bits_per_slot = kSupportedDaiBitsPerSlot,
    .bits_per_sample = kSupportedDaiBitsPerSample,
};

zx_status_t Tas5782::Reset() {
  fbl::AutoLock lock(&lock_);
  if (codec_mute_.is_valid()) {
    codec_mute_.Write(0);  // Set to "mute".
  }
  if (codec_reset_.is_valid()) {
    codec_reset_.Write(0);  // Reset.
    // Delay to be safe.
    zx_nanosleep(zx_deadline_after(zx::usec(1).get()));
    codec_reset_.Write(1);  // Set to "not reset".
    // Delay to be safe.
    zx_nanosleep(zx_deadline_after(zx::msec(10).get()));
  }
  constexpr uint8_t defaults[][2] = {
      {0x02, 0x10},  // Enter standby.
      {0x01, 0x11},  // Reset modules and registers.
      {0x0d, 0x10},  // The PLL reference clock is SCLK.
      {0x04, 0x01},  // PLL for MCLK setting.
      {0x28, 0x03},  // I2S, 32 bits.
      {0x2a, 0x22},  // Left DAC to Left ch, Right DAC to right channel.
      {0x02, 0x00},  // Exit standby.
  };
  for (auto& i : defaults) {
    auto status = WriteReg(i[0], i[1]);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s Failed to write I2C register 0x%02X", __FILE__, i[0]);
      return status;
    }
  }
  if (codec_mute_.is_valid()) {
    codec_mute_.Write(1);  // Set to "unmute".
  }
  initialized_ = true;
  return ZX_OK;
}

zx::status<DriverIds> Tas5782::Initialize() {
  return zx::ok(DriverIds{
      .vendor_id = PDEV_VID_TI,
      .device_id = PDEV_DID_TI_TAS5782,
  });
}

zx_status_t Tas5782::Shutdown() {
  if (codec_mute_.is_valid()) {
    codec_mute_.Write(0);  // Set to "mute".
  }
  if (codec_reset_.is_valid()) {
    codec_reset_.Write(0);  // Keep the codec in reset.
  }

  return ZX_OK;
}

zx_status_t Tas5782::Create(zx_device_t* parent) {
  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "%s Could not get composite protocol", __FILE__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Only I2C fragment is required.
  ddk::I2cChannel i2c(composite, "i2c");
  if (!i2c.is_valid()) {
    zxlogf(ERROR, "%s Could not get i2c protocol", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::GpioProtocolClient gpio_reset(composite, "gpio-reset");
  ddk::GpioProtocolClient gpio_mute(composite, "gpio-mute");

  auto dev = SimpleCodecServer::Create<Tas5782>(parent, i2c, gpio_reset, gpio_mute);

  // devmgr is now in charge of the memory for dev.
  dev.release();
  return ZX_OK;
}

Info Tas5782::GetInfo() {
  return {.unique_id = "", .manufacturer = "Texas Instruments", .product_name = "TAS5782m"};
}

bool Tas5782::IsBridgeable() { return false; }

void Tas5782::SetBridgedMode(bool enable_bridged_mode) {
  // TODO(andresoportus): Add support and report true in CodecIsBridgeable.
}

DaiSupportedFormats Tas5782::GetDaiFormats() { return kSupportedDaiFormats; }

zx_status_t Tas5782::SetDaiFormat(const DaiFormat& format) {
  if (!IsDaiFormatSupported(format, kSupportedDaiFormats)) {
    zxlogf(ERROR, "%s unsupported format", __FILE__);
    return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

GainFormat Tas5782::GetGainFormat() {
  return {
      .min_gain = kMinGain,
      .max_gain = kMaxGain,
      .gain_step = kGainStep,
      .can_mute = true,
      .can_agc = false,
  };
}

void Tas5782::SetGainState(GainState gain_state) {
  fbl::AutoLock lock(&lock_);
  float gain = std::clamp(gain_state.gain, kMinGain, kMaxGain);
  uint8_t gain_reg = static_cast<uint8_t>(48 - gain * 2);
  auto status = WriteReg(0x3d, gain_reg);  // Left gain.
  if (status != ZX_OK) {
    return;
  }
  status = WriteReg(0x3e, gain_reg);  // Right gain.
  if (status != ZX_OK) {
    return;
  }
  if (gain_state.agc_enabled) {
    zxlogf(ERROR, "%s AGC enable not supported", __FILE__);
    gain_state.agc_enabled = false;
  }
  gain_state_ = gain_state;
}

GainState Tas5782::GetGainState() { return gain_state_; }

zx_status_t Tas5782::WriteReg(uint8_t reg, uint8_t value) {
  uint8_t write_buf[2];
  write_buf[0] = reg;
  write_buf[1] = value;
//#define TRACE_I2C
#ifdef TRACE_I2C
  printf("Writing register 0x%02X to value 0x%02X\n", reg, value);
  auto status = i2c_.WriteSync(write_buf, 2);
  if (status != ZX_OK) {
    printf("Could not I2C write %d\n", status);
    return status;
  }
  uint8_t buffer = 0;
  status = i2c_.ReadSync(reg, &buffer, 1);
  if (status != ZX_OK) {
    printf("Could not I2C read %d\n", status);
    return status;
  }
  printf("Read register just written 0x%02X, value 0x%02X\n", reg, buffer);
  return ZX_OK;
#else
  constexpr uint8_t kNumberOfRetries = 2;
  constexpr zx::duration kRetryDelay = zx::msec(1);
  auto ret = i2c_.WriteSyncRetries(write_buf, countof(write_buf), kNumberOfRetries, kRetryDelay);
  if (ret.status != ZX_OK) {
    zxlogf(ERROR, "%s I2C write reg 0x%02X error %d, %d retries", __FILE__, reg, ret.status,
           ret.retries);
  }
  return ret.status;
#endif
}

zx_status_t tas5782_bind(void* ctx, zx_device_t* parent) { return Tas5782::Create(parent); }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = tas5782_bind;
  return ops;
}();

}  // namespace audio

ZIRCON_DRIVER(ti_tas5782, audio::driver_ops, "zircon", "0.1")

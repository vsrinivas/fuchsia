// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tas5707.h"

#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/simple-codec/simple-codec-helper.h>
#include <lib/zx/time.h>

#include <algorithm>
#include <memory>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

#include "src/media/audio/drivers/codecs/tas5707/ti_tas5707-bind.h"

namespace {
constexpr uint8_t kRegClockCtrl = 0x00;
constexpr uint8_t kRegSysCtrl1 = 0x03;
constexpr uint8_t kRegSerialDataInterface = 0x04;
constexpr uint8_t kRegSysCtrl2 = 0x05;
constexpr uint8_t kRegSoftMute = 0x06;
constexpr uint8_t kRegMasterVolume = 0x07;
constexpr uint8_t kRegChannelVol1 = 0x08;
constexpr uint8_t kRegChannelVol2 = 0x09;
constexpr uint8_t kRegFineMasterVolume = 0x0A;
constexpr uint8_t kRegVolumeConfig = 0x0E;
constexpr uint8_t kRegModulationLimit = 0x10;
constexpr uint8_t kRegIcDelayChannel1 = 0x11;
constexpr uint8_t kRegIcDelayChannel2 = 0x12;
constexpr uint8_t kRegIcDelayChannel3 = 0x13;
constexpr uint8_t kRegIcDelayChannel4 = 0x14;
constexpr uint8_t kRegStartStopPeriod = 0x1A;
constexpr uint8_t kRegOscTrimCtrl = 0x1B;
constexpr uint8_t kRegBankEndErr = 0x1C;

// gain_reg = 48 - gain * 2
// mute - 0xFF
// 0db  - 0x30
constexpr uint8_t kDefaultChannelVolume = 0x30;
}  // namespace

namespace audio {

// This codec offers a DAI interface with 2 channel I2S, even though it is a mono amp with the
// channel actually amplified specified via metadata for a particular product.
static const std::vector<uint32_t> kSupportedNumberOfChannels = {2};
static const std::vector<SampleFormat> kSupportedSampleFormats = {SampleFormat::PCM_SIGNED};
static const std::vector<FrameFormat> kSupportedFrameFormats = {FrameFormat::I2S};
static const std::vector<uint32_t> kSupportedRates = {48'000};
static const std::vector<uint8_t> kSupportedBitsPerSlot = {32};
static const std::vector<uint8_t> kSupportedBitsPerSample = {16};
static const audio::DaiSupportedFormats kSupportedDaiFormats = {
    .number_of_channels = kSupportedNumberOfChannels,
    .sample_formats = kSupportedSampleFormats,
    .frame_formats = kSupportedFrameFormats,
    .frame_rates = kSupportedRates,
    .bits_per_slot = kSupportedBitsPerSlot,
    .bits_per_sample = kSupportedBitsPerSample,
};

zx_status_t Tas5707::Shutdown() { return ZX_OK; }

zx::result<DriverIds> Tas5707::Initialize() {
  size_t actual = 0;
  auto status = device_get_metadata(parent(), DEVICE_METADATA_PRIVATE, &metadata_,
                                    sizeof(metadata_), &actual);
  if (status != ZX_OK || sizeof(metadata_) != actual) {
    zxlogf(ERROR, "device_get_metadata failed %d", status);
    return zx::error(status);
  }

  instance_count_ = metadata_.instance_count;

  return zx::ok(DriverIds{
      .vendor_id = PDEV_VID_TI,
      .device_id = PDEV_DID_TI_TAS5707,
      .instance_count = instance_count_,
  });
}

zx_status_t Tas5707::Reset() {
  zx_status_t status = WriteReg(kRegOscTrimCtrl, 0x00);
  if (status != ZX_OK) {
    zxlogf(ERROR, "instance %u - Could not reset oscillator: %s", instance_count_,
           zx_status_get_string(status));
    return status;
  }
  // Trim oscillator (write 0x00 to register 0x1B) and wait at least 50ms.
  zx::nanosleep(zx::deadline_after(zx::msec(55)));
  const uint8_t kDefaultsStart[][2] = {
      {kRegClockCtrl, 0x6c},
      {kRegSysCtrl1, 0xa0},
      {kRegSerialDataInterface, 0x05},
      {kRegSysCtrl2, 0x00},
      {kRegSoftMute, 0x00},
      {kRegMasterVolume, 0xFF},
      {kRegChannelVol1, kDefaultChannelVolume},
      {kRegChannelVol2, kDefaultChannelVolume},
      {kRegFineMasterVolume, 0x00},
      {kRegVolumeConfig, 0x91},
      {kRegModulationLimit, 0x02},
      {kRegIcDelayChannel1, 0xAC},
      {kRegIcDelayChannel2, 0x54},
      {kRegIcDelayChannel3, 0xAC},
      {kRegIcDelayChannel4, 0x54},
      {kRegStartStopPeriod, 0x0F},
      {kRegBankEndErr, 0x02},
  };
  for (auto& i : kDefaultsStart) {
    // 0x00 ~ 0x1C : 1 byte Write/Read
    if ((i[0] > kRegBankEndErr) || (i[0] == kRegOscTrimCtrl))
      continue;
    auto status = WriteReg(i[0], i[1]);
    if (status != ZX_OK) {
      zxlogf(ERROR, "instance %u - Failed to write I2C register 0x%02X", instance_count_, i[0]);
      return status;
    }
  }

  constexpr float kDefaultGainDb = -20.f;
  SetGainState({.gain = kDefaultGainDb, .muted = true});
  return ZX_OK;
}

Info Tas5707::GetInfo() {
  return {
      .unique_id = "",
      .manufacturer = "Texas Instruments",
      .product_name = "TAS5707",
  };
}

zx_status_t Tas5707::Stop() { return ZX_OK; }

zx_status_t Tas5707::Start() { return ZX_OK; }

DaiSupportedFormats Tas5707::GetDaiFormats() { return kSupportedDaiFormats; }

// this driver does not allow to change the slot used and does not check for the correct slot being
// specified
zx::result<CodecFormatInfo> Tas5707::SetDaiFormat(const DaiFormat& format) {
  if (!IsDaiFormatSupported(format, kSupportedDaiFormats)) {
    zxlogf(ERROR, "instance %u - unsupported format", instance_count_);
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  CodecFormatInfo info = {};
  return zx::ok(std::move(info));
}

GainFormat Tas5707::GetGainFormat() {
  return {
      .min_gain = kMinGain,
      .max_gain = kMaxGain,
      .gain_step = kGainStep,
      .can_mute = true,
      .can_agc = false,
  };
}

GainState Tas5707::GetGainState() { return gain_state_; }

void Tas5707::SetGainState(GainState gain_state) {
  float gain = std::clamp(gain_state.gain, kMinGain, kMaxGain);
  uint8_t gain_reg = static_cast<uint8_t>(48 - gain * 2);

  zx_status_t status = WriteReg(kRegMasterVolume, gain_reg);
  if (status != ZX_OK) {
    zxlogf(ERROR, "instance %u - Could not set Master Vol %s", instance_count_,
           zx_status_get_string(status));
    return;
  }

  gain_state_ = gain_state;
  status = WriteReg(kRegSoftMute, gain_state_.muted ? 0x3 : 0x0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "instance %u - Could not set mute state %s", instance_count_,
           zx_status_get_string(status));
    return;
  }
}

zx_status_t Tas5707::WriteReg(uint8_t reg, uint8_t value) {
  uint8_t write_buffer[2];
  write_buffer[0] = reg;
  write_buffer[1] = value;
// #define TRACE_I2C
#ifdef TRACE_I2C
  printf("Writing to instance/slot %u/%u register 0x%02X to value 0x%02X\n", instance_count_,
         tdm_slot_, reg, value);
#endif
  constexpr uint8_t kNumberOfRetries = 2;
  constexpr zx::duration kRetryDelay = zx::msec(1);
  auto ret =
      i2c_.WriteSyncRetries(write_buffer, std::size(write_buffer), kNumberOfRetries, kRetryDelay);
  if (ret.status != ZX_OK) {
    zxlogf(ERROR, "instance %u - I2C write reg 0x%02X error %d, %d retries", instance_count_, reg,
           ret.status, ret.retries);
  }
  return ret.status;
}

zx_status_t Tas5707::ReadReg(uint8_t reg, uint8_t* value) {
  constexpr uint8_t kNumberOfRetries = 2;
  constexpr zx::duration kRetryDelay = zx::msec(1);
  auto ret = i2c_.WriteReadSyncRetries(&reg, 1, value, 1, kNumberOfRetries, kRetryDelay);
  if (ret.status != ZX_OK) {
    zxlogf(ERROR, "instance %u - I2C read reg 0x%02X error %d, %d retries", instance_count_, reg,
           ret.status, ret.retries);
  }
#ifdef TRACE_I2C
  printf("instance %u - Read register 0x%02X, value %02X\n", instance_count_, reg, *value);
#endif
  return ret.status;
}

zx_status_t tas5707_bind(void* ctx, zx_device_t* parent) {
  ddk::I2cChannel i2c(parent, "i2c");
  if (!i2c.is_valid()) {
    zxlogf(ERROR, "tas5707: Could not get i2c protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  return SimpleCodecServer::CreateAndAddToDdk<Tas5707>(parent, std::move(i2c));
}

static zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = tas5707_bind;
  return ops;
}();
}  // namespace audio

// clang-format off
ZIRCON_DRIVER(ti_tas5707, audio::driver_ops, "zircon", "0.1");

// clang-format on

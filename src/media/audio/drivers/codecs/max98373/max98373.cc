// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "max98373.h"

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
constexpr uint16_t kRegReset                        = 0x2000;
constexpr uint16_t kRegGlobalEnable                 = 0x20ff;
constexpr uint16_t kRegPcmInterfaceFormat           = 0x2024;
constexpr uint16_t kRegPcmInterfaceClockRatio       = 0x2026;
constexpr uint16_t kRegPcmInterfaceSampleRate       = 0x2027;
constexpr uint16_t kRegPcmInterfaceDigitalMonoMixer = 0x2029;
constexpr uint16_t kRegPcmInterfaceInput            = 0x202b;
constexpr uint16_t kRegDigitalVol                   = 0x203d;
constexpr uint16_t kRegSpkPathAndDspEnable          = 0x2043;
constexpr uint16_t kRegRevId                        = 0x21ff;

constexpr uint8_t kRegSpkPathAndDspEnableSpkOn      = 0x01;
constexpr uint8_t kRegGlobalEnableOn                = 0x01;
constexpr uint8_t kRegResetReset                    = 0x01;
// clang-format on

}  // namespace

namespace audio {

static const std::vector<uint32_t> kSupportedNumberOfChannels = {2, 4, 8, 16};
static const std::vector<SampleFormat> kSupportedSampleFormats = {SampleFormat::PCM_SIGNED};
static const std::vector<FrameFormat> kSupportedFrameFormats = {FrameFormat::TDM1, FrameFormat::I2S,
                                                                FrameFormat::STEREO_LEFT};
static const std::vector<uint32_t> kSupportedRates = {16'000, 22'050, 24'000, 32'000,
                                                      44'100, 48'000, 88'200, 96'000};

static const std::vector<uint8_t> kSupportedBitsPerSlot = {16, 24, 32};
static const std::vector<uint8_t> kSupportedBitsPerSample = {16, 24, 32};
static const audio::DaiSupportedFormats kSupportedDaiFormats = {
    .number_of_channels = kSupportedNumberOfChannels,
    .sample_formats = kSupportedSampleFormats,
    .frame_formats = kSupportedFrameFormats,
    .frame_rates = kSupportedRates,
    .bits_per_slot = kSupportedBitsPerSlot,
    .bits_per_sample = kSupportedBitsPerSample,
};

zx_status_t Max98373::HardwareReset() {
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

  constexpr float initial_gain = -20.f;
  constexpr struct {
    uint16_t reg;
    uint8_t value;
  } kDefaults[] = {
      {kRegGlobalEnable, kRegGlobalEnableOn},
      {kRegSpkPathAndDspEnable, kRegSpkPathAndDspEnableSpkOn},
      {kRegDigitalVol, static_cast<uint8_t>(-initial_gain * 2.f)},
      {kRegPcmInterfaceInput, 0x01},  // PCM DIN enable.
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

zx::result<DriverIds> Max98373::Initialize() {
  auto ids = DriverIds{
      .vendor_id = PDEV_VID_MAXIM,
      .device_id = PDEV_DID_MAXIM_MAX98373,
  };

  zx_status_t status = HardwareReset();
  if (status != ZX_OK && status != ZX_ERR_NOT_SUPPORTED) {  // Ok if not supported.
    return zx::error(status);
  }
  status = Reset();
  if (status != ZX_OK) {
    return zx::error(status);
  }

  return zx::ok(ids);
}

zx_status_t Max98373::Shutdown() { return ZX_OK; }

zx_status_t Max98373::Create(zx_device_t* parent) {
  auto client = acpi::Client::Create(parent);
  if (client.is_ok()) {
    ddk::I2cChannel i2c(parent, "i2c000");
    if (!i2c.is_valid()) {
      zxlogf(ERROR, "Could not get i2c protocol");
      return ZX_ERR_NO_RESOURCES;
    }

    // No GPIO control.
    return SimpleCodecServer::CreateAndAddToDdk<Max98373>(parent, std::move(i2c),
                                                          ddk::GpioProtocolClient{});
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

  return SimpleCodecServer::CreateAndAddToDdk<Max98373>(parent, std::move(i2c), std::move(gpio));
}

Info Max98373::GetInfo() {
  return {.unique_id = "", .manufacturer = "Maxim", .product_name = "MAX98373"};
}

DaiSupportedFormats Max98373::GetDaiFormats() { return kSupportedDaiFormats; }

uint8_t Max98373::getTdmClockRatio(uint32_t number_of_channels, uint8_t bits_per_slot) {
  uint8_t clock_ratio = 0;
  // BCLKs per LRCLK for PCM.
  constexpr uint32_t kBclkPerLrclk[] = {32, 48, 64, 96, 128, 192, 256, 384, 512, 320};
  constexpr uint8_t kFirstBclkPerLrclk = 2;
  const uint32_t bits_per_frame = number_of_channels * static_cast<uint32_t>(bits_per_slot);
  size_t i = 0;
  for (; i < std::size(kBclkPerLrclk); ++i) {
    if (bits_per_frame == kBclkPerLrclk[i]) {
      clock_ratio = static_cast<uint8_t>(kFirstBclkPerLrclk + i);
      break;
    }
  }
  ZX_ASSERT_MSG(i != std::size(kBclkPerLrclk),
                "Must have supported number of channels and bits per slot");
  return clock_ratio;
}

zx::result<CodecFormatInfo> Max98373::SetDaiFormat(const DaiFormat& format) {
  if (!IsDaiFormatSupported(format, kSupportedDaiFormats)) {
    zxlogf(ERROR, "unsupported format");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  if (__builtin_popcountl(format.channels_to_use_bitmask) != 1) {  // only one bit can be set.
    zxlogf(ERROR, "unsupported bits to use bitmask, more than one bit set 0x%016lX",
           format.channels_to_use_bitmask);
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  // Find the first bit set starting at the least significant bit position by counting 0s.
  int slot_to_use = __builtin_ctzl(format.channels_to_use_bitmask);
  constexpr int kMaxNumberOfTdmChannelsSupported = 16;
  if (slot_to_use >= kMaxNumberOfTdmChannelsSupported) {
    zxlogf(ERROR, "unsupported bits to use bitmask, slot (%d) to high", slot_to_use);
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // Use "Mono Mixer Channel 0 Source Select" to pick the slot.
  zx_status_t status =
      WriteReg(kRegPcmInterfaceDigitalMonoMixer, static_cast<uint8_t>(slot_to_use));
  if (status != ZX_OK) {
    return zx::error(status);
  }

  // Configure DAI format.
  uint8_t clock_ratio = 0;
  switch (format.frame_format) {
    case fuchsia::hardware::audio::DaiFrameFormatStandard::I2S:
    case fuchsia::hardware::audio::DaiFrameFormatStandard::STEREO_LEFT:
      clock_ratio = 4;  // 64 BCLKs per LRCLK for PCM.
      break;
    case fuchsia::hardware::audio::DaiFrameFormatStandard::TDM1: {
      clock_ratio = getTdmClockRatio(format.number_of_channels, format.bits_per_slot);
    } break;
    default:
      ZX_ASSERT_MSG(0, "Must have supported frame format");
  }
  status = WriteReg(kRegPcmInterfaceClockRatio, clock_ratio);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  uint8_t data_width = 0;
  switch (format.bits_per_sample) {
    case 16:
      data_width = 1;
      break;
    case 24:
      data_width = 2;
      break;
    case 32:
      data_width = 3;
      break;
    default:
      ZX_ASSERT_MSG(0, "Must have supported bits per sample");
  }
  uint32_t pcm_format = 0;
  constexpr uint8_t kI2s = 0;
  constexpr uint8_t kStereoLeft = 1;
  constexpr uint8_t kTdm = 3;
  switch (format.frame_format) {
    case fuchsia::hardware::audio::DaiFrameFormatStandard::I2S:
      pcm_format = (data_width << 6) | (kI2s << 3);
      break;
    case fuchsia::hardware::audio::DaiFrameFormatStandard::STEREO_LEFT:
      pcm_format = (data_width << 6) | (kStereoLeft << 3);
      break;
    case fuchsia::hardware::audio::DaiFrameFormatStandard::TDM1:
      pcm_format = (data_width << 6) | (kTdm << 3);
      break;
    default:
      ZX_ASSERT_MSG(0, "Must have supported frame format");
  }
  status = WriteReg(kRegPcmInterfaceFormat, static_cast<uint8_t>(pcm_format));
  if (status != ZX_OK) {
    return zx::error(status);
  }

  uint8_t rate = 0;
  switch (format.frame_rate) {
    // clang-format off
    case 16'000: rate = 3; break;
    case 22'050: rate = 4; break;
    case 24'000: rate = 5; break;
    case 32'000: rate = 6; break;
    case 44'100: rate = 7; break;
    case 48'000: rate = 8; break;
    case 88'200: rate = 9; break;
    case 96'000: rate = 10; break;
      // clang-format on
    default:
      ZX_ASSERT_MSG(0, "Must have supported rate");
  }
  status = WriteReg(kRegPcmInterfaceSampleRate, rate);
  if (status != ZX_OK) {
    return zx::error(status);
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
// #define TRACE_I2C
#ifdef TRACE_I2C
  printf("Writing register 0x%02X to value 0x%02X\n", reg, value);
#endif
  constexpr uint8_t kNumberOfRetries = 2;
  constexpr zx::duration kRetryDelay = zx::msec(1);
  auto ret =
      i2c_.WriteSyncRetries(write_buffer, std::size(write_buffer), kNumberOfRetries, kRetryDelay);
  if (ret.status != ZX_OK) {
    zxlogf(ERROR, "I2C write reg 0x%02X error %d, %d retries", reg, ret.status, ret.retries);
  }
  return ret.status;
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

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tas5782.h"

#include <lib/device-protocol/i2c.h>

#include <algorithm>
#include <memory>

#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <ddk/protocol/i2c.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

#include "src/media/audio/drivers/codecs/tas5782/ti_tas5782-bind.h"

namespace {
// TODO(andresoportus): Add handling for the other formats supported by this codec.
static const uint32_t supported_n_channels[] = {2};
static const sample_format_t supported_sample_formats[] = {SAMPLE_FORMAT_PCM_SIGNED};
static const frame_format_t supported_frame_formats[] = {FRAME_FORMAT_I2S};
static const uint32_t supported_rates[] = {48000};
static const uint8_t supported_bits_per_slot[] = {32};
static const uint8_t supported_bits_per_sample[] = {32};
static const dai_supported_formats_t kSupportedDaiFormats = {
    .number_of_channels_list = supported_n_channels,
    .number_of_channels_count = countof(supported_n_channels),
    .sample_formats_list = supported_sample_formats,
    .sample_formats_count = countof(supported_sample_formats),
    .frame_formats_list = supported_frame_formats,
    .frame_formats_count = countof(supported_frame_formats),
    .frame_rates_list = supported_rates,
    .frame_rates_count = countof(supported_rates),
    .bits_per_slot_list = supported_bits_per_slot,
    .bits_per_slot_count = countof(supported_bits_per_slot),
    .bits_per_sample_list = supported_bits_per_sample,
    .bits_per_sample_count = countof(supported_bits_per_sample),
};

enum {
  FRAGMENT_I2C,
  FRAGMENT_RESET_GPIO,
  FRAGMENT_MUTE_GPIO,
  FRAGMENT_COUNT,
};

}  // namespace

namespace audio {

zx_status_t Tas5782::ResetAndInitialize() {
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

zx_status_t Tas5782::Bind() {
  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_TI},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_TI_TAS5782},
  };
  return DdkAdd(ddk::DeviceAddArgs("tas5782").set_props(props));
}

void Tas5782::Shutdown() {
  thrd_join(thread_, NULL);
  if (codec_mute_.is_valid()) {
    codec_mute_.Write(0);  // Set to "mute".
  }
  if (codec_reset_.is_valid()) {
    codec_reset_.Write(0);  // Keep the codec in reset.
  }
}

zx_status_t Tas5782::Create(zx_device_t* parent) {
  composite_protocol_t composite;

  auto status = device_get_protocol(parent, ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Could not get composite protocol", __FILE__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_device_t* fragments[FRAGMENT_COUNT] = {};
  size_t actual = 0;
  composite_get_fragments(&composite, fragments, countof(fragments), &actual);
  // Only I2C fragment is required.
  if (actual < 1) {
    zxlogf(ERROR, "%s Could not get fragments", __FILE__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  fbl::AllocChecker ac;
  auto dev = std::unique_ptr<Tas5782>(new (&ac) Tas5782(parent, fragments[FRAGMENT_I2C],
                                                        fragments[FRAGMENT_RESET_GPIO],
                                                        fragments[FRAGMENT_MUTE_GPIO]));
  if (!ac.check()) {
    zxlogf(ERROR, "%s Could not allocate memory", __FILE__);
    return ZX_ERR_NO_MEMORY;
  }
  status = dev->Bind();
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the memory for dev.
  dev.release();
  return ZX_OK;
}

void Tas5782::CodecReset(codec_reset_callback callback, void* cookie) {
  fbl::AutoLock lock(&lock_);
  auto status = ResetAndInitialize();
  callback(cookie, status);
}

void Tas5782::CodecGetInfo(codec_get_info_callback callback, void* cookie) {
  info_t info;
  info.unique_id = "";
  info.manufacturer = "Texas Instruments";
  info.product_name = "TAS5782m";
  callback(cookie, &info);
}

void Tas5782::CodecIsBridgeable(codec_is_bridgeable_callback callback, void* cookie) {
  callback(cookie, false);
}

void Tas5782::CodecSetBridgedMode(bool enable_bridged_mode,
                                  codec_set_bridged_mode_callback callback, void* cookie) {
  // TODO(andresoportus): Add support and report true in CodecIsBridgeable.
  callback(cookie);
}

void Tas5782::CodecGetDaiFormats(codec_get_dai_formats_callback callback, void* cookie) {
  callback(cookie, ZX_OK, &kSupportedDaiFormats, 1);
}

void Tas5782::CodecSetDaiFormat(const dai_format_t* format, codec_set_dai_format_callback callback,
                                void* cookie) {
  if (!initialized_) {
    callback(cookie, ZX_ERR_UNAVAILABLE);
    return;
  }
  if (format == nullptr) {
    callback(cookie, ZX_ERR_INVALID_ARGS);
    return;
  }

  // Only allow 2 channels.
  if (format->number_of_channels != 2) {
    zxlogf(ERROR, "%s DAI format number of channels not supported", __FILE__);
    callback(cookie, ZX_ERR_NOT_SUPPORTED);
    return;
  }
  if (format->channels_to_use_count != 2 || format->channels_to_use_list == nullptr ||
      format->channels_to_use_list[0] != 0 || format->channels_to_use_list[1] != 1) {
    zxlogf(ERROR, "%s DAI format channels to use not supported", __FILE__);
    callback(cookie, ZX_ERR_NOT_SUPPORTED);
    return;
  }

  // Only I2S.
  if (format->sample_format != SAMPLE_FORMAT_PCM_SIGNED ||
      format->frame_format != FRAME_FORMAT_I2S) {
    zxlogf(ERROR, "%s DAI format format not supported", __FILE__);
    callback(cookie, ZX_ERR_NOT_SUPPORTED);
    return;
  }

  // Check rates allowed.
  size_t i = 0;
  for (i = 0; i < kSupportedDaiFormats.frame_rates_count; ++i) {
    if (format->frame_rate == kSupportedDaiFormats.frame_rates_list[i]) {
      break;
    }
  }
  if (i == kSupportedDaiFormats.frame_rates_count) {
    zxlogf(ERROR, "%s DAI format rates not supported", __FILE__);
    callback(cookie, ZX_ERR_NOT_SUPPORTED);
    return;
  }

  // Allow only 32 bits samples and slot.
  if (format->bits_per_sample != 32 || format->bits_per_slot != 32) {
    callback(cookie, ZX_ERR_NOT_SUPPORTED);
    return;
  }
  callback(cookie, ZX_OK);
}

void Tas5782::CodecGetGainFormat(codec_get_gain_format_callback callback, void* cookie) {
  gain_format_t format = {};
  format.type = GAIN_TYPE_DECIBELS;
  format.min_gain = kMinGain;
  format.max_gain = kMaxGain;
  format.gain_step = kGainStep;
  callback(cookie, &format);
}

void Tas5782::CodecSetGainState(const gain_state_t* gain_state,
                                codec_set_gain_state_callback callback, void* cookie) {
  if (!initialized_) {
    zxlogf(ERROR, "%s Couldn't set gain, not initialized yet", __FILE__);
    callback(cookie);
    return;
  }
  fbl::AutoLock lock(&lock_);
  float gain = std::clamp(gain_state->gain, kMinGain, kMaxGain);
  uint8_t gain_reg = static_cast<uint8_t>(48 - gain * 2);
  auto status = WriteReg(0x3d, gain_reg);  // Left gain.
  if (status != ZX_OK) {
    callback(cookie);
    return;
  }
  status = WriteReg(0x3e, gain_reg);  // Right gain.
  if (status != ZX_OK) {
    callback(cookie);
    return;
  }
  current_gain_ = gain;
  callback(cookie);
}

void Tas5782::CodecGetGainState(codec_get_gain_state_callback callback, void* cookie) {
  gain_state_t gain_state = {};
  gain_state.gain = current_gain_;
  gain_state.muted = false;
  gain_state.agc_enable = false;
  callback(cookie, &gain_state);
}

void Tas5782::CodecGetPlugState(codec_get_plug_state_callback callback, void* cookie) {
  plug_state_t plug_state = {};
  plug_state.hardwired = true;
  plug_state.plugged = true;
  callback(cookie, &plug_state);
}

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
  return i2c_.WriteSync(write_buf, 2);
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

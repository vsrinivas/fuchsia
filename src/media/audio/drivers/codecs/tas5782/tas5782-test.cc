// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tas5782.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-i2c/mock-i2c.h>
#include <lib/sync/completion.h>

#include <mock/ddktl/protocol/gpio.h>
#include <zxtest/zxtest.h>

namespace audio {

static constexpr uint32_t kCodecTimeoutSecs = 1;

struct Tas5782Test : public Tas5782 {
  explicit Tas5782Test(zx_device_t* device, const ddk::I2cChannel& i2c,
                       const ddk::GpioProtocolClient& codec_reset,
                       const ddk::GpioProtocolClient& codec_mute)
      : Tas5782(device, i2c, codec_reset, codec_mute) {
    initialized_ = true;
  }
  zx_status_t CodecSetDaiFormat(dai_format_t* format) {
    struct AsyncOut {
      sync_completion_t completion;
      zx_status_t status;
    } out;
    Tas5782::CodecSetDaiFormat(
        format,
        [](void* ctx, zx_status_t s) {
          AsyncOut* out = reinterpret_cast<AsyncOut*>(ctx);
          out->status = s;
          sync_completion_signal(&out->completion);
        },
        &out);
    auto status = sync_completion_wait(&out.completion, zx::sec(kCodecTimeoutSecs).get());
    if (status != ZX_OK) {
      return status;
    }
    return out.status;
  }
};

TEST(Tas5782Test, GoodSetDai) {
  mock_i2c::MockI2c mock_i2c;
  ddk::I2cChannel i2c(mock_i2c.GetProto());
  ddk::GpioProtocolClient unused_gpio0, unused_gpio1;
  Tas5782Test device(nullptr, std::move(i2c), unused_gpio0, unused_gpio1);

  uint32_t channels[] = {0, 1};
  dai_format_t format = {};
  format.number_of_channels = 2;
  format.channels_to_use_list = channels;
  format.channels_to_use_count = countof(channels);
  format.sample_format = SAMPLE_FORMAT_PCM_SIGNED;
  format.frame_format = FRAME_FORMAT_I2S;
  format.frame_rate = 48000;
  format.bits_per_channel = 32;
  format.bits_per_sample = 32;
  EXPECT_OK(device.CodecSetDaiFormat(&format));
}

TEST(Tas5782Test, BadSetDai) {
  mock_i2c::MockI2c mock_i2c;
  ddk::I2cChannel i2c(mock_i2c.GetProto());
  ddk::GpioProtocolClient unused_gpio0, unused_gpio1;
  Tas5782Test device(nullptr, std::move(i2c), unused_gpio0, unused_gpio1);

  // No format at all.
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, device.CodecSetDaiFormat(nullptr));

  // Blank format.
  dai_format format = {};
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device.CodecSetDaiFormat(&format));

  // Almost good format (wrong frame_format).
  uint32_t channels[] = {0, 1};
  format.number_of_channels = 2;
  format.channels_to_use_list = channels;
  format.channels_to_use_count = countof(channels);
  format.sample_format = SAMPLE_FORMAT_PCM_SIGNED;
  format.frame_format = FRAME_FORMAT_STEREO_LEFT;  // This must fail, only I2S supported.
  format.frame_rate = 48000;
  format.bits_per_channel = 32;
  format.bits_per_sample = 32;
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device.CodecSetDaiFormat(&format));

  // Almost good format (wrong channels).
  format.frame_format = FRAME_FORMAT_I2S;  // Restore I2S frame format.
  format.number_of_channels = 1;
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device.CodecSetDaiFormat(&format));

  // Almost good format (wrong rate).
  format.number_of_channels = 2;  // Restore channel count;
  format.frame_rate = 7890;
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device.CodecSetDaiFormat(&format));

  mock_i2c.VerifyAndClear();
}

TEST(Tas5782Test, GetDai) {
  mock_i2c::MockI2c mock_i2c;
  ddk::I2cChannel i2c(mock_i2c.GetProto());
  ddk::GpioProtocolClient unused_gpio0, unused_gpio1;
  Tas5782 device(nullptr, std::move(i2c), unused_gpio0, unused_gpio1);
  struct AsyncOut {
    sync_completion_t completion;
    zx_status_t status;
  } out;

  device.CodecGetDaiFormats(
      [](void* ctx, zx_status_t status, const dai_supported_formats_t* formats_list,
         size_t formats_count) {
        AsyncOut* out = reinterpret_cast<AsyncOut*>(ctx);
        EXPECT_EQ(formats_count, 1);
        EXPECT_EQ(formats_list[0].number_of_channels_count, 1);
        EXPECT_EQ(formats_list[0].number_of_channels_list[0], 2);
        EXPECT_EQ(formats_list[0].sample_formats_count, 1);
        EXPECT_EQ(formats_list[0].sample_formats_list[0], SAMPLE_FORMAT_PCM_SIGNED);
        EXPECT_EQ(formats_list[0].frame_formats_count, 1);
        EXPECT_EQ(formats_list[0].frame_formats_list[0], FRAME_FORMAT_I2S);
        EXPECT_EQ(formats_list[0].frame_rates_count, 1);
        EXPECT_EQ(formats_list[0].frame_rates_list[0], 48000);
        EXPECT_EQ(formats_list[0].bits_per_channel_count, 1);
        EXPECT_EQ(formats_list[0].bits_per_channel_list[0], 32);
        EXPECT_EQ(formats_list[0].bits_per_sample_count, 1);
        EXPECT_EQ(formats_list[0].bits_per_sample_list[0], 32);
        out->status = status;
        sync_completion_signal(&out->completion);
      },
      &out);
  EXPECT_OK(out.status);
  EXPECT_OK(sync_completion_wait(&out.completion, zx::sec(kCodecTimeoutSecs).get()));
}

TEST(Tas5782Test, GetInfo) {
  ddk::I2cChannel unused_i2c;
  ddk::GpioProtocolClient unused_gpio0, unused_gpio1;
  Tas5782 device(nullptr, std::move(unused_i2c), unused_gpio0, unused_gpio1);

  device.CodecGetInfo(
      [](void* ctx, const info_t* info) {
        EXPECT_EQ(strcmp(info->unique_id, ""), 0);
        EXPECT_EQ(strcmp(info->manufacturer, "Texas Instruments"), 0);
        EXPECT_EQ(strcmp(info->product_name, "TAS5782m"), 0);
      },
      nullptr);
}

TEST(Tas5782Test, BridgedMode) {
  ddk::I2cChannel unused_i2c;
  ddk::GpioProtocolClient unused_gpio0, unused_gpio1;
  Tas5782 device(nullptr, std::move(unused_i2c), unused_gpio0, unused_gpio1);

  device.CodecIsBridgeable(
      [](void* ctx, bool supports_bridged_mode) { EXPECT_EQ(supports_bridged_mode, false); },
      nullptr);
}

TEST(Tas5782Test, GetGainFormat) {
  ddk::I2cChannel unused_i2c;
  ddk::GpioProtocolClient unused_gpio0, unused_gpio1;
  Tas5782 device(nullptr, std::move(unused_i2c), unused_gpio0, unused_gpio1);

  device.CodecGetGainFormat(
      [](void* ctx, const gain_format_t* format) {
        EXPECT_EQ(format->type, GAIN_TYPE_DECIBELS);
        EXPECT_EQ(format->min_gain, -103.0);
        EXPECT_EQ(format->max_gain, 24.0);
        EXPECT_EQ(format->gain_step, 0.5);
      },
      nullptr);
}

TEST(Tas5782Test, GetPlugState) {
  ddk::I2cChannel unused_i2c;
  ddk::GpioProtocolClient unused_gpio0, unused_gpio1;
  Tas5782 device(nullptr, std::move(unused_i2c), unused_gpio0, unused_gpio1);

  device.CodecGetPlugState(
      [](void* ctx, const plug_state_t* state) {
        EXPECT_EQ(state->hardwired, true);
        EXPECT_EQ(state->plugged, true);
      },
      nullptr);
}

TEST(Tas5782Test, Init) {
  mock_i2c::MockI2c mock_i2c;
  mock_i2c
      .ExpectWriteStop({0x02, 0x10})   // Enter standby.
      .ExpectWriteStop({0x01, 0x11})   // Reset modules and registers.
      .ExpectWriteStop({0x0d, 0x10})   // The PLL reference clock is SCLK.
      .ExpectWriteStop({0x04, 0x01})   // PLL for MCLK setting.
      .ExpectWriteStop({0x28, 0x03})   // I2S, 32 bits.
      .ExpectWriteStop({0x2a, 0x22})   // Left DAC to Left ch, Right DAC to right channel.
      .ExpectWriteStop({0x02, 0x00});  // Exit standby.

  ddk::I2cChannel i2c(mock_i2c.GetProto());
  ddk::MockGpio mock_gpio0, mock_gpio1;
  ddk::GpioProtocolClient gpio0(mock_gpio0.GetProto());
  ddk::GpioProtocolClient gpio1(mock_gpio1.GetProto());
  mock_gpio0.ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);  // Reset, set to 0 and then to 1.
  mock_gpio1.ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1);  // Set to mute and then to unmute..

  Tas5782 device(fake_ddk::kFakeParent, std::move(i2c), gpio0, gpio1);
  device.Bind();
  // Delay to test we don't do other init I2C writes in another thread.
  zx::nanosleep(zx::deadline_after(zx::msec(100)));
  device.ResetAndInitialize();
  mock_i2c.VerifyAndClear();
  mock_gpio0.VerifyAndClear();
  mock_gpio1.VerifyAndClear();
}
}  // namespace audio

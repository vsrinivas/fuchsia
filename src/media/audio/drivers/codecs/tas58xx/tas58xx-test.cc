// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tas58xx.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-i2c/mock-i2c.h>
#include <lib/sync/completion.h>

#include <ddk/binding.h>
#include <ddk/platform-defs.h>
#include <zxtest/zxtest.h>

namespace audio {

static constexpr uint32_t kCodecTimeoutSecs = 1;

struct Tas58xxTestDevice : public Tas58xx {
  explicit Tas58xxTestDevice(const ddk::I2cChannel& i2c)
      : Tas58xx(fake_ddk::kFakeParent, i2c, false) {
    initialized_ = true;
  }
  zx_status_t CodecSetDaiFormat(dai_format_t* format) {
    struct AsyncOut {
      sync_completion_t completion;
      zx_status_t status;
    } out;
    Tas58xx::CodecSetDaiFormat(
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

TEST(Tas58xxTest, GoodSetDai) {
  mock_i2c::MockI2c mock_i2c;
  ddk::I2cChannel i2c(mock_i2c.GetProto());
  Tas58xxTestDevice device(std::move(i2c));

  uint32_t channels[] = {0, 1};
  dai_format_t format = {};
  format.number_of_channels = 2;
  format.channels_to_use_list = channels;
  format.channels_to_use_count = countof(channels);
  format.sample_format = SAMPLE_FORMAT_PCM_SIGNED;
  format.justify_format = JUSTIFY_FORMAT_JUSTIFY_I2S;
  format.frame_rate = 48000;
  format.bits_per_channel = 32;

  format.bits_per_sample = 32;
  mock_i2c.ExpectWriteStop({0x33, 0x03});  // 32 bits.
  EXPECT_OK(device.CodecSetDaiFormat(&format));

  mock_i2c.ExpectWriteStop({0x33, 0x00});  // 16 bits.
  format.bits_per_sample = 16;
  EXPECT_OK(device.CodecSetDaiFormat(&format));

  mock_i2c.VerifyAndClear();
}

TEST(Tas58xxTest, BadSetDai) {
  mock_i2c::MockI2c mock_i2c;
  ddk::I2cChannel i2c(mock_i2c.GetProto());
  Tas58xxTestDevice device(std::move(i2c));

  // No format at all.
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, device.CodecSetDaiFormat(nullptr));

  // Blank format.
  dai_format format = {};
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device.CodecSetDaiFormat(&format));

  // Almost good format (wrong justify_format).
  uint32_t channels[] = {0, 1};
  format.number_of_channels = 2;
  format.channels_to_use_list = channels;
  format.channels_to_use_count = countof(channels);
  format.sample_format = SAMPLE_FORMAT_PCM_SIGNED;
  format.justify_format = JUSTIFY_FORMAT_JUSTIFY_LEFT;  // This must fail, only I2S supported.
  format.frame_rate = 48000;
  format.bits_per_channel = 32;
  format.bits_per_sample = 32;
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device.CodecSetDaiFormat(&format));

  // Almost good format (wrong channels).
  format.justify_format = JUSTIFY_FORMAT_JUSTIFY_I2S;  // Restore I2S justify format.
  format.channels_to_use_count = 1;
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device.CodecSetDaiFormat(&format));

  // Almost good format (wrong rate).
  format.channels_to_use_count = 2;  // Restore channel count;
  format.frame_rate = 1234;
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device.CodecSetDaiFormat(&format));

  mock_i2c.VerifyAndClear();
}

TEST(Tas58xxTest, GetDai) {
  mock_i2c::MockI2c mock_i2c;
  ddk::I2cChannel i2c(mock_i2c.GetProto());
  Tas58xxTestDevice device(std::move(i2c));
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
        EXPECT_EQ(formats_list[0].justify_formats_count, 1);
        EXPECT_EQ(formats_list[0].justify_formats_list[0], JUSTIFY_FORMAT_JUSTIFY_I2S);
        EXPECT_EQ(formats_list[0].frame_rates_count, 1);
        EXPECT_EQ(formats_list[0].frame_rates_list[0], 48000);
        EXPECT_EQ(formats_list[0].bits_per_channel_count, 2);
        EXPECT_EQ(formats_list[0].bits_per_channel_list[0], 16);
        EXPECT_EQ(formats_list[0].bits_per_channel_list[1], 32);
        EXPECT_EQ(formats_list[0].bits_per_sample_count, 2);
        EXPECT_EQ(formats_list[0].bits_per_sample_list[0], 16);
        EXPECT_EQ(formats_list[0].bits_per_sample_list[1], 32);
        out->status = status;
        sync_completion_signal(&out->completion);
      },
      &out);
  EXPECT_OK(out.status);
  EXPECT_OK(sync_completion_wait(&out.completion, zx::sec(kCodecTimeoutSecs).get()));
}

TEST(Tas58xxTest, GetInfo5805) {
  mock_i2c::MockI2c mock_i2c;

  // Get Info.
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x00});

  ddk::I2cChannel i2c(mock_i2c.GetProto());
  Tas58xxTestDevice device(std::move(i2c));

  device.CodecGetInfo(
      [](void* ctx, const info_t* info) {
        EXPECT_EQ(strcmp(info->unique_id, ""), 0);
        EXPECT_EQ(strcmp(info->manufacturer, "Texas Instruments"), 0);
        EXPECT_EQ(strcmp(info->product_name, "TAS5805m"), 0);
      },
      nullptr);
}

TEST(Tas58xxTest, GetInfo5825) {
  mock_i2c::MockI2c mock_i2c;

  // Get Info.
  mock_i2c.ExpectWrite({0x67}).ExpectReadStop({0x95});

  ddk::I2cChannel i2c(mock_i2c.GetProto());
  Tas58xxTestDevice device(std::move(i2c));

  device.CodecGetInfo(
      [](void* ctx, const info_t* info) {
        EXPECT_EQ(strcmp(info->unique_id, ""), 0);
        EXPECT_EQ(strcmp(info->manufacturer, "Texas Instruments"), 0);
        EXPECT_EQ(strcmp(info->product_name, "TAS5825m"), 0);
      },
      nullptr);
}

TEST(Tas58xxTest, BridgedMode) {
  ddk::I2cChannel unused_i2c;
  Tas58xxTestDevice device(std::move(unused_i2c));

  device.CodecIsBridgeable(
      [](void* ctx, bool supports_bridged_mode) { EXPECT_EQ(supports_bridged_mode, false); },
      nullptr);
}

TEST(Tas58xxTest, GetGainFormat) {
  ddk::I2cChannel unused_i2c;
  Tas58xxTestDevice device(std::move(unused_i2c));

  device.CodecGetGainFormat(
      [](void* ctx, const gain_format_t* format) {
        EXPECT_EQ(format->type, GAIN_TYPE_DECIBELS);
        EXPECT_EQ(format->min_gain, -103.0);
        EXPECT_EQ(format->max_gain, 24.0);
        EXPECT_EQ(format->gain_step, 0.5);
      },
      nullptr);
}

TEST(Tas58xxTest, GetPlugState) {
  ddk::I2cChannel unused_i2c;
  Tas58xxTestDevice device(std::move(unused_i2c));

  device.CodecGetPlugState(
      [](void* ctx, const plug_state_t* state) {
        EXPECT_EQ(state->hardwired, true);
        EXPECT_EQ(state->plugged, true);
      },
      nullptr);
}

TEST(Tas58xxTest, Reset) {
  mock_i2c::MockI2c mock_i2c;

  mock_i2c
      .ExpectWriteStop({0x00, 0x00})   // Page 0.
      .ExpectWriteStop({0x7f, 0x00})   // book 0.
      .ExpectWriteStop({0x03, 0x02})   // HiZ, Enables DSP.
      .ExpectWriteStop({0x01, 0x11})   // Reset.
      .ExpectWriteStop({0x00, 0x00})   // Page 0.
      .ExpectWriteStop({0x7f, 0x00})   // book 0.
      .ExpectWriteStop({0x02, 0x01})   // Normal modulation, mono, no PBTL.
      .ExpectWriteStop({0x03, 0x03})   // Play,
      .ExpectWriteStop({0x00, 0x00})   // Page 0.
      .ExpectWriteStop({0x7f, 0x00})   // book 0.
      .ExpectWriteStop({0x78, 0x80});  // Clear analog fault.

  ddk::I2cChannel i2c(mock_i2c.GetProto());
  Tas58xxTestDevice device(std::move(i2c));
  device.Bind();
  // Delay to test we don't do other init I2C writes in another thread.
  zx::nanosleep(zx::deadline_after(zx::msec(100)));
  device.ResetAndInitialize();
  mock_i2c.VerifyAndClear();
}

TEST(Tas58xxTest, Pbtl) {
  mock_i2c::MockI2c mock_i2c;

  // Reset with PBTL mode on.
  mock_i2c
      .ExpectWriteStop({0x00, 0x00})   // Page 0.
      .ExpectWriteStop({0x7f, 0x00})   // book 0.
      .ExpectWriteStop({0x03, 0x02})   // HiZ, Enables DSP.
      .ExpectWriteStop({0x01, 0x11})   // Reset.
      .ExpectWriteStop({0x00, 0x00})   // Page 0.
      .ExpectWriteStop({0x7f, 0x00})   // book 0.
      .ExpectWriteStop({0x02, 0x05})   // Normal modulation, mono, PBTL.
      .ExpectWriteStop({0x03, 0x03})   // Play,
      .ExpectWriteStop({0x00, 0x00})   // Page 0.
      .ExpectWriteStop({0x7f, 0x00})   // book 0.
      .ExpectWriteStop({0x78, 0x80});  // Clear analog fault.

  ddk::I2cChannel i2c(mock_i2c.GetProto());
  Tas58xx device(nullptr, std::move(i2c), true);
  device.ResetAndInitialize();
  mock_i2c.VerifyAndClear();
}

}  // namespace audio

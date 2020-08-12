// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec.h"

// TODO(fxbug.dev/44249): Abstract Audio drivers controllers-codecs communications

namespace {
static bool IsFormatSupported(sample_format_t sample_format, justify_format_t justify_format,
                              uint32_t frame_rate, uint8_t bits_per_sample,
                              uint8_t bits_per_channel, const dai_supported_formats_t* formats) {
  size_t i = 0;
  for (i = 0; i < formats->sample_formats_count && formats->sample_formats_list[i] != sample_format;
       ++i) {
  }
  if (i == formats->sample_formats_count) {
    zxlogf(ERROR, "%s did not find wanted sample format", __FILE__);
    return false;
  }

  for (i = 0;
       i < formats->justify_formats_count && formats->justify_formats_list[i] != justify_format;
       ++i) {
  }
  if (i == formats->justify_formats_count) {
    zxlogf(ERROR, "%s did not find wanted justify format", __FILE__);
    return false;
  }

  for (i = 0; i < formats->frame_rates_count && formats->frame_rates_list[i] != frame_rate; ++i) {
  }
  if (i == formats->frame_rates_count) {
    zxlogf(ERROR, "%s did not find wanted sample rate", __FILE__);
    return false;
  }

  for (i = 0;
       i < formats->bits_per_sample_count && formats->bits_per_sample_list[i] != bits_per_sample;
       ++i) {
  }
  if (i == formats->bits_per_sample_count) {
    zxlogf(ERROR, "%s did not find wanted bits per sample", __FILE__);
    return false;
  }

  for (i = 0;
       i < formats->bits_per_channel_count && formats->bits_per_channel_list[i] != bits_per_channel;
       ++i) {
  }
  if (i == formats->bits_per_channel_count) {
    zxlogf(ERROR, "%s did not find wanted bits per channel", __FILE__);
    return false;
  }

  return true;
}
}  // namespace

namespace audio {
namespace nelson {

zx_status_t Codec::GetInfo() {
  sync_completion_t completion = {};
  proto_client_.GetInfo(
      [](void* ctx, const info_t* info) {
        auto* completion = reinterpret_cast<sync_completion_t*>(ctx);
        zxlogf(INFO, "audio: Found codec %s by %s", info->product_name, info->manufacturer);
        sync_completion_signal(completion);
      },
      &completion);
  auto status = sync_completion_wait(&completion, zx::sec(kCodecTimeoutSecs).get());
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s sync_completion_wait failed %d", __FILE__, status);
  }
  return status;
}

zx_status_t Codec::Reset() {
  struct AsyncOut {
    sync_completion_t completion;
    zx_status_t status;
  } out = {};
  proto_client_.Reset(
      [](void* ctx, zx_status_t status) {
        auto* out = reinterpret_cast<AsyncOut*>(ctx);
        out->status = status;
        sync_completion_signal(&out->completion);
      },
      &out);
  auto status = sync_completion_wait(&out.completion, zx::sec(kCodecTimeoutSecs).get());
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to reset %d", __FILE__, status);
  }
  return status;
}

zx_status_t Codec::SetNotBridged() {
  struct AsyncOut {
    sync_completion_t completion;
    bool supports_bridged_mode;
  } out = {};
  proto_client_.IsBridgeable(
      [](void* ctx, bool supports_bridged_mode) {
        auto* out = reinterpret_cast<AsyncOut*>(ctx);
        out->supports_bridged_mode = supports_bridged_mode;
        sync_completion_signal(&out->completion);
      },
      &out);
  auto status = sync_completion_wait(&out.completion, zx::sec(kCodecTimeoutSecs).get());
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed get bridging support %d", __FILE__, status);
    return status;
  }
  if (out.supports_bridged_mode) {
    proto_client_.SetBridgedMode(
        false, [](void*) {}, nullptr);
  }

  return status;
}

zx_status_t Codec::CheckExpectedDaiFormat() {
  AsyncOut out;
  proto_client_.GetDaiFormats(
      [](void* ctx, zx_status_t s, const dai_supported_formats_t* formats_list,
         size_t formats_count) {
        auto* out = reinterpret_cast<AsyncOut*>(ctx);
        out->status = s;
        if (out->status == ZX_OK) {
          size_t i = 0;
          for (; i < formats_count; ++i) {
            if (IsFormatSupported(kWantedSampleFormat, kWantedJustifyFormat, kWantedFrameRate,
                                  kWantedBitsPerSample, kWantedBitsPerChannel, &formats_list[i])) {
              break;
            }
          }
          out->status = i != formats_count ? ZX_OK : ZX_ERR_INTERNAL;
        }
        sync_completion_signal(&out->completion);
      },
      &out);
  auto status = sync_completion_wait(&out.completion, zx::sec(kCodecTimeoutSecs).get());
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to get DAI formats %d", __FILE__, status);
    return status;
  }
  if (out.status != ZX_OK) {
    zxlogf(ERROR, "%s did not find expected DAI formats %d", __FILE__, out.status);
  }
  return status;
}

zx_status_t Codec::SetDaiFormat(dai_format_t format) {
  AsyncOut out = {};
  proto_client_.SetDaiFormat(
      &format,
      [](void* ctx, zx_status_t s) {
        auto* out = reinterpret_cast<AsyncOut*>(ctx);
        out->status = s;
        sync_completion_signal(&out->completion);
      },
      &out);
  auto status = sync_completion_wait(&out.completion, zx::sec(kCodecTimeoutSecs).get());
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to get DAI formats %d", __FILE__, status);
    return status;
  }
  if (out.status != ZX_OK) {
    zxlogf(ERROR, "%s did not find expected DAI formats %d", __FILE__, out.status);
  }
  return status;
}

zx_status_t Codec::GetGainFormat(gain_format_t* format) {
  struct AsyncOut {
    sync_completion_t completion;
    gain_format_t format;
  } out = {};
  proto_client_.GetGainFormat(
      [](void* ctx, const gain_format_t* format) {
        auto* out = reinterpret_cast<AsyncOut*>(ctx);
        out->format = *format;
        sync_completion_signal(&out->completion);
      },
      &out);
  auto status = sync_completion_wait(&out.completion, zx::sec(kCodecTimeoutSecs).get());
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to get gain format %d", __FILE__, status);
    return status;
  }
  *format = out.format;
  return status;
}

zx_status_t Codec::GetGainState(gain_state_t* state) {
  struct AsyncOut {
    sync_completion_t completion;
    gain_state_t state;
  } out = {};
  proto_client_.GetGainState(
      [](void* ctx, const gain_state_t* state) {
        auto* out = reinterpret_cast<AsyncOut*>(ctx);
        out->state = *state;
        sync_completion_signal(&out->completion);
      },
      &out);
  auto status = sync_completion_wait(&out.completion, zx::sec(kCodecTimeoutSecs).get());
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to get gain state %d", __FILE__, status);
    return status;
  }
  *state = out.state;
  return status;
}

zx_status_t Codec::SetGainState(gain_state_t* state) {
  proto_client_.SetGainState(
      state, [](void* ctx) {}, nullptr);
  return ZX_OK;
}

}  // namespace nelson
}  // namespace audio

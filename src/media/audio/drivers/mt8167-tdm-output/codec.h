// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_MT8167_TDM_OUTPUT_CODEC_H_
#define SRC_MEDIA_AUDIO_DRIVERS_MT8167_TDM_OUTPUT_CODEC_H_

#include <lib/sync/completion.h>
#include <lib/zx/time.h>

#include <ddk/debug.h>
#include <ddktl/protocol/codec.h>

namespace audio {
namespace mt8167 {

static constexpr sample_format_t wanted_sample_format = SAMPLE_FORMAT_PCM_SIGNED;
static constexpr frame_format_t wanted_frame_format = FRAME_FORMAT_I2S;
static constexpr uint32_t wanted_frame_rate = 48000;
static constexpr uint8_t wanted_bits_per_sample = 32;
static constexpr uint8_t wanted_bits_per_channel = 32;

struct Codec {
  static constexpr uint32_t kCodecTimeoutSecs = 1;

  struct AsyncOut {
    sync_completion_t completion;
    zx_status_t status;
  };

  zx_status_t Reset();
  zx_status_t SetNotBridged();
  void CheckAndSetUnb();
  zx_status_t CheckExpectedDaiFormat();
  zx_status_t SetDaiFormat(dai_format_t format);
  zx_status_t GetGainFormat(gain_format_t* format);
  zx_status_t GetGainState(gain_state_t* state);
  zx_status_t SetGainState(gain_state_t* state);

  ddk::CodecProtocolClient proto_client_;
};

}  // namespace mt8167
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_MT8167_TDM_OUTPUT_CODEC_H_

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_NELSON_TDM_OUTPUT_CODEC_H_
#define SRC_MEDIA_AUDIO_DRIVERS_NELSON_TDM_OUTPUT_CODEC_H_

#include <lib/sync/completion.h>
#include <lib/zx/time.h>

#include <ddk/debug.h>
#include <ddktl/protocol/codec.h>

// TODO(fxbug.dev/44249): Abstract Audio drivers controllers-codecs communications

namespace audio {
namespace nelson {

constexpr sample_format_t kWantedSampleFormat = SAMPLE_FORMAT_PCM_SIGNED;
constexpr frame_format_t kWantedFrameFormat = FRAME_FORMAT_I2S;
constexpr uint32_t kWantedFrameRate = 48000;
constexpr uint8_t kWantedBitsPerSample = 32;
constexpr uint8_t kWantedBitsPerSlot = 32;

struct Codec {
  static constexpr uint32_t kCodecTimeoutSecs = 1;

  struct AsyncOut {
    sync_completion_t completion;
    zx_status_t status;
  };

  zx_status_t GetInfo();
  zx_status_t Reset();
  zx_status_t SetNotBridged();
  zx_status_t CheckExpectedDaiFormat();
  zx_status_t SetDaiFormat(dai_format_t format);
  zx_status_t GetGainFormat(gain_format_t* format);
  zx_status_t GetGainState(gain_state_t* state);
  zx_status_t SetGainState(gain_state_t* state);

  ddk::CodecProtocolClient proto_client_;
};

}  // namespace nelson
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_NELSON_TDM_OUTPUT_CODEC_H_

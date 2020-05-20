// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_TOOLS_AUDIO_DRIVER_CTL_WAV_SOURCE_H_
#define SRC_MEDIA_AUDIO_TOOLS_AUDIO_DRIVER_CTL_WAV_SOURCE_H_

#include <zircon/types.h>

#include <audio-utils/audio-stream.h>
#include <audio-utils/duration.h>

#include "wav-common.h"

class WAVSource : public WAVCommon, public audio::utils::AudioSource {
 public:
  using Duration = audio::utils::Duration;
  using LoopingDoneCallback = audio::utils::LoopingDoneCallback;

  WAVSource() {}
  zx_status_t Initialize(const char* filename, uint64_t channels_to_use_bitmask, Duration duration);

  // AudioSource interface
  zx_status_t GetFormat(AudioStream::Format* out_format) final;
  zx_status_t GetFrames(void* buffer, uint32_t buf_space, uint32_t* out_packed) final;
  bool finished() const final;

 private:
  uint32_t payload_len_ = 0;
  uint32_t payload_played_ = 0;
  AudioStream::Format audio_format_;
  Duration duration_ = {};
};

#endif  // SRC_MEDIA_AUDIO_TOOLS_AUDIO_DRIVER_CTL_WAV_SOURCE_H_

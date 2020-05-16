// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_TOOLS_AUDIO_DRIVER_CTL_GENERATED_SOURCE_H_
#define SRC_MEDIA_AUDIO_TOOLS_AUDIO_DRIVER_CTL_GENERATED_SOURCE_H_

#include <zircon/types.h>

#include <limits>

#include <audio-utils/audio-stream.h>
#include <audio-utils/duration.h>

class GeneratedSource : public audio::utils::AudioSource {
 public:
  static constexpr uint32_t kAllChannelsActive = std::numeric_limits<uint32_t>::max();

  using Duration = audio::utils::Duration;
  using LoopingDoneCallback = audio::utils::LoopingDoneCallback;

  virtual zx_status_t Init(float freq, float amp, Duration duration, uint32_t frame_rate,
                           uint32_t channels, uint32_t active, audio_sample_format_t sample_format);

  zx_status_t GetFormat(Format* out_format) final;
  zx_status_t GetFrames(void* buffer, uint32_t buf_space, uint32_t* out_packed) final;
  bool finished() const final;

 protected:
  virtual double GenerateValue(double pos) = 0;

  double pos_scalar_ = 1.0;

 private:
  using GetFramesThunk = zx_status_t (GeneratedSource::*)(void* buffer, uint32_t buf_space,
                                                          uint32_t* out_packed);

  template <audio_sample_format_t SAMPLE_FORMAT>
  zx_status_t InitInternal();

  template <audio_sample_format_t SAMPLE_FORMAT>
  zx_status_t GetFramesInternal(void* buffer, uint32_t buf_space, uint32_t* out_packed);

  uint64_t frames_to_produce_;
  uint64_t frames_produced_;
  double amp_;
  uint32_t frame_rate_;
  uint32_t channels_;
  uint32_t active_;
  uint32_t frame_size_;
  audio_sample_format_t sample_format_;
  GetFramesThunk get_frames_thunk_ = nullptr;
  Duration duration_ = {};
};

#endif  // SRC_MEDIA_AUDIO_TOOLS_AUDIO_DRIVER_CTL_GENERATED_SOURCE_H_

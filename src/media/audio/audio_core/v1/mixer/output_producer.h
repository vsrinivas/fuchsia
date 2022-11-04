// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIXER_OUTPUT_PRODUCER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIXER_OUTPUT_PRODUCER_H_

#include <fuchsia/media/cpp/fidl.h>

#include <memory>

#include "src/media/audio/lib/format2/stream_converter.h"

namespace media::audio {

class OutputProducer {
 public:
  static std::unique_ptr<OutputProducer> Select(
      const fuchsia::media::AudioStreamType& output_format);

  ~OutputProducer() = default;

  /**
   * Take frames of audio from the source intermediate buffer and convert them
   * to the proper sample format for the output buffer, clipping the audio as
   * needed in the process.
   *
   * @note It is assumed that the source intermediate mixing buffer has the same
   * number of channels and channel ordering as the output buffer.
   *
   * @param source_ptr A pointer to the normalized frames of audio to use as the
   * source.
   *
   * @param dest_void_ptr A pointer to the destination buffer whose frames match the
   * format described by output_format during the call to Select.
   *
   * @param frames The number of frames to produce.
   */
  void ProduceOutput(const float* source_ptr, void* dest_void_ptr, int64_t frames) const;

  /**
   * Fill a destination buffer with silence.
   *
   * @param dest_void_ptr A pointer to the destination buffer whose frames match the
   * format described by output_format during the call to Select.
   *
   * @param frames The number of frames to produce.
   */
  void FillWithSilence(void* dest_void_ptr, int64_t frames) const;

  const fuchsia::media::AudioStreamType& format() const { return format_; }
  int32_t channels() const { return channels_; }
  int32_t bytes_per_sample() const { return bytes_per_sample_; }
  int32_t bytes_per_frame() const { return bytes_per_frame_; }

  // Implementation detail. Use Select.
  OutputProducer(std::shared_ptr<::media_audio::StreamConverter> converter,
                 const fuchsia::media::AudioStreamType& output_format, int32_t bytes_per_sample);

 protected:
  std::shared_ptr<::media_audio::StreamConverter> converter_;
  fuchsia::media::AudioStreamType format_;
  int32_t channels_ = 0;
  int32_t bytes_per_sample_ = 0;
  int32_t bytes_per_frame_ = 0;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIXER_OUTPUT_PRODUCER_H_

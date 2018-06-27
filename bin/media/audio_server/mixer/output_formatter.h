// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_SERVER_MIXER_OUTPUT_FORMATTER_H_
#define GARNET_BIN_MEDIA_AUDIO_SERVER_MIXER_OUTPUT_FORMATTER_H_

#include <memory>

#include <fuchsia/media/cpp/fidl.h>

namespace media {
namespace audio {

class OutputFormatter;
using OutputFormatterPtr = std::unique_ptr<OutputFormatter>;

class OutputFormatter {
 public:
  static OutputFormatterPtr Select(
      const fuchsia::media::AudioMediaTypeDetailsPtr& output_format);

  virtual ~OutputFormatter() = default;

  /**
   * Take frames of audio from the source intermediate buffer and convert them
   * to the proper sample format for the output buffer, clipping the audio as
   * needed in the process.
   *
   * @note It is assumed that the source intermediate mixing buffer has the same
   * number of channels and channel ordering as the output buffer.
   *
   * @param source A pointer to the normalized frames of audio to use as the
   * source.
   *
   * @param dest A pointer to the destination buffer whose frames match the
   * format described by output_format during the call to Select.
   *
   * @param frames The number of frames to produce.
   */
  virtual void ProduceOutput(const float* source, void* dest,
                             uint32_t frames) const = 0;

  /**
   * Fill a destination buffer with silence.
   *
   * @param dest A pointer to the destination buffer whose frames match the
   * format described by output_format during the call to Select.
   *
   * @param frames The number of frames to produce.
   */
  virtual void FillWithSilence(void* dest, uint32_t frames) const = 0;

  const fuchsia::media::AudioMediaTypeDetailsPtr& format() const {
    return format_;
  }
  uint32_t channels() const { return channels_; }
  uint32_t bytes_per_sample() const { return bytes_per_sample_; }
  uint32_t bytes_per_frame() const { return bytes_per_frame_; }

 protected:
  OutputFormatter(const fuchsia::media::AudioMediaTypeDetailsPtr& output_format,
                  uint32_t bytes_per_sample);

  fuchsia::media::AudioMediaTypeDetailsPtr format_;
  uint32_t channels_ = 0;
  uint32_t bytes_per_sample_ = 0;
  uint32_t bytes_per_frame_ = 0;
};

}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_SERVER_MIXER_OUTPUT_FORMATTER_H_

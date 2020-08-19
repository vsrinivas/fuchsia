// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_FORMAT_FORMAT_H_
#define SRC_MEDIA_AUDIO_LIB_FORMAT_FORMAT_H_

#include <fuchsia/media/cpp/fidl.h>
#include <stdint.h>

#include "src/media/audio/lib/timeline/timeline_rate.h"

namespace media::audio {

template <fuchsia::media::AudioSampleFormat SampleFormat>
class TypedFormat;

// Format represents the format of audio data, and primarily includes a SampleFormat,
// a channel count, and a sample rate measured in frames/second.
class Format {
 public:
  static fit::result<Format> Create(fuchsia::media::AudioStreamType stream_type);

  template <fuchsia::media::AudioSampleFormat SampleFormat>
  static fit::result<TypedFormat<SampleFormat>> Create(uint32_t channels,
                                                       uint32_t frames_per_second);

  Format(const Format&) = default;
  Format& operator=(const Format&) = default;

  bool operator==(const Format& other) const;
  bool operator!=(const Format& other) const { return !(*this == other); }

  const fuchsia::media::AudioStreamType& stream_type() const { return stream_type_; }
  uint32_t channels() const { return stream_type_.channels; }
  uint32_t frames_per_second() const { return stream_type_.frames_per_second; }
  fuchsia::media::AudioSampleFormat sample_format() const { return stream_type_.sample_format; }

  const TimelineRate& frames_per_ns() const { return frames_per_ns_; }
  const TimelineRate& frame_to_media_ratio() const { return frame_to_media_ratio_; }
  uint32_t bytes_per_frame() const { return bytes_per_frame_; }
  uint32_t bytes_per_sample() const { return bytes_per_frame_ / channels(); }
  uint32_t valid_bits_per_channel() const { return valid_bits_per_channel_; }

 protected:
  Format(fuchsia::media::AudioStreamType stream_type);

  fuchsia::media::AudioStreamType stream_type_;
  TimelineRate frames_per_ns_;
  TimelineRate frame_to_media_ratio_;
  uint32_t bytes_per_frame_;
  uint32_t valid_bits_per_channel_;
};

// TypedFormat is a wrapper around Format that carries the underlying SampleFormat in its type,
// making it more convenient to use with AudioBuffer and other typed functions.
template <fuchsia::media::AudioSampleFormat SampleFormat>
class TypedFormat : public Format {
 private:
  friend class Format;
  TypedFormat(fuchsia::media::AudioStreamType stream_type) : Format(stream_type) {}
};

// Macro that expands to M(F) for all possible SampleFormats F.
#define INSTANTIATE_FOR_ALL_FORMATS(M)                  \
  M(fuchsia::media::AudioSampleFormat::UNSIGNED_8)      \
  M(fuchsia::media::AudioSampleFormat::SIGNED_16)       \
  M(fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32) \
  M(fuchsia::media::AudioSampleFormat::FLOAT)

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_FORMAT_FORMAT_H_

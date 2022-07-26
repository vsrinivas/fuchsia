// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_FORMAT2_FORMAT_H_
#define SRC_MEDIA_AUDIO_LIB_FORMAT2_FORMAT_H_

#include <fidl/fuchsia.mediastreams/cpp/natural_types.h>
#include <fidl/fuchsia.mediastreams/cpp/wire_types.h>
#include <lib/fpromise/result.h>
#include <stdint.h>

#include <ostream>
#include <string>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/timeline/timeline_rate.h"

namespace media_audio {

// Format wraps a `fuchsia.mediastreams/AudioFormat` FIDL struct.
class Format {
 public:
  // Creates a Format from a FIDL struct,
  // Returns an error if the struct contains invalid parameters.
  static fpromise::result<Format, std::string> Create(fuchsia_mediastreams::wire::AudioFormat fidl);

  // Creates a Format from a FIDL struct.
  // Crashes if the the struct contains invalid parameters.
  static Format CreateOrDie(fuchsia_mediastreams::wire::AudioFormat fidl);

  Format(const Format&) = default;
  Format& operator=(const Format&) = default;

  bool operator==(const Format& rhs) const;
  bool operator!=(const Format& rhs) const { return !(*this == rhs); }

  fuchsia_mediastreams::wire::AudioFormat ToFidl() const;
  fuchsia_mediastreams::wire::AudioSampleFormat sample_format() const { return sample_format_; }
  int64_t channels() const { return channels_; }
  int64_t frames_per_second() const { return frames_per_second_; }

  int64_t bytes_per_frame() const { return bytes_per_frame_; }
  int64_t bytes_per_sample() const { return bytes_per_frame_ / channels_; }
  int32_t valid_bits_per_sample() const { return valid_bits_per_sample_; }

  // Returns the frame rate as ratio of frames per nanoseconds.
  const media::TimelineRate& frames_per_ns() const { return frames_per_ns_; }
  const media::TimelineRate& frac_frames_per_ns() const { return frac_frames_per_ns_; }

  // Computes the number of integral frames for the given duration.
  // Rounds up by default.
  int64_t integer_frames_per(zx::duration duration,
                             media::TimelineRate::RoundingMode rounding_mode =
                                 ::media::TimelineRate::RoundingMode::Ceiling) const;

  // Computes the number of fractional frames for the given duration.
  // Rounds up by default.
  Fixed frac_frames_per(zx::duration duration,
                        media::TimelineRate::RoundingMode rounding_mode =
                            ::media::TimelineRate::RoundingMode::Ceiling) const;

  // Computes the number of bytes for the given duration.
  // Rounds up by default.
  int64_t bytes_per(zx::duration duration, media::TimelineRate::RoundingMode rounding_mode =
                                               media::TimelineRate::RoundingMode::Ceiling) const;

 private:
  Format(fuchsia_mediastreams::wire::AudioSampleFormat sample_format, int64_t channels,
         int64_t frames_per_second);

  // TODO(fxbug.dev/87651): include channel_layout once that is defined
  fuchsia_mediastreams::wire::AudioSampleFormat sample_format_;
  int64_t channels_;
  int64_t frames_per_second_;
  int64_t bytes_per_frame_;
  int32_t valid_bits_per_sample_;
  media::TimelineRate frames_per_ns_;
  media::TimelineRate frac_frames_per_ns_;
};

std::ostream& operator<<(std::ostream& out, const Format& format);

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_LIB_FORMAT2_FORMAT_H_

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_FORMAT2_FORMAT_H_
#define SRC_MEDIA_AUDIO_LIB_FORMAT2_FORMAT_H_

#include <fidl/fuchsia.audio/cpp/natural_types.h>
#include <fidl/fuchsia.audio/cpp/wire_types.h>
#include <fidl/fuchsia.mediastreams/cpp/wire_types.h>
#include <lib/fpromise/result.h>
#include <stdint.h>

#include <ostream>
#include <string>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/timeline/timeline_rate.h"

namespace media_audio {

// Format wraps a `fuchsia.audio.Format` FIDL table.
class Format {
 public:
  struct Args {
    fuchsia_audio::SampleType sample_type;
    int64_t channels;
    int64_t frames_per_second;
  };

  // Creates a Format from a wire FIDL object, a natural FIDL object, or an inline struct.
  // Returns an error if the struct contains invalid parameters.
  static fpromise::result<Format, std::string> Create(fuchsia_audio::wire::Format msg);
  static fpromise::result<Format, std::string> Create(fuchsia_audio::Format msg);
  static fpromise::result<Format, std::string> Create(Args args);

  // Creates a Format from a wire FIDL object, a natural FIDL object, or an inline struct.
  // Crashes if the the struct contains invalid parameters.
  static Format CreateOrDie(fuchsia_audio::wire::Format msg);
  static Format CreateOrDie(fuchsia_audio::Format msg);
  static Format CreateOrDie(Args args);

  // TODO(fxbug.dev/114919): Remove when fuchsia.audio.effects has migrated to the new types.
  static fpromise::result<Format, std::string> CreateLegacy(
      fuchsia_mediastreams::wire::AudioFormat msg);
  static Format CreateLegacyOrDie(fuchsia_mediastreams::wire::AudioFormat msg);

  Format(const Format&) = default;
  Format& operator=(const Format&) = default;

  bool operator==(const Format& rhs) const;
  bool operator!=(const Format& rhs) const { return !(*this == rhs); }

  fuchsia_audio::wire::Format ToWireFidl(fidl::AnyArena& arena) const;
  fuchsia_audio::Format ToNaturalFidl() const;
  // TODO(fxbug.dev/114919): Remove when fuchsia.audio.effects has migrated to the new types.
  fuchsia_mediastreams::wire::AudioFormat ToLegacyFidl() const;

  fuchsia_audio::SampleType sample_type() const { return sample_type_; }
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
                                 media::TimelineRate::RoundingMode::Ceiling) const;

  // Computes the number of fractional frames for the given duration.
  // Rounds up by default.
  Fixed frac_frames_per(zx::duration duration,
                        media::TimelineRate::RoundingMode rounding_mode =
                            media::TimelineRate::RoundingMode::Ceiling) const;

  // Computes the number of bytes for the given duration.
  // Rounds up by default.
  int64_t bytes_per(zx::duration duration, media::TimelineRate::RoundingMode rounding_mode =
                                               media::TimelineRate::RoundingMode::Ceiling) const;

  // Computes the duration that covers the given number of fractional frames.
  // Rounds up by default.
  zx::duration duration_per(Fixed frames, media::TimelineRate::RoundingMode rounding_mode =
                                              media::TimelineRate::RoundingMode::Ceiling) const;

 private:
  Format(fuchsia_audio::SampleType sample_type, int64_t channels, int64_t frames_per_second);

  // TODO(fxbug.dev/114436): include channel_layout
  fuchsia_audio::SampleType sample_type_;
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

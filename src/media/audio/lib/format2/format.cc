// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/format2/format.h"

#include <lib/syslog/cpp/macros.h>

#include <sstream>

#include <sdk/lib/fidl/cpp/enum.h>

#include "src/media/audio/lib/format2/fixed.h"

namespace media_audio {

using AudioSampleFormat = fuchsia_mediastreams::wire::AudioSampleFormat;
using TimelineRate = media::TimelineRate;

fpromise::result<Format, std::string> Format::Create(fuchsia_mediastreams::wire::AudioFormat fidl) {
  std::ostringstream err;

  switch (fidl.sample_format) {
    case AudioSampleFormat::kUnsigned8:
    case AudioSampleFormat::kSigned16:
    case AudioSampleFormat::kSigned24In32:
    case AudioSampleFormat::kSigned32:
    case AudioSampleFormat::kFloat:
      break;
    default:
      err << "bad sample_format '" << fidl::ToUnderlying(fidl.sample_format) << "'";
      return fpromise::error(err.str());
  }

  // TODO(fxbug.dev/87651): validate channel and fps limits once those are defined
  // For now just validate they are not zero.
  if (fidl.channel_count == 0) {
    err << "bad channel_count '" << fidl.channel_count << "'";
    return fpromise::error(err.str());
  }
  if (fidl.frames_per_second == 0) {
    err << "bad frames_per_second '" << fidl.frames_per_second << "'";
    return fpromise::error(err.str());
  }

  return fpromise::ok(Format(fidl.sample_format, fidl.channel_count, fidl.frames_per_second));
}

Format Format::CreateOrDie(fuchsia_mediastreams::wire::AudioFormat fidl) {
  auto result = Create(fidl);
  if (!result.is_ok()) {
    FX_CHECK(false) << "Format::CreateOrDie failed: " << result.error();
  }
  return result.take_value();
}

Format::Format(fuchsia_mediastreams::wire::AudioSampleFormat sample_format, int64_t channels,
               int64_t frames_per_second)
    : sample_format_(sample_format), channels_(channels), frames_per_second_(frames_per_second) {
  // Caller has validated that the parameters are valid, so just precompute the remaining fields.
  int32_t bytes_per_sample;
  switch (sample_format_) {
    case AudioSampleFormat::kUnsigned8:
      bytes_per_sample = 1;
      valid_bits_per_sample_ = 8;
      break;
    case AudioSampleFormat::kSigned16:
      bytes_per_sample = 2;
      valid_bits_per_sample_ = 16;
      break;
    case AudioSampleFormat::kSigned24In32:
      bytes_per_sample = 4;
      valid_bits_per_sample_ = 24;
      break;
    case AudioSampleFormat::kSigned32:
    case AudioSampleFormat::kFloat:
      bytes_per_sample = 4;
      valid_bits_per_sample_ = 32;
      break;
    default:
      FX_CHECK(false) << "unexpected sample format " << fidl::ToUnderlying(sample_format_);
  }

  bytes_per_frame_ = bytes_per_sample * channels_;
  frames_per_ns_ = TimelineRate(frames_per_second_, zx::sec(1).to_nsecs());

  auto frac_frames_per_frame = TimelineRate(Fixed(1).raw_value(), 1);
  frac_frames_per_ns_ = TimelineRate::Product(frames_per_ns_, frac_frames_per_frame);
}

bool Format::operator==(const Format& rhs) const {
  // All other fields are derived from these types.
  return sample_format_ == rhs.sample_format_ && channels_ == rhs.channels_ &&
         frames_per_second_ == rhs.frames_per_second_;
}

fuchsia_mediastreams::wire::AudioFormat Format::ToFidl() const {
  return fuchsia_mediastreams::wire::AudioFormat{
      .sample_format = sample_format_,
      .channel_count = static_cast<uint32_t>(channels_),
      .frames_per_second = static_cast<uint32_t>(frames_per_second_),
      .channel_layout = fuchsia_mediastreams::wire::AudioChannelLayout::WithPlaceholder(0),
  };
}

int64_t Format::integer_frames_per(zx::duration duration, TimelineRate::RoundingMode mode) const {
  return frames_per_ns_.Scale(duration.to_nsecs(), mode);
}

Fixed Format::frac_frames_per(zx::duration duration, TimelineRate::RoundingMode mode) const {
  return Fixed::FromRaw(frac_frames_per_ns_.Scale(duration.to_nsecs(), mode));
}

int64_t Format::bytes_per(zx::duration duration, TimelineRate::RoundingMode mode) const {
  return bytes_per_frame_ * integer_frames_per(duration, mode);
}

std::ostream& operator<<(std::ostream& out, const Format& format) {
  out << format.frames_per_second() << "hz-" << format.channels() << "ch-";

  switch (format.sample_format()) {
    case AudioSampleFormat::kUnsigned8:
      out << "u8";
      break;
    case AudioSampleFormat::kSigned16:
      out << "i16";
      break;
    case AudioSampleFormat::kSigned24In32:
      out << "i24in32";
      break;
    case AudioSampleFormat::kSigned32:
      out << "i24";
      break;
    case AudioSampleFormat::kFloat:
      out << "f32";
      break;
    default:
      FX_CHECK(false) << static_cast<int>(format.sample_format());
  }

  return out;
}

}  // namespace media_audio

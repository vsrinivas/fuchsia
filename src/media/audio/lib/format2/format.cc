// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/format2/format.h"

#include <lib/syslog/cpp/macros.h>

#include <sstream>

#include <sdk/lib/fidl/cpp/enum.h>

#include "src/media/audio/lib/format2/fixed.h"

namespace media_audio {

using SampleType = fuchsia_audio::SampleType;
using TimelineRate = media::TimelineRate;

fpromise::result<Format, std::string> Format::Create(fuchsia_audio::wire::Format fidl) {
  std::ostringstream err;

  if (!fidl.has_sample_type()) {
    err << "missing required field (sample_type)";
    return fpromise::error(err.str());
  }
  if (!fidl.has_channel_count()) {
    err << "missing required field (channel_count)";
    return fpromise::error(err.str());
  }
  if (!fidl.has_frames_per_second()) {
    err << "missing required field (frames_per_second)";
    return fpromise::error(err.str());
  }

  switch (fidl.sample_type()) {
    case SampleType::kUint8:
    case SampleType::kInt16:
    case SampleType::kInt32:
    case SampleType::kFloat32:
    case SampleType::kFloat64:
      break;
    default:
      err << "bad sample_type '" << fidl.sample_type() << "'";
      return fpromise::error(err.str());
  }

  // TODO(fxbug.dev/87651): validate channel and fps limits once those are defined
  // For now just validate they are not zero.
  if (fidl.channel_count() == 0) {
    err << "bad channel_count '" << fidl.channel_count() << "'";
    return fpromise::error(err.str());
  }
  if (fidl.frames_per_second() == 0) {
    err << "bad frames_per_second '" << fidl.frames_per_second() << "'";
    return fpromise::error(err.str());
  }

  return fpromise::ok(Format(fidl.sample_type(), fidl.channel_count(), fidl.frames_per_second()));
}

Format Format::CreateOrDie(fuchsia_audio::wire::Format fidl) {
  auto result = Create(fidl);
  if (!result.is_ok()) {
    FX_CHECK(false) << "Format::CreateOrDie failed: " << result.error();
  }
  return result.take_value();
}

Format Format::CreateOrDie(Args args) {
  fidl::Arena<> arena;
  return CreateOrDie(fuchsia_audio::wire::Format::Builder(arena)
                         .sample_type(args.sample_type)
                         .channel_count(static_cast<uint32_t>(args.channels))
                         .frames_per_second(static_cast<uint32_t>(args.frames_per_second))
                         .Build());
}

fpromise::result<Format, std::string> Format::CreateLegacy(
    fuchsia_mediastreams::wire::AudioFormat fidl) {
  std::ostringstream err;

  SampleType sample_type;
  switch (fidl.sample_format) {
    case fuchsia_mediastreams::wire::AudioSampleFormat::kUnsigned8:
      sample_type = SampleType::kUint8;
      break;
    case fuchsia_mediastreams::wire::AudioSampleFormat::kSigned16:
      sample_type = SampleType::kInt16;
      break;
    case fuchsia_mediastreams::wire::AudioSampleFormat::kSigned24In32:
      sample_type = SampleType::kInt32;
      break;
    case fuchsia_mediastreams::wire::AudioSampleFormat::kFloat:
      sample_type = SampleType::kFloat32;
      break;
    default:
      err << "bad sample_format '" << fidl::ToUnderlying(fidl.sample_format) << "'";
      return fpromise::error(err.str());
  }

  fidl::Arena<> arena;
  return Create(fuchsia_audio::wire::Format::Builder(arena)
                    .sample_type(sample_type)
                    .channel_count(fidl.channel_count)
                    .frames_per_second(fidl.frames_per_second)
                    .Build());
}

Format Format::CreateLegacyOrDie(fuchsia_mediastreams::wire::AudioFormat fidl) {
  auto result = CreateLegacy(fidl);
  if (!result.is_ok()) {
    FX_CHECK(false) << "Format::CreateLegacyOrDie failed: " << result.error();
  }
  return result.take_value();
}

Format::Format(fuchsia_audio::SampleType sample_type, int64_t channels, int64_t frames_per_second)
    : sample_type_(sample_type), channels_(channels), frames_per_second_(frames_per_second) {
  // Caller has validated that the parameters are valid, so just precompute the remaining fields.
  int32_t bytes_per_sample;
  switch (sample_type_) {
    case SampleType::kUint8:
      bytes_per_sample = 1;
      valid_bits_per_sample_ = 8;
      break;
    case SampleType::kInt16:
      bytes_per_sample = 2;
      valid_bits_per_sample_ = 16;
      break;
    case SampleType::kInt32:
    case SampleType::kFloat32:
      bytes_per_sample = 4;
      valid_bits_per_sample_ = 32;
      break;
    case SampleType::kFloat64:
      bytes_per_sample = 8;
      valid_bits_per_sample_ = 64;
      break;
    default:
      FX_LOGS(FATAL) << "unexpected sample format " << sample_type_;
      __builtin_unreachable();
  }

  bytes_per_frame_ = bytes_per_sample * channels_;
  frames_per_ns_ = TimelineRate(frames_per_second_, zx::sec(1).to_nsecs());

  auto frac_frames_per_frame = TimelineRate(Fixed(1).raw_value(), 1);
  frac_frames_per_ns_ = TimelineRate::Product(frames_per_ns_, frac_frames_per_frame);
}

bool Format::operator==(const Format& rhs) const {
  // All other fields are derived from these types.
  return sample_type_ == rhs.sample_type_ && channels_ == rhs.channels_ &&
         frames_per_second_ == rhs.frames_per_second_;
}

fuchsia_audio::wire::Format Format::ToFidl(fidl::AnyArena& arena) const {
  return fuchsia_audio::wire::Format::Builder(arena)
      .sample_type(sample_type_)
      .channel_count(static_cast<uint32_t>(channels_))
      .frames_per_second(static_cast<uint32_t>(frames_per_second_))
      .Build();
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

  switch (format.sample_type()) {
    case SampleType::kUint8:
      out << "u8";
      break;
    case SampleType::kInt16:
      out << "i16";
      break;
    case SampleType::kInt32:
      out << "i24";
      break;
    case SampleType::kFloat32:
      out << "f32";
      break;
    case SampleType::kFloat64:
      out << "f64";
      break;
    default:
      FX_CHECK(false) << static_cast<int>(format.sample_type());
  }

  return out;
}

}  // namespace media_audio

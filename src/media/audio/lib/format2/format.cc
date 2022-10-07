// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/format2/format.h"

#include <fidl/fuchsia.audio/cpp/natural_ostream.h>
#include <fidl/fuchsia.audio/cpp/natural_types.h>
#include <fidl/fuchsia.audio/cpp/wire_types.h>
#include <fidl/fuchsia.mediastreams/cpp/wire_types.h>
#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>

#include <ostream>
#include <string>

#include <sdk/lib/fidl/cpp/enum.h>

#include "src/media/audio/lib/format2/fixed.h"

namespace media_audio {

using SampleType = fuchsia_audio::SampleType;
using TimelineRate = media::TimelineRate;

fpromise::result<Format, std::string> Format::Create(fuchsia_audio::wire::Format msg) {
  if (!msg.has_sample_type()) {
    return fpromise::error("missing required field (sample_type)");
  }
  if (!msg.has_channel_count()) {
    return fpromise::error("missing required field (channel_count)");
  }
  if (!msg.has_frames_per_second()) {
    return fpromise::error("missing required field (frames_per_second)");
  }

  return Create(Args{
      .sample_type = msg.sample_type(),
      .channels = msg.channel_count(),
      .frames_per_second = msg.frames_per_second(),
  });
}

fpromise::result<Format, std::string> Format::Create(fuchsia_audio::Format msg) {
  if (!msg.sample_type().has_value()) {
    return fpromise::error("missing required field (sample_type)");
  }
  if (!msg.channel_count().has_value()) {
    return fpromise::error("missing required field (channel_count)");
  }
  if (!msg.frames_per_second().has_value()) {
    return fpromise::error("missing required field (frames_per_second)");
  }

  return Create(Args{
      .sample_type = *msg.sample_type(),
      .channels = *msg.channel_count(),
      .frames_per_second = *msg.frames_per_second(),
  });
}

fpromise::result<Format, std::string> Format::Create(Args args) {
  switch (args.sample_type) {
    case SampleType::kUint8:
    case SampleType::kInt16:
    case SampleType::kInt32:
    case SampleType::kFloat32:
    case SampleType::kFloat64:
      break;
    default:
      return fpromise::error("bad sample_type '" +
                             std::to_string(fidl::ToUnderlying(args.sample_type)) + "'");
  }

  // TODO(fxbug.dev/87651): validate channel and fps limits once those are defined
  // For now just validate they are not zero.
  if (args.channels == 0) {
    return fpromise::error("bad channel_count '" + std::to_string(args.channels) + "'");
  }
  if (args.frames_per_second == 0) {
    return fpromise::error("bad frames_per_second '" + std::to_string(args.frames_per_second) +
                           "'");
  }

  return fpromise::ok(Format(args.sample_type, args.channels, args.frames_per_second));
}

Format Format::CreateOrDie(fuchsia_audio::wire::Format msg) {
  auto result = Create(msg);
  if (!result.is_ok()) {
    FX_CHECK(false) << "Format::CreateOrDie failed: " << result.error();
  }
  return result.take_value();
}

Format Format::CreateOrDie(fuchsia_audio::Format msg) {
  auto result = Create(msg);
  if (!result.is_ok()) {
    FX_CHECK(false) << "Format::CreateOrDie failed: " << result.error();
  }
  return result.take_value();
}

Format Format::CreateOrDie(Args args) {
  auto result = Create(args);
  if (!result.is_ok()) {
    FX_CHECK(false) << "Format::CreateOrDie failed: " << result.error();
  }
  return result.take_value();
}

fpromise::result<Format, std::string> Format::CreateLegacy(
    fuchsia_mediastreams::wire::AudioFormat msg) {
  SampleType sample_type;
  switch (msg.sample_format) {
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
      return fpromise::error("bad sample_format '" +
                             std::to_string(fidl::ToUnderlying(msg.sample_format)) + "'");
  }

  fidl::Arena<> arena;
  return Create(fuchsia_audio::wire::Format::Builder(arena)
                    .sample_type(sample_type)
                    .channel_count(msg.channel_count)
                    .frames_per_second(msg.frames_per_second)
                    .Build());
}

Format Format::CreateLegacyOrDie(fuchsia_mediastreams::wire::AudioFormat msg) {
  auto result = CreateLegacy(msg);
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
  frac_frames_per_ns_ = TimelineRate(Fixed(frames_per_second_).raw_value(), zx::sec(1).to_nsecs());
}

bool Format::operator==(const Format& rhs) const {
  // All other fields are derived from these types.
  return sample_type_ == rhs.sample_type_ && channels_ == rhs.channels_ &&
         frames_per_second_ == rhs.frames_per_second_;
}

fuchsia_audio::wire::Format Format::ToWireFidl(fidl::AnyArena& arena) const {
  return fuchsia_audio::wire::Format::Builder(arena)
      .sample_type(sample_type_)
      .channel_count(static_cast<uint32_t>(channels_))
      .frames_per_second(static_cast<uint32_t>(frames_per_second_))
      .Build();
}

fuchsia_audio::Format Format::ToNaturalFidl() const {
  fuchsia_audio::Format msg;
  msg.sample_type() = sample_type_;
  msg.channel_count() = static_cast<uint32_t>(channels_);
  msg.frames_per_second() = static_cast<uint32_t>(frames_per_second_);
  return msg;
}

fuchsia_mediastreams::wire::AudioFormat Format::ToLegacyFidl() const {
  auto sample_format = fuchsia_mediastreams::AudioSampleFormat::kFloat;
  switch (sample_type_) {
    case SampleType::kUint8:
      sample_format = fuchsia_mediastreams::wire::AudioSampleFormat::kUnsigned8;
      break;
    case SampleType::kInt16:
      sample_format = fuchsia_mediastreams::wire::AudioSampleFormat::kSigned16;
      break;
    case SampleType::kInt32:
      sample_format = fuchsia_mediastreams::wire::AudioSampleFormat::kSigned24In32;
      break;
    case SampleType::kFloat32:
      sample_format = fuchsia_mediastreams::wire::AudioSampleFormat::kFloat;
      break;
    default:
      FX_LOGS(FATAL) << "unexpected sample format " << sample_type_;
      break;
  }
  return {
      .sample_format = sample_format,
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

zx::duration Format::duration_per(Fixed frames, media::TimelineRate::RoundingMode mode) const {
  return zx::duration(frac_frames_per_ns_.Inverse().Scale(frames.raw_value(), mode));
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
      FX_CHECK(false) << format.sample_type();
  }

  return out;
}

}  // namespace media_audio

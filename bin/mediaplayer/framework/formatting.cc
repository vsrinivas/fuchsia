// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/framework/formatting.h"

#include <iomanip>
#include <iostream>

#include <fuchsia/media/cpp/fidl.h>

#include "garnet/bin/mediaplayer/framework/stages/stage_impl.h"

namespace media_player {

// Prints an ns value in 0.123,456,789 format.
std::ostream& operator<<(std::ostream& os, AsNs value) {
  if (value.value_ == fuchsia::media::NO_TIMESTAMP) {
    return os << "<no timestamp>";
  }

  if (value.value_ == 0) {
    return os << "0";
  }

  int64_t s = std::abs(value.value_);
  int64_t ns = s % 1000;
  s /= 1000;
  int64_t us = s % 1000;
  s /= 1000;
  int64_t ms = s % 1000;
  s /= 1000;

  if (value.value_ < 0) {
    os << "-";
  }

  return os << s << "." << std::setw(3) << std::setfill('0') << ms << ","
            << std::setw(3) << std::setfill('0') << us << "," << std::setw(3)
            << std::setfill('0') << ns;
}

std::ostream& operator<<(std::ostream& os, Result value) {
  switch (value) {
    case Result::kOk:
      return os << "ok";
    case Result::kUnknownError:
      return os << "unknown error";
    case Result::kInternalError:
      return os << "internal Error";
    case Result::kUnsupportedOperation:
      return os << "unsupported operation";
    case Result::kInvalidArgument:
      return os << "invalid argument";
    case Result::kNotFound:
      return os << "not found";
    case Result::kPeerClosed:
      return os << "peer closed";
    case Result::kCancelled:
      return os << "cancelled";
  }

  return os;
}

std::ostream& operator<<(std::ostream& os, const PacketPtr& value) {
  if (!value) {
    return os << "<null>";
  }

  os << AsNs(value->GetPts(media::TimelineRate::NsPerSecond)) << " ("
     << value->pts() << "@" << value->pts_rate() << ")"
     << " " << value->size() << " bytes";

  if (value->keyframe()) {
    os << " keyframe";
  }

  if (value->end_of_stream()) {
    os << " eos";
  }

  return os;
}

std::ostream& operator<<(std::ostream& os, const StreamType& value) {
  os << fostr::Indent;
  os << fostr::NewLine << "medium:               " << value.medium();
  os << fostr::NewLine << "encoding:             " << value.encoding();
  os << fostr::NewLine
     << "encoding parameters:  " << value.encoding_parameters();

  switch (value.medium()) {
    case StreamType::Medium::kAudio:
      os << fostr::NewLine
         << "sample format:        " << value.audio()->sample_format();
      os << fostr::NewLine
         << "channels:             " << value.audio()->channels();
      os << fostr::NewLine
         << "frames per second:    " << value.audio()->frames_per_second();
      break;
    case StreamType::Medium::kVideo:
      os << fostr::NewLine
         << "profile:              " << value.video()->profile();
      os << fostr::NewLine
         << "pixel format:         " << value.video()->pixel_format();
      os << fostr::NewLine
         << "color space:          " << value.video()->color_space();
      os << fostr::NewLine << "size:                 " << value.video()->width()
         << "x" << value.video()->height();
      os << fostr::NewLine
         << "coded size:           " << value.video()->coded_width() << "x"
         << value.video()->coded_height();
      os << fostr::NewLine << "pixel aspect ratio:   "
         << value.video()->pixel_aspect_ratio_width() << "x"
         << value.video()->pixel_aspect_ratio_height();
      os << fostr::NewLine << "line stride:          "
         << AsInlineVector<uint32_t>(value.video()->line_stride());
      os << fostr::NewLine << "plane offsets:        "
         << AsInlineVector<uint32_t>(value.video()->plane_offset());
      break;
    default:
      break;
  }

  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const StreamTypeSet& value) {
  os << fostr::Indent;
  os << fostr::NewLine << "medium:            " << value.medium();
  os << fostr::NewLine << "encodings:         " << value.encodings();
  switch (value.medium()) {
    case StreamType::Medium::kAudio:
      os << fostr::NewLine
         << "sample format:     " << value.audio()->sample_format();
      os << fostr::NewLine
         << "channels:          " << value.audio()->channels();
      os << fostr::NewLine
         << "frames per second: " << value.audio()->frames_per_second();
      break;
    case StreamType::Medium::kVideo:
      os << fostr::NewLine << "width:             " << value.video()->width();
      os << fostr::NewLine << "height:            " << value.video()->height();
      break;
    default:
      break;
  }

  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, StreamType::Medium value) {
  switch (value) {
    case StreamType::Medium::kAudio:
      return os << "audio";
    case StreamType::Medium::kVideo:
      return os << "video";
    case StreamType::Medium::kText:
      return os << "text";
    case StreamType::Medium::kSubpicture:
      return os << "subpicture";
  }

  return os;
}

std::ostream& operator<<(std::ostream& os,
                         AudioStreamType::SampleFormat value) {
  switch (value) {
    case AudioStreamType::SampleFormat::kNone:
      return os << "none";
    case AudioStreamType::SampleFormat::kAny:
      return os << "any";
    case AudioStreamType::SampleFormat::kUnsigned8:
      return os << "unsigned 8";
    case AudioStreamType::SampleFormat::kSigned16:
      return os << "signed 16";
    case AudioStreamType::SampleFormat::kSigned24In32:
      return os << "signed 24 in 32";
    case AudioStreamType::SampleFormat::kFloat:
      return os << "float";
  }

  return os;
}

std::ostream& operator<<(std::ostream& os,
                         VideoStreamType::VideoProfile value) {
  switch (value) {
    case VideoStreamType::VideoProfile::kUnknown:
      return os << "unknown";
    case VideoStreamType::VideoProfile::kNotApplicable:
      return os << "not applicable";
    case VideoStreamType::VideoProfile::kH264Baseline:
      return os << "h264 baseline";
    case VideoStreamType::VideoProfile::kH264Main:
      return os << "h264 main";
    case VideoStreamType::VideoProfile::kH264Extended:
      return os << "h264 extended";
    case VideoStreamType::VideoProfile::kH264High:
      return os << "h264 high";
    case VideoStreamType::VideoProfile::kH264High10:
      return os << "h264 high 10";
    case VideoStreamType::VideoProfile::kH264High422:
      return os << "h264 high 422";
    case VideoStreamType::VideoProfile::kH264High444Predictive:
      return os << "h264 high 444 predictive";
    case VideoStreamType::VideoProfile::kH264ScalableBaseline:
      return os << "h264 scalable baseline";
    case VideoStreamType::VideoProfile::kH264ScalableHigh:
      return os << "h264 scalable high";
    case VideoStreamType::VideoProfile::kH264StereoHigh:
      return os << "h264 stereo high";
    case VideoStreamType::VideoProfile::kH264MultiviewHigh:
      return os << "h264 multiview high";
  }

  return os;
}

std::ostream& operator<<(std::ostream& os, VideoStreamType::PixelFormat value) {
  switch (value) {
    case VideoStreamType::PixelFormat::kUnknown:
      return os << "unknown";
    case VideoStreamType::PixelFormat::kI420:
      return os << "i420";
    case VideoStreamType::PixelFormat::kYv12:
      return os << "yv12";
    case VideoStreamType::PixelFormat::kYv16:
      return os << "yv16";
    case VideoStreamType::PixelFormat::kYv12A:
      return os << "yv12a";
    case VideoStreamType::PixelFormat::kYv24:
      return os << "yv24";
    case VideoStreamType::PixelFormat::kNv12:
      return os << "nv12";
    case VideoStreamType::PixelFormat::kNv21:
      return os << "nv21";
    case VideoStreamType::PixelFormat::kUyvy:
      return os << "uyvy";
    case VideoStreamType::PixelFormat::kYuy2:
      return os << "yuy2";
    case VideoStreamType::PixelFormat::kArgb:
      return os << "argb";
    case VideoStreamType::PixelFormat::kXrgb:
      return os << "xrgb";
    case VideoStreamType::PixelFormat::kRgb24:
      return os << "rgb24";
    case VideoStreamType::PixelFormat::kRgb32:
      return os << "rgb32";
    case VideoStreamType::PixelFormat::kMjpeg:
      return os << "mjpeg";
    case VideoStreamType::PixelFormat::kMt21:
      return os << "mt21";
  }

  return os;
}

std::ostream& operator<<(std::ostream& os, VideoStreamType::ColorSpace value) {
  switch (value) {
    case VideoStreamType::ColorSpace::kUnknown:
      return os << "unknown";
    case VideoStreamType::ColorSpace::kNotApplicable:
      return os << "not applicable";
    case VideoStreamType::ColorSpace::kJpeg:
      return os << "jpeg";
    case VideoStreamType::ColorSpace::kHdRec709:
      return os << "hd rec 709";
    case VideoStreamType::ColorSpace::kSdRec601:
      return os << "sd rec 601";
  }

  return os;
}

std::ostream& operator<<(std::ostream& os, const Bytes& value) {
  return os << value.size() << " bytes";
}

std::ostream& operator<<(std::ostream& os, media::TimelineRate value) {
  return os << value.subject_delta() << "/" << value.reference_delta();
}

std::ostream& operator<<(std::ostream& os, media::TimelineFunction value) {
  return os << AsNs(value.subject_time())
            << "::" << AsNs(value.reference_time()) << "@" << value.rate();
}

std::ostream& operator<<(std::ostream& os, const GenericNode& value) {
  return os << value.label();
}

std::ostream& operator<<(std::ostream& os, const StageImpl& value) {
  FXL_DCHECK(value.GetGenericNode());

  return os << *value.GetGenericNode();
}

std::ostream& operator<<(std::ostream& os, const Input& value) {
  FXL_DCHECK(value.stage());

  return os << *value.stage() << ".input#" << value.index();
}

std::ostream& operator<<(std::ostream& os, const Output& value) {
  FXL_DCHECK(value.stage());

  return os << *value.stage() << ".output#" << value.index();
}

}  // namespace media_player

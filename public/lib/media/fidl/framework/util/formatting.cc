// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "services/media/framework/util/formatting.h"

namespace mojo {
namespace media {

int ostream_indent_index() {
  static int i = std::ios_base::xalloc();
  return i;
}

std::ostream& begl(std::ostream& os) {
  for (long i = 0; i < os.iword(ostream_indent_index()); i++) {
    os << "    ";
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, Result value) {
  switch (value) {
    case Result::kOk:
      return os << "kOk";
    case Result::kUnknownError:
      return os << "kUnknownError";
    case Result::kInternalError:
      return os << "kInternalError";
    case Result::kUnsupportedOperation:
      return os << "kUnsupportedOperation";
    case Result::kInvalidArgument:
      return os << "kInvalidArgument";
    case Result::kNotFound:
      return os << "kNotFound";
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, Demand value) {
  switch (value) {
    case Demand::kNegative:
      return os << "kNegative";
    case Demand::kNeutral:
      return os << "kNeutral";
    case Demand::kPositive:
      return os << "kPositive";
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const PacketPtr& value) {
  if (!value) {
    return os << "<nullptr>";
  }

  os << "&" << std::hex << uint64_t(value.get()) << std::dec;
  os << "/pts:" << value->pts();
  os << "/eos:" << (value->end_of_stream() ? "t" : "f");
  os << "/size:" << value->size();
  os << "/payload:" << std::hex << uint64_t(value->payload()) << std::dec;
  return os;
}

std::ostream& operator<<(std::ostream& os,
                         const std::unique_ptr<StreamType>& value) {
  if (!value) {
    return os << "<nullptr>" << std::endl;
  } else {
    os << std::endl;
  }

  os << indent;
  os << begl << "Medium medium(): " << value->medium() << std::endl;
  os << begl << "std::string encoding: " << value->encoding() << std::endl;
  os << begl << "Bytes encoding_parameters: " << value->encoding_parameters()
     << std::endl;
  switch (value->medium()) {
    case StreamType::Medium::kAudio:
      os << begl
         << "SampleFormat sample_format: " << value->audio()->sample_format()
         << std::endl;
      os << begl << "uint32_t channels: " << value->audio()->channels()
         << std::endl;
      os << begl << "uint32_t frames_per_second: "
         << value->audio()->frames_per_second() << std::endl;
      break;
    case StreamType::Medium::kVideo:
      os << begl << "VideoProfile profile: " << value->video()->profile()
         << std::endl;
      os << begl
         << "PixelFormat pixel_format: " << value->video()->pixel_format()
         << std::endl;
      os << begl << "ColorSpace color_space: " << value->video()->color_space()
         << std::endl;
      os << begl << "uint32_t width: " << value->video()->width() << std::endl;
      os << begl << "uint32_t height: " << value->video()->height()
         << std::endl;
      os << begl << "uint32_t coded_width: " << value->video()->coded_width()
         << std::endl;
      os << begl << "uint32_t coded_height: " << value->video()->coded_height()
         << std::endl;
      break;
    default:
      break;
  }

  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const std::unique_ptr<StreamTypeSet>& value) {
  if (!value) {
    return os << "<nullptr>" << std::endl;
  } else {
    os << std::endl;
  }

  os << indent;
  os << begl << "Medium medium(): " << value->medium() << std::endl;
  os << begl << "std::vector<std::string>&: " << value->encodings()
     << std::endl;
  switch (value->medium()) {
    case StreamType::Medium::kAudio:
      os << begl
         << "SampleFormat sample_format: " << value->audio()->sample_format()
         << std::endl;
      os << begl << "Range<uint32_t> channels: " << value->audio()->channels()
         << std::endl;
      os << begl << "Range<uint32_t> frames_per_second: "
         << value->audio()->frames_per_second() << std::endl;
      break;
    case StreamType::Medium::kVideo:
      os << begl << "Range<uint32_t> width: " << value->video()->width()
         << std::endl;
      os << begl << "Range<uint32_t> height: " << value->video()->height()
         << std::endl;
      break;
    default:
      break;
  }

  return os << outdent;
}

std::ostream& operator<<(
    std::ostream& os,
    const std::unique_ptr<std::vector<std::unique_ptr<StreamType>>>& value) {
  if (!value) {
    return os << "<nullptr>" << std::endl;
  } else if (value->size() == 0) {
    return os << "<empty>" << std::endl;
  } else {
    os << std::endl;
  }

  int index = 0;
  for (const std::unique_ptr<StreamType>& element : *value) {
    os << "[" << index++ << "]: " << element;
  }

  return os;
}

std::ostream& operator<<(
    std::ostream& os,
    const std::unique_ptr<std::vector<std::unique_ptr<StreamTypeSet>>>& value) {
  if (!value) {
    return os << "<nullptr>" << std::endl;
  } else if (value->size() == 0) {
    return os << "<empty>" << std::endl;
  } else {
    os << std::endl;
  }

  int index = 0;
  for (const std::unique_ptr<StreamTypeSet>& element : *value) {
    os << "[" << index++ << "]: " << element;
  }

  return os;
}

std::ostream& operator<<(std::ostream& os, StreamType::Medium value) {
  switch (value) {
    case StreamType::Medium::kAudio:
      return os << "kAudio";
    case StreamType::Medium::kVideo:
      return os << "kVideo";
    case StreamType::Medium::kText:
      return os << "kText";
    case StreamType::Medium::kSubpicture:
      return os << "kSubpicture";
  }
  return os;
}

std::ostream& operator<<(std::ostream& os,
                         AudioStreamType::SampleFormat value) {
  switch (value) {
    case AudioStreamType::SampleFormat::kAny:
      return os << "kAny";
    case AudioStreamType::SampleFormat::kUnsigned8:
      return os << "kUnsigned8";
    case AudioStreamType::SampleFormat::kSigned16:
      return os << "kSigned16";
    case AudioStreamType::SampleFormat::kSigned24In32:
      return os << "kSigned24In32";
    case AudioStreamType::SampleFormat::kFloat:
      return os << "kFloat";
  }
  return os;
}

std::ostream& operator<<(std::ostream& os,
                         VideoStreamType::VideoProfile value) {
  switch (value) {
    case VideoStreamType::VideoProfile::kUnknown:
      return os << "kUnknown";
    case VideoStreamType::VideoProfile::kNotApplicable:
      return os << "kNotApplicable";
    case VideoStreamType::VideoProfile::kH264Baseline:
      return os << "kH264Baseline";
    case VideoStreamType::VideoProfile::kH264Main:
      return os << "kH264Main";
    case VideoStreamType::VideoProfile::kH264Extended:
      return os << "kH264Extended";
    case VideoStreamType::VideoProfile::kH264High:
      return os << "kH264High";
    case VideoStreamType::VideoProfile::kH264High10:
      return os << "kH264High10";
    case VideoStreamType::VideoProfile::kH264High422:
      return os << "kH264High422";
    case VideoStreamType::VideoProfile::kH264High444Predictive:
      return os << "kH264High444Predictive";
    case VideoStreamType::VideoProfile::kH264ScalableBaseline:
      return os << "kH264ScalableBaseline";
    case VideoStreamType::VideoProfile::kH264ScalableHigh:
      return os << "kH264ScalableHigh";
    case VideoStreamType::VideoProfile::kH264StereoHigh:
      return os << "kH264StereoHigh";
    case VideoStreamType::VideoProfile::kH264MultiviewHigh:
      return os << "kH264MultiviewHigh";
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, VideoStreamType::PixelFormat value) {
  switch (value) {
    case VideoStreamType::PixelFormat::kUnknown:
      return os << "kUnknown";
    case VideoStreamType::PixelFormat::kI420:
      return os << "kI420";
    case VideoStreamType::PixelFormat::kYv12:
      return os << "kYv12";
    case VideoStreamType::PixelFormat::kYv16:
      return os << "kYv16";
    case VideoStreamType::PixelFormat::kYv12A:
      return os << "kYv12A";
    case VideoStreamType::PixelFormat::kYv24:
      return os << "kYv24";
    case VideoStreamType::PixelFormat::kNv12:
      return os << "kNv12";
    case VideoStreamType::PixelFormat::kNv21:
      return os << "kNv21";
    case VideoStreamType::PixelFormat::kUyvy:
      return os << "kUyvy";
    case VideoStreamType::PixelFormat::kYuy2:
      return os << "kYuy2";
    case VideoStreamType::PixelFormat::kArgb:
      return os << "kArgb";
    case VideoStreamType::PixelFormat::kXrgb:
      return os << "kXrgb";
    case VideoStreamType::PixelFormat::kRgb24:
      return os << "kRgb24";
    case VideoStreamType::PixelFormat::kRgb32:
      return os << "kRgb24";
    case VideoStreamType::PixelFormat::kMjpeg:
      return os << "kRgb24";
    case VideoStreamType::PixelFormat::kMt21:
      return os << "kRgb24";
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, VideoStreamType::ColorSpace value) {
  switch (value) {
    case VideoStreamType::ColorSpace::kUnknown:
      return os << "kUnknown";
    case VideoStreamType::ColorSpace::kNotApplicable:
      return os << "kNotApplicable";
    case VideoStreamType::ColorSpace::kJpeg:
      return os << "kJpeg";
    case VideoStreamType::ColorSpace::kHdRec709:
      return os << "kHdRec709";
    case VideoStreamType::ColorSpace::kSdRec601:
      return os << "kSdRec601";
  }
  return os;
}

std::ostream& operator<<(std::ostream& os,
                         const std::unique_ptr<Bytes>& value) {
  if (value == nullptr) {
    return os << "<nullptr>";
  } else {
    return os << value->size() << " bytes";
  }
}

std::ostream& operator<<(std::ostream& os, Range<bool> value) {
  if (value.min) {
    return os << "true";
  } else if (value.max) {
    return os << "false..true";
  } else {
    return os << "false";
  }
}

}  // namespace media
}  // namespace mojo

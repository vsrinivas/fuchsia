// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/graph/formatting.h"

#include <iomanip>
#include <iostream>

#include "src/lib/fostr/zx_types.h"

namespace media_player {

// Prints an ns value in 0.123,456,789 format.
std::ostream& operator<<(std::ostream& os, AsNs value) {
  if (value.value_ == Packet::kNoPts) {
    return os << "<none>";
  }

  if (value.value_ == Packet::kMinPts) {
    return os << "<min>";
  }

  if (value.value_ == Packet::kMaxPts) {
    return os << "<max>";
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

  return os << s << "." << std::setw(3) << std::setfill('0') << ms << "," << std::setw(3)
            << std::setfill('0') << us << "," << std::setw(3) << std::setfill('0') << ns;
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

  os << AsNs(value->GetPts(media::TimelineRate::NsPerSecond)) << " (" << AsNs(value->pts()) << "@"
     << value->pts_rate() << ")"
     << " " << value->size() << " bytes";

  if (value->keyframe()) {
    os << " keyframe";
  }

  if (value->end_of_stream()) {
    os << " eos";
  }

  if (value->discontinuity()) {
    os << " disc";
  }

  return os;
}

std::ostream& operator<<(std::ostream& os, const StreamType& value) {
  os << fostr::Indent;
  os << fostr::NewLine << "medium:                " << value.medium();
  if (value.encrypted()) {
    os << fostr::NewLine << "encryption parameters: " << *value.encryption_parameters();
  } else {
    os << fostr::NewLine << "encryption parameters: <null>";
  }
  os << fostr::NewLine << "encoding:              " << value.encoding();
  if (value.encoding_parameters()) {
    os << fostr::NewLine << "encoding parameters:   " << *value.encoding_parameters();
  } else {
    os << fostr::NewLine << "encoding parameters:   <null>";
  }

  switch (value.medium()) {
    case StreamType::Medium::kAudio:
      os << fostr::NewLine << "sample format:         " << value.audio()->sample_format();
      os << fostr::NewLine << "channels:              " << value.audio()->channels();
      os << fostr::NewLine << "frames per second:     " << value.audio()->frames_per_second();
      break;
    case StreamType::Medium::kVideo:
      os << fostr::NewLine << "pixel format:          " << value.video()->pixel_format();
      os << fostr::NewLine << "color space:           " << value.video()->color_space();
      os << fostr::NewLine << "size:                  " << value.video()->width() << "x"
         << value.video()->height();
      os << fostr::NewLine << "coded size:            " << value.video()->coded_width() << "x"
         << value.video()->coded_height();
      os << fostr::NewLine << "pixel aspect ratio:    " << value.video()->pixel_aspect_ratio_width()
         << "x" << value.video()->pixel_aspect_ratio_height();
      os << fostr::NewLine << "line stride:           " << value.video()->line_stride();
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
      os << fostr::NewLine << "sample format:     " << value.audio()->sample_format();
      os << fostr::NewLine << "channels:          " << value.audio()->channels();
      os << fostr::NewLine << "frames per second: " << value.audio()->frames_per_second();
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

std::ostream& operator<<(std::ostream& os, AudioStreamType::SampleFormat value) {
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

std::ostream& operator<<(std::ostream& os, VideoStreamType::PixelFormat value) {
  switch (value) {
    case VideoStreamType::PixelFormat::kUnknown:
      return os << "unknown";
    case VideoStreamType::PixelFormat::kArgb:
      return os << "argb";
    case VideoStreamType::PixelFormat::kYuy2:
      return os << "yuy2";
    case VideoStreamType::PixelFormat::kYv12:
      return os << "yv12";
    case VideoStreamType::PixelFormat::kNv12:
      return os << "nv12";
    case VideoStreamType::PixelFormat::kI420:
      return os << "i420";
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
  return os << AsNs(value.subject_time()) << "::" << AsNs(value.reference_time()) << "@"
            << value.rate();
}

std::ostream& operator<<(std::ostream& os, const Node& value) { return os << value.label(); }

std::ostream& operator<<(std::ostream& os, const Input& value) {
  FX_DCHECK(value.node());

  return os << *value.node() << ".input#" << value.index();
}

std::ostream& operator<<(std::ostream& os, const Output& value) {
  FX_DCHECK(value.node());

  return os << *value.node() << ".output#" << value.index();
}

std::ostream& operator<<(std::ostream& os, PayloadMode value) {
  switch (value) {
    case PayloadMode::kNotConfigured:
      return os << "not configured";
    case PayloadMode::kUsesLocalMemory:
      return os << "uses local memory";
    case PayloadMode::kProvidesLocalMemory:
      return os << "provides local memory";
    case PayloadMode::kUsesVmos:
      return os << "uses vmos";
    case PayloadMode::kProvidesVmos:
      return os << "provides vmos";
    case PayloadMode::kUsesSysmemVmos:
      return os << "uses sysmem vmos";
  }

  return os;
}

std::ostream& operator<<(std::ostream& os, VmoAllocation value) {
  switch (value) {
    case VmoAllocation::kNotApplicable:
      return os << "not applicable";
    case VmoAllocation::kSingleVmo:
      return os << "single vmo";
    case VmoAllocation::kVmoPerBuffer:
      return os << "vmo per buffer";
    case VmoAllocation::kUnrestricted:
      return os << "unrestricted";
  }

  return os;
}

std::ostream& operator<<(std::ostream& os, const PayloadConfig& value) {
  os << fostr::Indent;
  os << fostr::NewLine << "mode:                       " << value.mode_;
  os << fostr::NewLine << "max aggregate payload_size: " << value.max_aggregate_payload_size_;
  os << fostr::NewLine << "max payload count:          " << value.max_payload_count_;
  os << fostr::NewLine << "max payload size:           " << value.max_payload_size_;
  os << fostr::NewLine << "vmo allocation:             " << value.vmo_allocation_;
  os << fostr::NewLine << "map flags:                  ";

  switch (value.map_flags_) {
    case 0:
      os << "<none>";
      break;
    case ZX_VM_PERM_READ:
      os << "ZX_VM_PERM_READ";
      break;
    case ZX_VM_PERM_WRITE:
      os << "ZX_VM_PERM_WRITE";
      break;
    case ZX_VM_PERM_READ | ZX_VM_PERM_WRITE:
      os << "ZX_VM_PERM_READ | ZX_VM_PERM_WRITE";
      break;
    default:
      os << "0x" << std::hex << value.map_flags_ << std::dec;
      break;
  }

  if (value.output_video_constraints_) {
    auto& constraints = *value.output_video_constraints_;
    os << fostr::NewLine << "output video constraints:";
    os << fostr::Indent;
    os << fostr::NewLine
       << "pixel format:          " << static_cast<uint32_t>(constraints.pixel_format.type);
    os << fostr::NewLine << "color space:           ";
    for (size_t i = 0; i < constraints.color_spaces_count; ++i) {
      os << static_cast<uint32_t>(constraints.color_space[i].type) << " ";
    }
    if (constraints.min_coded_width != 0) {
      os << fostr::NewLine << "min coded width:       " << constraints.min_coded_width;
    }
    if (constraints.max_coded_width != 0) {
      os << fostr::NewLine << "max coded width:       " << constraints.max_coded_width;
    }
    if (constraints.min_coded_height != 0) {
      os << fostr::NewLine << "min coded height:      " << constraints.min_coded_height;
    }
    if (constraints.max_coded_height != 0) {
      os << fostr::NewLine << "max coded height:      " << constraints.max_coded_height;
    }
    if (constraints.min_bytes_per_row != 0) {
      os << fostr::NewLine << "min bytes per row:     " << constraints.min_bytes_per_row;
    }
    if (constraints.max_bytes_per_row != 0) {
      os << fostr::NewLine << "max bytes per row:     " << constraints.max_bytes_per_row;
    }
    if (constraints.max_coded_width_times_coded_height != 0xFFFFFFFF) {
      os << fostr::NewLine
         << "max coded area:        " << constraints.max_coded_width_times_coded_height;
    }
    if (constraints.layers != 1) {
      os << fostr::NewLine << "layers:                " << constraints.layers;
    }
    if (constraints.coded_width_divisor != 1) {
      os << fostr::NewLine << "coded width div:       " << constraints.coded_width_divisor;
    }
    if (constraints.coded_height_divisor != 1) {
      os << fostr::NewLine << "coded height div:      " << constraints.coded_height_divisor;
    }
    if (constraints.bytes_per_row_divisor != 1) {
      os << fostr::NewLine << "bytes per row div:     " << constraints.bytes_per_row_divisor;
    }
    if (constraints.start_offset_divisor != 1) {
      os << fostr::NewLine << "start offset div:      " << constraints.start_offset_divisor;
    }
    if (constraints.display_width_divisor != 1) {
      os << fostr::NewLine << "display width div:     " << constraints.display_width_divisor;
    }
    if (constraints.display_height_divisor != 1) {
      os << fostr::NewLine << "display height div:    " << constraints.display_height_divisor;
    }
    if (constraints.required_min_coded_width != 0) {
      os << fostr::NewLine << "req min coded width:   " << constraints.required_min_coded_width;
    }
    if (constraints.required_max_coded_width != 0) {
      os << fostr::NewLine << "req max coded width:   " << constraints.required_max_coded_width;
    }
    if (constraints.required_min_coded_height != 0) {
      os << fostr::NewLine << "req min coded height:  " << constraints.required_min_coded_height;
    }
    if (constraints.required_max_coded_height != 0) {
      os << fostr::NewLine << "rea max coded height:  " << constraints.required_max_coded_height;
    }
    if (constraints.required_min_bytes_per_row != 0) {
      os << fostr::NewLine << "req min bytes per row: " << constraints.required_min_bytes_per_row;
    }
    if (constraints.required_max_bytes_per_row != 0) {
      os << fostr::NewLine << "req max bytes per row: " << constraints.required_max_bytes_per_row;
    }
    os << fostr::Outdent;
  }

  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const PayloadVmo& value) {
  return os << "size " << value.size() << ", start " << std::hex << value.start() << std::dec
            << ", vmo " << value.vmo();
}

}  // namespace media_player

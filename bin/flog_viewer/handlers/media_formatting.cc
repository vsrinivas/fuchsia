// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iomanip>

#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"

namespace flog {
namespace handlers {

template <typename T>
std::ostream& operator<<(std::ostream& os, const fidl::InterfacePtr<T>& value) {
  if (!value.is_bound()) {
    return os << "<not bound>";
  } else {
    return os << "<bound>";
  }
}

std::ostream& operator<<(std::ostream& os, const media::MediaTypePtr& value) {
  if (!value) {
    return os << "<nullptr>";
  }

  os << indent << "\n";
  os << begl << "medium: " << StringFromMediaTypeMedium(value->medium) << "\n";
  os << begl << "details: " << value->details << "\n";
  os << begl << "encoding: " << value->encoding << "\n";
  if (value->encoding_parameters) {
    os << begl << "encoding_parameters: " << value->encoding_parameters.size()
       << " bytes";
  } else {
    os << begl << "encoding_parameters: <nullptr>";
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const media::MediaTypeSetPtr& value) {
  if (!value) {
    return os << "<nullptr>";
  }

  os << indent << "\n";
  os << begl << "medium: " << StringFromMediaTypeMedium(value->medium) << "\n";
  os << begl << "details: " << value->details << "\n";
  os << begl << "encodings: " << AsInlineArray<fidl::String>(value->encodings);
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const media::MediaTypeDetailsPtr& value) {
  if (!value) {
    return os << "<nullptr>";
  } else if (value->has_unknown_tag()) {
    return os << "<empty>";
  }

  os << indent << "\n";
  if (value->is_audio()) {
    return os << begl << "audio: " << value->get_audio() << outdent;
  }
  if (value->is_video()) {
    return os << begl << "video: " << value->get_video() << outdent;
  }
  if (value->is_text()) {
    return os << begl << "text: " << value->get_text() << outdent;
  }
  if (value->is_subpicture()) {
    return os << begl << "subpicture: " << value->get_subpicture() << outdent;
  }
  return os << begl << "UNKNOWN TAG" << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const media::MediaTypeSetDetailsPtr& value) {
  if (!value) {
    return os << "<nullptr>";
  } else if (value->has_unknown_tag()) {
    return os << "<empty>";
  }

  os << indent << "\n";
  if (value->is_audio()) {
    return os << begl << "audio: " << value->get_audio() << outdent;
  }
  if (value->is_video()) {
    return os << begl << "video: " << value->get_video() << outdent;
  }
  if (value->is_text()) {
    return os << begl << "text: " << value->get_text() << outdent;
  }
  if (value->is_subpicture()) {
    return os << begl << "subpicture: " << value->get_subpicture() << outdent;
  }
  return os << begl << "UNKNOWN TAG" << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const media::AudioMediaTypeDetailsPtr& value) {
  if (!value) {
    return os << "<nullptr>";
  }

  os << indent << "\n";
  os << begl
     << "sample_format: " << StringFromAudioSampleFormat(value->sample_format)
     << "\n";
  os << begl << "channels: " << int(value->channels) << "\n";
  os << begl << "frames_per_second: " << value->frames_per_second;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const media::AudioMediaTypeSetDetailsPtr& value) {
  if (!value) {
    return os << "<nullptr>";
  }

  os << indent << "\n";
  os << begl
     << "sample_format: " << StringFromAudioSampleFormat(value->sample_format)
     << "\n";
  os << begl << "min_channels: " << int(value->min_channels) << "\n";
  os << begl << "max_channels: " << int(value->max_channels) << "\n";
  os << begl << "min_frames_per_second: " << value->min_frames_per_second
     << "\n";
  os << begl << "max_frames_per_second: " << value->max_frames_per_second;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const media::VideoMediaTypeDetailsPtr& value) {
  if (!value) {
    return os << "<nullptr>";
  }

  os << indent << "\n";
  os << begl << "profile: " << value->profile << "\n";
  os << begl << "pixel_format: " << value->pixel_format << "\n";
  os << begl << "color_space: " << value->color_space << "\n";
  os << begl << "width: " << value->width << "\n";
  os << begl << "height: " << value->height << "\n";
  os << begl << "coded_width: " << value->coded_width << "\n";
  os << begl << "coded_height: " << value->coded_height << "\n";
  os << begl << "pixel_aspect_ratio_width: " << value->pixel_aspect_ratio_width
     << "\n";
  os << begl
     << "pixel_aspect_ratio_height: " << value->pixel_aspect_ratio_height
     << "\n";
  os << begl << "line_stride: " << AsInlineArray<uint32_t>(value->line_stride)
     << "\n";
  os << begl
     << "plane_offset: " << AsInlineArray<uint32_t>(value->plane_offset);
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const media::VideoMediaTypeSetDetailsPtr& value) {
  if (!value) {
    return os << "<nullptr>";
  }

  os << indent;
  os << begl << "min_width: " << value->min_width << "\n";
  os << begl << "max_width: " << value->max_width << "\n";
  os << begl << "min_height: " << value->min_height << "\n";
  os << begl << "max_height: " << value->max_height;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const media::TextMediaTypeDetailsPtr& value) {
  if (!value) {
    return os << "<nullptr>";
  }

  os << indent << "\n";
  os << begl << "NO MEMBERS";
  // TODO(dalesat): Print members here when we define some.
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const media::TextMediaTypeSetDetailsPtr& value) {
  if (!value) {
    return os << "<nullptr>";
  }

  os << indent;
  os << begl << "NO MEMBERS";
  // TODO(dalesat): Print members here when we define some.
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const media::SubpictureMediaTypeDetailsPtr& value) {
  if (!value) {
    return os << "<nullptr>";
  }

  os << indent << "\n";
  os << begl << "NO MEMBERS";
  // TODO(dalesat): Print members here when we define some.
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const media::SubpictureMediaTypeSetDetailsPtr& value) {
  if (!value) {
    return os << "<nullptr>";
  }

  os << indent;
  os << begl << "NO MEMBERS";
  // TODO(dalesat): Print members here when we define some.
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const media::TimelineTransformPtr& value) {
  if (!value) {
    return os << "<nullptr>";
  }

  os << indent << "\n";
  os << begl << "reference_time: " << AsTime(value->reference_time) << "\n";
  os << begl << "subject_time: " << AsTime(value->subject_time) << "\n";
  os << begl << "reference_delta: " << value->reference_delta << "\n";
  os << begl << "subject_delta: " << value->subject_delta;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const media::MediaPacketPtr& value) {
  if (!value) {
    return os << "<nullptr>";
  }

  return os << *value;
}

std::ostream& operator<<(std::ostream& os, const media::MediaPacket& value) {
  os << "\n";
  os << indent;
  os << begl << "pts: " << AsTime(value.pts) << "\n";
  os << begl << "pts_rate_ticks: " << value.pts_rate_ticks << "\n";
  os << begl << "pts_rate_seconds: " << value.pts_rate_seconds << "\n";
  os << begl << "end_of_stream: " << value.end_of_stream << "\n";
  os << begl << "payload_buffer_id: " << value.payload_buffer_id << "\n";
  os << begl << "payload_offset: " << value.payload_offset << "\n";
  os << begl << "payload_size: " << value.payload_size;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const media::MediaPacketDemandPtr& value) {
  if (!value) {
    return os << "<nullptr>";
  }

  os << indent << "\n";
  os << begl << "min_packets_outstanding: " << value->min_packets_outstanding
     << "\n";
  os << begl << "min_pts: " << AsTime(value->min_pts);
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, AsTime value) {
  int64_t time = value.time_;

  if (time == media::kUnspecifiedTime) {
    return os << "unspecified";
  }

  if (time < 0) {
    time = -time;
    os << "-";
  }

  return os << time / 1000000000ll << "." << std::setw(9) << std::setfill('0')
            << time % 1000000000ll;
}

const char* StringFromMediaTypeMedium(media::MediaTypeMedium value) {
  switch (value) {
    case media::MediaTypeMedium::AUDIO:
      return "AUDIO";
    case media::MediaTypeMedium::VIDEO:
      return "VIDEO";
    case media::MediaTypeMedium::TEXT:
      return "TEXT";
    case media::MediaTypeMedium::SUBPICTURE:
      return "SUBPICTURE";
  }
  return "UNKNOWN MEDIUM";
}

const char* StringFromAudioSampleFormat(media::AudioSampleFormat value) {
  switch (value) {
    case media::AudioSampleFormat::NONE:
      return "NONE";
    case media::AudioSampleFormat::ANY:
      return "ANY";
    case media::AudioSampleFormat::UNSIGNED_8:
      return "UNSIGNED_8";
    case media::AudioSampleFormat::SIGNED_16:
      return "SIGNED_16";
    case media::AudioSampleFormat::SIGNED_24_IN_32:
      return "SIGNED_24_IN_32";
    case media::AudioSampleFormat::FLOAT:
      return "FLOAT";
  }
  return "UNKNOWN FORMAT";
}

}  // namespace handlers
}  // namespace flog

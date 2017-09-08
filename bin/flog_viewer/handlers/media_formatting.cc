// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iomanip>

#include "garent/bin/flog_viewer/handlers/media_formatting.h"

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
  os << begl << "size: " << value->width << "x" << value->height << "\n";
  os << begl << "coded_size: " << value->coded_width << "x"
     << value->coded_height << "\n";
  os << begl << "pixel_aspect_ratio: " << value->pixel_aspect_ratio_width << "x"
     << value->pixel_aspect_ratio_height << "\n";
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

  os << indent << "\n";
  os << begl << "min_size: " << value->min_width << "x" << value->min_height
     << "\n";
  os << begl << "max_size: " << value->max_width << "x" << value->max_height
     << "\n";
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
  os << begl << "reference_time: " << AsNsTime(value->reference_time) << "\n";
  os << begl << "subject_time: " << AsNsTime(value->subject_time) << "\n";
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
  if (value.pts_rate_seconds == 1 && value.pts_rate_ticks == 1000000000ll) {
    os << begl << "pts: " << AsNsTime(value.pts) << " (ns)\n";
  } else if (value.pts_rate_seconds == 1 && value.pts_rate_ticks == 1000000ll) {
    os << begl << "pts: " << AsUsTime(value.pts) << " (us)\n";
  } else if (value.pts_rate_seconds == 1 && value.pts_rate_ticks == 1000ll) {
    os << begl << "pts: " << AsMsTime(value.pts) << " (ms)\n";
  } else {
    int64_t pts_ns = value.pts * (media::TimelineRate::NsPerSecond /
                                  media::TimelineRate(value.pts_rate_ticks,
                                                      value.pts_rate_seconds));
    os << begl << "pts: " << value.pts << " (" << value.pts_rate_ticks << "/"
       << value.pts_rate_seconds << ")"
       << " " << AsNsTime(pts_ns) << " (ns)\n";
  }
  if (value.keyframe) {
    os << begl << "keyframe: true\n";
  }
  if (value.end_of_stream) {
    os << begl << "end_of_stream: true\n";
  }
  os << begl << "payload: " << value.payload_buffer_id << " offset "
     << value.payload_offset << " size " << value.payload_size;
  if (value.revised_media_type) {
    os << "\n" << begl << "revised_media_type: " << value.revised_media_type;
  }

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
  os << begl << "min_pts: " << AsNsTime(value->min_pts);
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, AsNsTime value) {
  int64_t time = value.time_;

  switch (time) {
    case media::kUnspecifiedTime:
      return os << "unspecified";
    case media::kMinTime:
      return os << "min";
    case media::kMaxTime:
      return os << "max";
  }

  if (time < 0) {
    time = -time;
    os << "-";
  }

  return os << time / 1000000000ll << "." << std::setw(9) << std::setfill('0')
            << time % 1000000000ll;
}

std::ostream& operator<<(std::ostream& os, AsUsTime value) {
  int64_t time = value.time_;

  switch (time) {
    case media::kUnspecifiedTime:
      return os << "unspecified";
    case media::kMinTime:
      return os << "min";
    case media::kMaxTime:
      return os << "max";
  }

  if (time < 0) {
    time = -time;
    os << "-";
  }

  return os << time / 1000000ll << "." << std::setw(6) << std::setfill('0')
            << time % 1000000ll;
}

std::ostream& operator<<(std::ostream& os, AsMsTime value) {
  int64_t time = value.time_;

  switch (time) {
    case media::kUnspecifiedTime:
      return os << "unspecified";
    case media::kMinTime:
      return os << "min";
    case media::kMaxTime:
      return os << "max";
  }

  if (time < 0) {
    time = -time;
    os << "-";
  }

  return os << time / 1000ll << "." << std::setw(3) << std::setfill('0')
            << time % 1000ll;
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

std::ostream& operator<<(std::ostream& os, media::TimelineRate value) {
  return os << value.subject_delta() << "/" << value.reference_delta();
}

}  // namespace handlers
}  // namespace flog

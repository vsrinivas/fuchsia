// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/fidl/fidl_formatting.h"

namespace media {

template <typename T>
std::ostream& operator<<(std::ostream& os, const fidl::InterfacePtr<T>& value) {
  if (!value.is_bound()) {
    return os << "<not bound>\n";
  } else {
    return os << "<bound>\n";
  }
}

std::ostream& operator<<(std::ostream& os, const MediaTypePtr& value) {
  if (!value) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  os << begl
     << "MediaTypeMedium medium: " << StringFromMediaTypeMedium(value->medium)
     << "\n";
  os << begl << "MediaTypeDetailsPtr details: " << value->details;
  os << begl << "string encoding: " << value->encoding << "\n";
  if (value->encoding_parameters) {
    os << begl << "array<uint8>? encoding_parameters: "
       << value->encoding_parameters.size() << " bytes\n";
  } else {
    os << begl << "array<uint8>? encoding_parameters: <nullptr>\n";
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const MediaTypeSetPtr& value) {
  if (!value) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  os << begl
     << "MediaTypeMedium medium: " << StringFromMediaTypeMedium(value->medium)
     << "\n";
  os << begl << "MediaTypeSetDetailsPtr details: " << value->details;
  os << begl << "array<string> encodings: " << value->encodings;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const MediaTypeDetailsPtr& value) {
  if (!value) {
    return os << "<nullptr>\n";
  } else if (value->has_unknown_tag()) {
    return os << "<empty>\n";
  } else {
    os << "\n";
  }

  os << indent;
  if (value->is_audio()) {
    return os << begl
              << "AudioMediaTypeDetailsPtr* audio: " << value->get_audio()
              << outdent;
  }
  if (value->is_video()) {
    return os << begl
              << "VideoMediaTypeDetailsPtr* video: " << value->get_video()
              << outdent;
  }
  if (value->is_text()) {
    return os << begl << "TextMediaTypeDetailsPtr* text: " << value->get_text()
              << outdent;
  }
  if (value->is_subpicture()) {
    return os << begl << "SubpictureMediaTypeDetailsPtr* video: "
              << value->get_subpicture() << outdent;
  }
  return os << begl << "UNKNOWN TAG\n" << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const MediaTypeSetDetailsPtr& value) {
  if (!value) {
    return os << "<nullptr>\n";
  } else if (value->has_unknown_tag()) {
    return os << "<empty>\n";
  } else {
    os << "\n";
  }

  os << indent;
  if (value->is_audio()) {
    return os << begl
              << "AudioMediaTypeSetDetailsPtr* audio: " << value->get_audio()
              << outdent;
  }
  if (value->is_video()) {
    return os << begl
              << "VideoMediaTypeSetDetailsPtr* video: " << value->get_video()
              << outdent;
  }
  if (value->is_text()) {
    return os << begl
              << "TextMediaTypeSetDetailsPtr* video: " << value->get_text()
              << outdent;
  }
  if (value->is_subpicture()) {
    return os << begl << "SubpictureMediaTypeSetDetailsPtr* video: "
              << value->get_subpicture() << outdent;
  }
  return os << begl << "UNKNOWN TAG\n" << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const AudioMediaTypeDetailsPtr& value) {
  if (!value) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  os << begl << "AudioSampleFormat sample_format: "
     << StringFromAudioSampleFormat(value->sample_format) << "\n";
  os << begl << "uint32_t channels: " << int(value->channels) << "\n";
  os << begl << "uint32_t frames_per_second: " << value->frames_per_second
     << "\n";
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const AudioMediaTypeSetDetailsPtr& value) {
  if (!value) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  os << begl << "AudioSampleFormat sample_format: "
     << StringFromAudioSampleFormat(value->sample_format) << "\n";
  os << begl << "uint32_t min_channels: " << int(value->min_channels) << "\n";
  os << begl << "uint32_t max_channels: " << int(value->max_channels) << "\n";
  os << begl
     << "uint32_t min_frames_per_second: " << value->min_frames_per_second
     << "\n";
  os << begl
     << "uint32_t max_cframes_per_second: " << value->max_frames_per_second
     << "\n";
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const VideoMediaTypeDetailsPtr& value) {
  if (!value) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  os << begl << "VideoProfile profile: " << value->profile << "\n";
  os << begl << "PixelFormat pixel_format: " << value->pixel_format << "\n";
  os << begl << "ColorSpace color_space: " << value->color_space << "\n";
  os << begl << "uint32_t width: " << value->width << "\n";
  os << begl << "uint32_t height: " << value->height << "\n";
  os << begl << "uint32_t coded_width: " << value->coded_width << "\n";
  os << begl << "uint32_t coded_height: " << value->coded_height << "\n";
  os << begl << "array<uint32_t> line_stride: "
     << AsInlineArray<uint32_t>(value->line_stride) << "\n";
  os << begl << "array<uint32_t> plane_offset: "
     << AsInlineArray<uint32_t>(value->plane_offset) << "\n";
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const VideoMediaTypeSetDetailsPtr& value) {
  if (!value) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  os << begl << "uint32_t min_width: " << value->min_width << "\n";
  os << begl << "uint32_t max_width: " << value->max_width << "\n";
  os << begl << "uint32_t min_height: " << value->min_height << "\n";
  os << begl << "uint32_t max_height: " << value->max_height << "\n";
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const TextMediaTypeDetailsPtr& value) {
  if (!value) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const TextMediaTypeSetDetailsPtr& value) {
  if (!value) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const SubpictureMediaTypeDetailsPtr& value) {
  if (!value) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const SubpictureMediaTypeSetDetailsPtr& value) {
  if (!value) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const TimelineTransformPtr& value) {
  if (!value) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  os << begl << "int64 reference_time: " << value->reference_time << "\n";
  os << begl << "int64 subject_time: " << value->subject_time << "\n";
  os << begl << "uint32 reference_delta: " << value->reference_delta << "\n";
  os << begl << "uint32 subject_delta: " << value->subject_delta << "\n";
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const network::HttpHeaderPtr& value) {
  if (!value) {
    return os << "<nullptr>\n";
  } else {
    return os << value->name << ":" << value->value << "\n";
  }
}

std::ostream& operator<<(std::ostream& os, const network::URLBodyPtr& value) {
  if (!value) {
    return os << "<nullptr>\n";
  } else {
    if (value->is_stream()) {
      return os << "mx::socket stream: " << value->get_stream() << "\n";
    } else if (value->is_buffer()) {
      return os << "mx::vmo buffer: " << value->get_buffer() << "\n";
    } else {
      return os << "<unknown>\n";
    }
  }
}

std::ostream& operator<<(std::ostream& os,
                         const network::URLRequestPtr& value) {
  if (!value) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  os << begl << "fidl::String url: " << value->url << "\n";
  os << begl << "fidl::String method: " << value->method << "\n";
  os << begl
     << "fidl::Array<network::HttpHeaderPtr> headers: " << value->headers;
  os << begl << "network::URLBody body: " << value->body;
  os << begl << "uint32_t response_body_buffer_size: "
     << value->response_body_buffer_size << "\n";
  os << begl << "bool auto_follow_redirects: " << value->auto_follow_redirects
     << "\n";
  os << begl
     << "network::URLRequest::CacheMode cache_mode: " << value->cache_mode
     << "\n";
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const network::URLResponsePtr& value) {
  if (!value) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  os << begl << "network::NetworkErrorPtr error: " << value->error;
  os << begl << "mx::socket body: " << value->body << "\n";
  os << begl << "fidl::String url: " << value->url << "\n";
  os << begl << "uint32_t status_code: " << value->status_code << "\n";
  os << begl << "fidl::String status_line: " << value->status_line << "\n";
  os << begl
     << "fidl::Array<network::HttpHeaderPtr> headers: " << value->headers;
  os << begl << "fidl::String mime_type: " << value->mime_type << "\n";
  os << begl << "fidl::String charset: " << value->charset << "\n";
  os << begl << "fidl::String redirect_method: " << value->redirect_method
     << "\n";
  os << begl << "fidl::String redirect_url: " << value->redirect_url << "\n";
  os << begl << "fidl::String redirect_referrer: " << value->redirect_referrer
     << "\n";
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const network::NetworkErrorPtr& value) {
  if (!value) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  os << begl << "int32_t code: " << value->code << "\n";
  os << begl << "fidl::String description: " << value->description << "\n";
  return os << outdent;
}

const char* StringFromMediaTypeMedium(MediaTypeMedium value) {
  switch (value) {
    case MediaTypeMedium::AUDIO:
      return "AUDIO";
    case MediaTypeMedium::VIDEO:
      return "VIDEO";
    case MediaTypeMedium::TEXT:
      return "TEXT";
    case MediaTypeMedium::SUBPICTURE:
      return "SUBPICTURE";
  }
  return "UNKNOWN MEDIUM";
}

const char* StringFromAudioSampleFormat(AudioSampleFormat value) {
  switch (value) {
    case AudioSampleFormat::NONE:
      return "NONE";
    case AudioSampleFormat::ANY:
      return "ANY";
    case AudioSampleFormat::UNSIGNED_8:
      return "UNSIGNED_8";
    case AudioSampleFormat::SIGNED_16:
      return "SIGNED_16";
    case AudioSampleFormat::SIGNED_24_IN_32:
      return "SIGNED_24_IN_32";
    case AudioSampleFormat::FLOAT:
      return "FLOAT";
  }
  return "UNKNOWN FORMAT";
}

}  // namespace media

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

std::ostream& operator<<(std::ostream& os, const MediaType& value) {
  os << "\n";
  os << indent;
  os << begl
     << "MediaTypeMedium medium: " << StringFromMediaTypeMedium(value.medium)
     << "\n";
  os << begl << "MediaTypeDetailsPtr details: " << value.details;
  os << begl << "string encoding: " << value.encoding << "\n";
  if (value.encoding_parameters) {
    os << begl << "array<uint8>? encoding_parameters: "
       << value.encoding_parameters->size() << " bytes\n";
  } else {
    os << begl << "array<uint8>? encoding_parameters: <nullptr>\n";
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const MediaTypeSet& value) {
  os << "\n";
  os << indent;
  os << begl
     << "MediaTypeMedium medium: " << StringFromMediaTypeMedium(value.medium)
     << "\n";
  os << begl << "MediaTypeSetDetailsPtr details: " << value.details;
  os << begl << "array<string> encodings: " << value.encodings;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const MediaTypeDetails& value) {
  if (value.has_invalid_tag()) {
    return os << "<empty>\n";
  } else {
    os << "\n";
  }

  os << indent;
  if (value.is_audio()) {
    return os << begl << "AudioMediaTypeDetailsPtr* audio: " << value.audio()
              << outdent;
  }
  if (value.is_video()) {
    return os << begl << "VideoMediaTypeDetailsPtr* video: " << value.video()
              << outdent;
  }
  if (value.is_text()) {
    return os << begl << "TextMediaTypeDetailsPtr* text: " << value.text()
              << outdent;
  }
  if (value.is_subpicture()) {
    return os << begl
              << "SubpictureMediaTypeDetailsPtr* video: " << value.subpicture()
              << outdent;
  }
  return os << begl << "UNKNOWN TAG\n" << outdent;
}

std::ostream& operator<<(std::ostream& os, const MediaTypeSetDetails& value) {
  if (value.has_invalid_tag()) {
    return os << "<empty>\n";
  } else {
    os << "\n";
  }

  os << indent;
  if (value.is_audio()) {
    return os << begl << "AudioMediaTypeSetDetailsPtr* audio: " << value.audio()
              << outdent;
  }
  if (value.is_video()) {
    return os << begl << "VideoMediaTypeSetDetailsPtr* video: " << value.video()
              << outdent;
  }
  if (value.is_text()) {
    return os << begl << "TextMediaTypeSetDetailsPtr* video: " << value.text()
              << outdent;
  }
  if (value.is_subpicture()) {
    return os << begl << "SubpictureMediaTypeSetDetailsPtr* video: "
              << value.subpicture() << outdent;
  }
  return os << begl << "UNKNOWN TAG\n" << outdent;
}

std::ostream& operator<<(std::ostream& os, const AudioMediaTypeDetails& value) {
  os << "\n";
  os << indent;
  os << begl << "AudioSampleFormat sample_format: "
     << StringFromAudioSampleFormat(value.sample_format) << "\n";
  os << begl << "uint32_t channels: " << int(value.channels) << "\n";
  os << begl << "uint32_t frames_per_second: " << value.frames_per_second
     << "\n";
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const AudioMediaTypeSetDetails& value) {
  os << "\n";
  os << indent;
  os << begl << "AudioSampleFormat sample_format: "
     << StringFromAudioSampleFormat(value.sample_format) << "\n";
  os << begl << "uint32_t min_channels: " << int(value.min_channels) << "\n";
  os << begl << "uint32_t max_channels: " << int(value.max_channels) << "\n";
  os << begl
     << "uint32_t min_frames_per_second: " << value.min_frames_per_second
     << "\n";
  os << begl
     << "uint32_t max_cframes_per_second: " << value.max_frames_per_second
     << "\n";
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const VideoMediaTypeDetails& value) {
  os << "\n";
  os << indent;
  // TODO(fidl2): Re-enable these formatters when they work.
  // os << begl << "VideoProfile profile: " << value.profile << "\n";
  // os << begl << "PixelFormat pixel_format: " << value.pixel_format << "\n";
  // os << begl << "ColorSpace color_space: " << value.color_space << "\n";
  os << begl << "uint32_t width: " << value.width << "\n";
  os << begl << "uint32_t height: " << value.height << "\n";
  os << begl << "uint32_t coded_width: " << value.coded_width << "\n";
  os << begl << "uint32_t coded_height: " << value.coded_height << "\n";
  os << begl << "array<uint32_t> line_stride: "
     << AsInlineArray<uint32_t>(value.line_stride) << "\n";
  os << begl << "array<uint32_t> plane_offset: "
     << AsInlineArray<uint32_t>(value.plane_offset) << "\n";
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const VideoMediaTypeSetDetails& value) {
  os << "\n";
  os << indent;
  os << begl << "uint32_t min_width: " << value.min_width << "\n";
  os << begl << "uint32_t max_width: " << value.max_width << "\n";
  os << begl << "uint32_t min_height: " << value.min_height << "\n";
  os << begl << "uint32_t max_height: " << value.max_height << "\n";
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const TextMediaTypeDetails& value) {
  os << "\n";
  os << indent;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const TextMediaTypeSetDetails& value) {
  os << "\n";
  os << indent;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const SubpictureMediaTypeDetails& value) {
  os << "\n";
  os << indent;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os,
                         const SubpictureMediaTypeSetDetails& value) {
  os << "\n";
  os << indent;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const TimelineTransform& value) {
  os << "\n";
  os << indent;
  os << begl << "int64 reference_time: " << value.reference_time << "\n";
  os << begl << "int64 subject_time: " << value.subject_time << "\n";
  os << begl << "uint32 reference_delta: " << value.reference_delta << "\n";
  os << begl << "uint32 subject_delta: " << value.subject_delta << "\n";
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

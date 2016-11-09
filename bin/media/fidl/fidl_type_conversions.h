// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/interfaces/media_common.fidl.h"
#include "apps/media/interfaces/media_metadata.fidl.h"
#include "apps/media/interfaces/media_types.fidl.h"
#include "apps/media/src/framework/metadata.h"
#include "apps/media/src/framework/result.h"
#include "apps/media/src/framework/types/audio_stream_type.h"
#include "apps/media/src/framework/types/stream_type.h"
#include "apps/media/src/framework/types/video_stream_type.h"

namespace media {

// Converts a MediaResult into a Result.
Result Convert(MediaResult media_result);

// Creates a StreamType::Medium from a MediaTypeMedium.
StreamType::Medium Convert(MediaTypeMedium media_type_medium);

// Creates an AudioStreamType::SampleFormat from an AudioSampleFormat.
AudioStreamType::SampleFormat Convert(AudioSampleFormat audio_sample_format);

// Creates a VideoStreamType::VideoProfile from a VideoProfile.
VideoStreamType::VideoProfile Convert(VideoProfile video_profile);

// Creates a VideoStreamType::PixelFormat from a PixelFormat.
VideoStreamType::PixelFormat Convert(PixelFormat pixel_format);

// Creates a VideoStreamType::ColorSpace from a ColorSpace.
VideoStreamType::ColorSpace Convert(ColorSpace color_space);

// Creates a MediaTypeMedium from a StreamType::Medium.
MediaTypeMedium Convert(StreamType::Medium medium);

// Creates an AudioSampleFormat from an AudioStreamType::SampleFormat.
AudioSampleFormat Convert(AudioStreamType::SampleFormat sample_format);

// Creates a VideoProfile from a VideoStreamType::VideoProfile.
VideoProfile Convert(VideoStreamType::VideoProfile video_profile);

// Creates a PixelFormat from a VideoStreamType::PixelFormat.
PixelFormat Convert(VideoStreamType::PixelFormat pixel_format);

// Creates a ColorSpace from a VideoStreamType::ColorSpace.
ColorSpace Convert(VideoStreamType::ColorSpace color_space);

}  // namespace media

namespace fidl {

template <>
struct TypeConverter<media::MediaTypePtr, std::unique_ptr<media::StreamType>> {
  static media::MediaTypePtr Convert(
      const std::unique_ptr<media::StreamType>& input);
};

template <>
struct TypeConverter<std::unique_ptr<media::StreamType>, media::MediaTypePtr> {
  static std::unique_ptr<media::StreamType> Convert(
      const media::MediaTypePtr& input);
};

template <>
struct TypeConverter<media::MediaTypeSetPtr,
                     std::unique_ptr<media::StreamTypeSet>> {
  static media::MediaTypeSetPtr Convert(
      const std::unique_ptr<media::StreamTypeSet>& input);
};

template <>
struct TypeConverter<std::unique_ptr<media::StreamTypeSet>,
                     media::MediaTypeSetPtr> {
  static std::unique_ptr<media::StreamTypeSet> Convert(
      const media::MediaTypeSetPtr& input);
};

template <>
struct TypeConverter<media::MediaMetadataPtr,
                     std::unique_ptr<media::Metadata>> {
  static media::MediaMetadataPtr Convert(
      const std::unique_ptr<media::Metadata>& input);
};

template <>
struct TypeConverter<std::unique_ptr<media::Metadata>,
                     media::MediaMetadataPtr> {
  static std::unique_ptr<media::Metadata> Convert(
      const media::MediaMetadataPtr& input);
};

template <>
struct TypeConverter<
    fidl::Array<media::MediaTypePtr>,
    std::unique_ptr<std::vector<std::unique_ptr<media::StreamType>>>> {
  static fidl::Array<media::MediaTypePtr> Convert(
      const std::unique_ptr<std::vector<std::unique_ptr<media::StreamType>>>&
          input);
};

template <>
struct TypeConverter<
    std::unique_ptr<std::vector<std::unique_ptr<media::StreamType>>>,
    fidl::Array<media::MediaTypePtr>> {
  static std::unique_ptr<std::vector<std::unique_ptr<media::StreamType>>>
  Convert(const fidl::Array<media::MediaTypePtr>& input);
};

template <>
struct TypeConverter<
    fidl::Array<media::MediaTypeSetPtr>,
    std::unique_ptr<std::vector<std::unique_ptr<media::StreamTypeSet>>>> {
  static fidl::Array<media::MediaTypeSetPtr> Convert(
      const std::unique_ptr<std::vector<std::unique_ptr<media::StreamTypeSet>>>&
          input);
};

template <>
struct TypeConverter<
    std::unique_ptr<std::vector<std::unique_ptr<media::StreamTypeSet>>>,
    fidl::Array<media::MediaTypeSetPtr>> {
  static std::unique_ptr<std::vector<std::unique_ptr<media::StreamTypeSet>>>
  Convert(const fidl::Array<media::MediaTypeSetPtr>& input);
};

template <>
struct TypeConverter<fidl::Array<uint8_t>, std::unique_ptr<media::Bytes>> {
  static fidl::Array<uint8_t> Convert(
      const std::unique_ptr<media::Bytes>& input);
};

template <>
struct TypeConverter<std::unique_ptr<media::Bytes>, fidl::Array<uint8_t>> {
  static std::unique_ptr<media::Bytes> Convert(
      const fidl::Array<uint8_t>& input);
};

}  // namespace fidl

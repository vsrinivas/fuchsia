// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_FIDL_FIDL_TYPE_CONVERSIONS_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_FIDL_FIDL_TYPE_CONVERSIONS_H_

#include <media/cpp/fidl.h>
#include <media_player/cpp/fidl.h>

#include "garnet/bin/media/media_player/framework/metadata.h"
#include "garnet/bin/media/media_player/framework/result.h"
#include "garnet/bin/media/media_player/framework/types/audio_stream_type.h"
#include "garnet/bin/media/media_player/framework/types/stream_type.h"
#include "garnet/bin/media/media_player/framework/types/video_stream_type.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/types/type_converters.h"
#include "lib/fxl/type_converter.h"

namespace fxl {

template <>
struct TypeConverter<media_player::Result, media::MediaResult> {
  static media_player::Result Convert(media::MediaResult media_result);
};

template <>
struct TypeConverter<media_player::StreamType::Medium, media::MediaTypeMedium> {
  static media_player::StreamType::Medium Convert(
      media::MediaTypeMedium media_type_medium);
};

template <>
struct TypeConverter<media_player::AudioStreamType::SampleFormat,
                     media::AudioSampleFormat> {
  static media_player::AudioStreamType::SampleFormat Convert(
      media::AudioSampleFormat audio_sample_format);
};

template <>
struct TypeConverter<media_player::VideoStreamType::VideoProfile,
                     media::VideoProfile> {
  static media_player::VideoStreamType::VideoProfile Convert(
      media::VideoProfile video_profile);
};

template <>
struct TypeConverter<media_player::VideoStreamType::PixelFormat,
                     media::PixelFormat> {
  static media_player::VideoStreamType::PixelFormat Convert(
      media::PixelFormat pixel_format);
};

template <>
struct TypeConverter<media_player::VideoStreamType::ColorSpace,
                     media::ColorSpace> {
  static media_player::VideoStreamType::ColorSpace Convert(
      media::ColorSpace color_space);
};

template <>
struct TypeConverter<media::MediaTypeMedium, media_player::StreamType::Medium> {
  static media::MediaTypeMedium Convert(
      media_player::StreamType::Medium medium);
};

template <>
struct TypeConverter<media::AudioSampleFormat,
                     media_player::AudioStreamType::SampleFormat> {
  static media::AudioSampleFormat Convert(
      media_player::AudioStreamType::SampleFormat sample_format);
};

template <>
struct TypeConverter<media::VideoProfile,
                     media_player::VideoStreamType::VideoProfile> {
  static media::VideoProfile Convert(
      media_player::VideoStreamType::VideoProfile video_profile);
};

template <>
struct TypeConverter<media::PixelFormat,
                     media_player::VideoStreamType::PixelFormat> {
  static media::PixelFormat Convert(
      media_player::VideoStreamType::PixelFormat pixel_format);
};

template <>
struct TypeConverter<media::ColorSpace,
                     media_player::VideoStreamType::ColorSpace> {
  static media::ColorSpace Convert(
      media_player::VideoStreamType::ColorSpace color_space);
};

template <>
struct TypeConverter<media::MediaType, media_player::StreamType> {
  static media::MediaType Convert(const media_player::StreamType& input);
};

template <>
struct TypeConverter<media::MediaType,
                     std::unique_ptr<media_player::StreamType>> {
  static media::MediaType Convert(
      const std::unique_ptr<media_player::StreamType>& input) {
    FXL_DCHECK(input);
    return fxl::To<media::MediaType>(*input);
  }
};

template <>
struct TypeConverter<media::MediaTypePtr,
                     std::unique_ptr<media_player::StreamType>> {
  static media::MediaTypePtr Convert(
      const std::unique_ptr<media_player::StreamType>& input) {
    if (!input)
      return nullptr;
    return fidl::MakeOptional(fxl::To<media::MediaType>(*input));
  }
};

template <>
struct TypeConverter<std::unique_ptr<media_player::StreamType>,
                     media::MediaType> {
  static std::unique_ptr<media_player::StreamType> Convert(
      const media::MediaType& input);
};

template <>
struct TypeConverter<std::unique_ptr<media_player::StreamType>,
                     media::MediaTypePtr> {
  static std::unique_ptr<media_player::StreamType> Convert(
      const media::MediaTypePtr& input) {
    if (!input)
      return nullptr;
    return To<std::unique_ptr<media_player::StreamType>>(*input);
  }
};

template <>
struct TypeConverter<media::MediaTypeSet,
                     std::unique_ptr<media_player::StreamTypeSet>> {
  static media::MediaTypeSet Convert(
      const std::unique_ptr<media_player::StreamTypeSet>& input);
};

template <>
struct TypeConverter<std::unique_ptr<media_player::StreamTypeSet>,
                     media::MediaTypeSet> {
  static std::unique_ptr<media_player::StreamTypeSet> Convert(
      const media::MediaTypeSet& input);
};

template <>
struct TypeConverter<media_player::MediaMetadataPtr,
                     std::unique_ptr<media_player::Metadata>> {
  static media_player::MediaMetadataPtr Convert(
      const std::unique_ptr<media_player::Metadata>& input);
};

template <>
struct TypeConverter<media_player::MediaMetadataPtr,
                     const media_player::Metadata*> {
  static media_player::MediaMetadataPtr Convert(
      const media_player::Metadata* input);
};

template <>
struct TypeConverter<media_player::MediaMetadataPtr, media_player::Metadata> {
  static media_player::MediaMetadataPtr Convert(
      const media_player::Metadata& input);
};

template <>
struct TypeConverter<std::unique_ptr<media_player::Metadata>,
                     media_player::MediaMetadataPtr> {
  static std::unique_ptr<media_player::Metadata> Convert(
      const media_player::MediaMetadataPtr& input);
};

template <>
struct TypeConverter<fidl::VectorPtr<uint8_t>,
                     std::unique_ptr<media_player::Bytes>> {
  static fidl::VectorPtr<uint8_t> Convert(
      const std::unique_ptr<media_player::Bytes>& input);
};

template <>
struct TypeConverter<std::unique_ptr<media_player::Bytes>,
                     fidl::VectorPtr<uint8_t>> {
  static std::unique_ptr<media_player::Bytes> Convert(
      const fidl::VectorPtr<uint8_t>& input);
};

}  // namespace fxl

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_FIDL_FIDL_TYPE_CONVERSIONS_H_

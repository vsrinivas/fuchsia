// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_FIDL_FIDL_TYPE_CONVERSIONS_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_FIDL_FIDL_TYPE_CONVERSIONS_H_

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/mediacodec/cpp/fidl.h>
#include <fuchsia/mediaplayer/cpp/fidl.h>

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
struct TypeConverter<media_player::Result, fuchsia::mediaplayer::MediaResult> {
  static media_player::Result Convert(
      fuchsia::mediaplayer::MediaResult media_result);
};

template <>
struct TypeConverter<media_player::AudioStreamType::SampleFormat,
                     fuchsia::media::AudioSampleFormat> {
  static media_player::AudioStreamType::SampleFormat Convert(
      fuchsia::media::AudioSampleFormat audio_sample_format);
};

template <>
struct TypeConverter<media_player::VideoStreamType::VideoProfile,
                     fuchsia::media::VideoProfile> {
  static media_player::VideoStreamType::VideoProfile Convert(
      fuchsia::media::VideoProfile video_profile);
};

template <>
struct TypeConverter<media_player::VideoStreamType::PixelFormat,
                     fuchsia::media::PixelFormat> {
  static media_player::VideoStreamType::PixelFormat Convert(
      fuchsia::media::PixelFormat pixel_format);
};

template <>
struct TypeConverter<media_player::VideoStreamType::ColorSpace,
                     fuchsia::media::ColorSpace> {
  static media_player::VideoStreamType::ColorSpace Convert(
      fuchsia::media::ColorSpace color_space);
};

template <>
struct TypeConverter<fuchsia::media::AudioSampleFormat,
                     media_player::AudioStreamType::SampleFormat> {
  static fuchsia::media::AudioSampleFormat Convert(
      media_player::AudioStreamType::SampleFormat sample_format);
};

template <>
struct TypeConverter<fuchsia::media::VideoProfile,
                     media_player::VideoStreamType::VideoProfile> {
  static fuchsia::media::VideoProfile Convert(
      media_player::VideoStreamType::VideoProfile video_profile);
};

template <>
struct TypeConverter<fuchsia::media::PixelFormat,
                     media_player::VideoStreamType::PixelFormat> {
  static fuchsia::media::PixelFormat Convert(
      media_player::VideoStreamType::PixelFormat pixel_format);
};

template <>
struct TypeConverter<fuchsia::media::ColorSpace,
                     media_player::VideoStreamType::ColorSpace> {
  static fuchsia::media::ColorSpace Convert(
      media_player::VideoStreamType::ColorSpace color_space);
};

template <>
struct TypeConverter<fuchsia::media::StreamType, media_player::StreamType> {
  static fuchsia::media::StreamType Convert(
      const media_player::StreamType& input);
};

template <>
struct TypeConverter<fuchsia::media::StreamType,
                     std::unique_ptr<media_player::StreamType>> {
  static fuchsia::media::StreamType Convert(
      const std::unique_ptr<media_player::StreamType>& input) {
    FXL_DCHECK(input);
    return fxl::To<fuchsia::media::StreamType>(*input);
  }
};

template <>
struct TypeConverter<fuchsia::media::StreamTypePtr,
                     std::unique_ptr<media_player::StreamType>> {
  static fuchsia::media::StreamTypePtr Convert(
      const std::unique_ptr<media_player::StreamType>& input) {
    if (!input)
      return nullptr;
    return fidl::MakeOptional(fxl::To<fuchsia::media::StreamType>(*input));
  }
};

template <>
struct TypeConverter<std::unique_ptr<media_player::StreamType>,
                     fuchsia::media::StreamType> {
  static std::unique_ptr<media_player::StreamType> Convert(
      const fuchsia::media::StreamType& input);
};

template <>
struct TypeConverter<std::unique_ptr<media_player::StreamType>,
                     fuchsia::media::StreamTypePtr> {
  static std::unique_ptr<media_player::StreamType> Convert(
      const fuchsia::media::StreamTypePtr& input) {
    if (!input)
      return nullptr;
    return To<std::unique_ptr<media_player::StreamType>>(*input);
  }
};

template <>
struct TypeConverter<fuchsia::mediaplayer::Metadata, media_player::Metadata> {
  static fuchsia::mediaplayer::Metadata Convert(
      const media_player::Metadata& input);
};

template <>
struct TypeConverter<media_player::Metadata, fuchsia::mediaplayer::Metadata> {
  static media_player::Metadata Convert(
      const fuchsia::mediaplayer::Metadata& input);
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

template <>
struct TypeConverter<fuchsia::mediacodec::CodecFormatDetailsPtr,
                     media_player::StreamType> {
  static fuchsia::mediacodec::CodecFormatDetailsPtr Convert(
      const media_player::StreamType& input);
};

template <>
struct TypeConverter<std::unique_ptr<media_player::StreamType>,
                     fuchsia::mediacodec::CodecFormatDetails> {
  static std::unique_ptr<media_player::StreamType> Convert(
      const fuchsia::mediacodec::CodecFormatDetails& input);
};

}  // namespace fxl

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_FIDL_FIDL_TYPE_CONVERSIONS_H_

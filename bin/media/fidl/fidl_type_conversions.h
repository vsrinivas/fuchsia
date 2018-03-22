// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/framework/metadata.h"
#include "garnet/bin/media/framework/result.h"
#include "garnet/bin/media/framework/types/audio_stream_type.h"
#include "garnet/bin/media/framework/types/stream_type.h"
#include "garnet/bin/media/framework/types/video_stream_type.h"
#include "lib/fxl/type_converter.h"
#include "lib/media/fidl/media_metadata.fidl.h"
#include "lib/media/fidl/media_result.fidl.h"
#include "lib/media/fidl/media_types.fidl.h"

namespace fxl {

template <>
struct TypeConverter<media::Result, media::MediaResult> {
  static media::Result Convert(media::MediaResult media_result);
};

template <>
struct TypeConverter<media::StreamType::Medium, media::MediaTypeMedium> {
  static media::StreamType::Medium Convert(
      media::MediaTypeMedium media_type_medium);
};

template <>
struct TypeConverter<media::AudioStreamType::SampleFormat,
                     media::AudioSampleFormat> {
  static media::AudioStreamType::SampleFormat Convert(
      media::AudioSampleFormat audio_sample_format);
};

template <>
struct TypeConverter<media::VideoStreamType::VideoProfile,
                     media::VideoProfile> {
  static media::VideoStreamType::VideoProfile Convert(
      media::VideoProfile video_profile);
};

template <>
struct TypeConverter<media::VideoStreamType::PixelFormat, media::PixelFormat> {
  static media::VideoStreamType::PixelFormat Convert(
      media::PixelFormat pixel_format);
};

template <>
struct TypeConverter<media::VideoStreamType::ColorSpace, media::ColorSpace> {
  static media::VideoStreamType::ColorSpace Convert(
      media::ColorSpace color_space);
};

template <>
struct TypeConverter<media::MediaTypeMedium, media::StreamType::Medium> {
  static media::MediaTypeMedium Convert(media::StreamType::Medium medium);
};

template <>
struct TypeConverter<media::AudioSampleFormat,
                     media::AudioStreamType::SampleFormat> {
  static media::AudioSampleFormat Convert(
      media::AudioStreamType::SampleFormat sample_format);
};

template <>
struct TypeConverter<media::VideoProfile,
                     media::VideoStreamType::VideoProfile> {
  static media::VideoProfile Convert(
      media::VideoStreamType::VideoProfile video_profile);
};

template <>
struct TypeConverter<media::PixelFormat, media::VideoStreamType::PixelFormat> {
  static media::PixelFormat Convert(
      media::VideoStreamType::PixelFormat pixel_format);
};

template <>
struct TypeConverter<media::ColorSpace, media::VideoStreamType::ColorSpace> {
  static media::ColorSpace Convert(
      media::VideoStreamType::ColorSpace color_space);
};

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
struct TypeConverter<f1dl::VectorPtr<uint8_t>, std::unique_ptr<media::Bytes>> {
  static f1dl::VectorPtr<uint8_t> Convert(
      const std::unique_ptr<media::Bytes>& input);
};

template <>
struct TypeConverter<std::unique_ptr<media::Bytes>, f1dl::VectorPtr<uint8_t>> {
  static std::unique_ptr<media::Bytes> Convert(
      const f1dl::VectorPtr<uint8_t>& input);
};

}  // namespace fxl

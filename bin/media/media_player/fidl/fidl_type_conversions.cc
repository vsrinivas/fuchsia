// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/fidl/fidl_type_conversions.h"

#include "garnet/bin/media/media_player/framework/types/audio_stream_type.h"
#include "garnet/bin/media/media_player/framework/types/subpicture_stream_type.h"
#include "garnet/bin/media/media_player/framework/types/text_stream_type.h"
#include "garnet/bin/media/media_player/framework/types/video_stream_type.h"

namespace fxl {

namespace {

bool KnownEncodingsMatch() {
  return !strcmp(media_player::StreamType::kAudioEncodingAac,
                 fuchsia::media::kAudioEncodingAac) &&
         !strcmp(media_player::StreamType::kAudioEncodingAmrNb,
                 fuchsia::media::kAudioEncodingAmrNb) &&
         !strcmp(media_player::StreamType::kAudioEncodingAmrWb,
                 fuchsia::media::kAudioEncodingAmrWb) &&
         !strcmp(media_player::StreamType::kAudioEncodingFlac,
                 fuchsia::media::kAudioEncodingFlac) &&
         !strcmp(media_player::StreamType::kAudioEncodingGsmMs,
                 fuchsia::media::kAudioEncodingGsmMs) &&
         !strcmp(media_player::StreamType::kAudioEncodingLpcm,
                 fuchsia::media::kAudioEncodingLpcm) &&
         !strcmp(media_player::StreamType::kAudioEncodingMp3,
                 fuchsia::media::kAudioEncodingMp3) &&
         !strcmp(media_player::StreamType::kAudioEncodingPcmALaw,
                 fuchsia::media::kAudioEncodingPcmALaw) &&
         !strcmp(media_player::StreamType::kAudioEncodingPcmMuLaw,
                 fuchsia::media::kAudioEncodingPcmMuLaw) &&
         !strcmp(media_player::StreamType::kAudioEncodingVorbis,
                 fuchsia::media::kAudioEncodingVorbis) &&
         !strcmp(media_player::StreamType::kVideoEncodingH263,
                 fuchsia::media::kVideoEncodingH263) &&
         !strcmp(media_player::StreamType::kVideoEncodingH264,
                 fuchsia::media::kVideoEncodingH264) &&
         !strcmp(media_player::StreamType::kVideoEncodingMpeg4,
                 fuchsia::media::kVideoEncodingMpeg4) &&
         !strcmp(media_player::StreamType::kVideoEncodingTheora,
                 fuchsia::media::kVideoEncodingTheora) &&
         !strcmp(media_player::StreamType::kVideoEncodingUncompressed,
                 fuchsia::media::kVideoEncodingUncompressed) &&
         !strcmp(media_player::StreamType::kVideoEncodingVp3,
                 fuchsia::media::kVideoEncodingVp3) &&
         !strcmp(media_player::StreamType::kVideoEncodingVp8,
                 fuchsia::media::kVideoEncodingVp8) &&
         !strcmp(media_player::StreamType::kVideoEncodingVp9,
                 fuchsia::media::kVideoEncodingVp9);
}

}  // namespace

media_player::Result
TypeConverter<media_player::Result, fuchsia::mediaplayer::MediaResult>::Convert(
    fuchsia::mediaplayer::MediaResult media_result) {
  switch (media_result) {
    case fuchsia::mediaplayer::MediaResult::OK:
      return media_player::Result::kOk;
    case fuchsia::mediaplayer::MediaResult::INTERNAL_ERROR:
      return media_player::Result::kInternalError;
    case fuchsia::mediaplayer::MediaResult::UNSUPPORTED_OPERATION:
    case fuchsia::mediaplayer::MediaResult::NOT_IMPLEMENTED:
      return media_player::Result::kUnsupportedOperation;
    case fuchsia::mediaplayer::MediaResult::INVALID_ARGUMENT:
      return media_player::Result::kInvalidArgument;
    case fuchsia::mediaplayer::MediaResult::NOT_FOUND:
      return media_player::Result::kNotFound;
    case fuchsia::mediaplayer::MediaResult::UNKNOWN_ERROR:
    case fuchsia::mediaplayer::MediaResult::UNSUPPORTED_CONFIG:
    case fuchsia::mediaplayer::MediaResult::INSUFFICIENT_RESOURCES:
    case fuchsia::mediaplayer::MediaResult::BAD_STATE:
    case fuchsia::mediaplayer::MediaResult::BUF_OVERFLOW:
    case fuchsia::mediaplayer::MediaResult::FLUSHED:
    case fuchsia::mediaplayer::MediaResult::BUSY:
    case fuchsia::mediaplayer::MediaResult::PROTOCOL_ERROR:
    case fuchsia::mediaplayer::MediaResult::ALREADY_EXISTS:
    case fuchsia::mediaplayer::MediaResult::SHUTTING_DOWN:
    case fuchsia::mediaplayer::MediaResult::CONNECTION_LOST:
      break;
  }

  return media_player::Result::kUnknownError;
}

media_player::StreamType::Medium TypeConverter<
    media_player::StreamType::Medium,
    fuchsia::media::MediaTypeMedium>::Convert(fuchsia::media::MediaTypeMedium
                                                  media_type_medium) {
  switch (media_type_medium) {
    case fuchsia::media::MediaTypeMedium::AUDIO:
      return media_player::StreamType::Medium::kAudio;
    case fuchsia::media::MediaTypeMedium::VIDEO:
      return media_player::StreamType::Medium::kVideo;
    case fuchsia::media::MediaTypeMedium::TEXT:
      return media_player::StreamType::Medium::kText;
    case fuchsia::media::MediaTypeMedium::SUBPICTURE:
      return media_player::StreamType::Medium::kSubpicture;
  }

  FXL_LOG(ERROR) << "unrecognized medium";
  abort();
}

media_player::AudioStreamType::SampleFormat
TypeConverter<media_player::AudioStreamType::SampleFormat,
              fuchsia::media::AudioSampleFormat>::
    Convert(fuchsia::media::AudioSampleFormat audio_sample_format) {
  switch (audio_sample_format) {
    case fuchsia::media::AudioSampleFormat::NONE:
      return media_player::AudioStreamType::SampleFormat::kNone;
    case fuchsia::media::AudioSampleFormat::ANY:
      return media_player::AudioStreamType::SampleFormat::kAny;
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      return media_player::AudioStreamType::SampleFormat::kUnsigned8;
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      return media_player::AudioStreamType::SampleFormat::kSigned16;
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      return media_player::AudioStreamType::SampleFormat::kSigned24In32;
    case fuchsia::media::AudioSampleFormat::FLOAT:
      return media_player::AudioStreamType::SampleFormat::kFloat;
  }

  FXL_LOG(ERROR) << "unrecognized sample format";
  abort();
}

media_player::VideoStreamType::VideoProfile TypeConverter<
    media_player::VideoStreamType::VideoProfile,
    fuchsia::media::VideoProfile>::Convert(fuchsia::media::VideoProfile
                                               video_profile) {
  switch (video_profile) {
    case fuchsia::media::VideoProfile::UNKNOWN:
      return media_player::VideoStreamType::VideoProfile::kUnknown;
    case fuchsia::media::VideoProfile::NOT_APPLICABLE:
      return media_player::VideoStreamType::VideoProfile::kNotApplicable;
    case fuchsia::media::VideoProfile::H264_BASELINE:
      return media_player::VideoStreamType::VideoProfile::kH264Baseline;
    case fuchsia::media::VideoProfile::H264_MAIN:
      return media_player::VideoStreamType::VideoProfile::kH264Main;
    case fuchsia::media::VideoProfile::H264_EXTENDED:
      return media_player::VideoStreamType::VideoProfile::kH264Extended;
    case fuchsia::media::VideoProfile::H264_HIGH:
      return media_player::VideoStreamType::VideoProfile::kH264High;
    case fuchsia::media::VideoProfile::H264_HIGH10:
      return media_player::VideoStreamType::VideoProfile::kH264High10;
    case fuchsia::media::VideoProfile::H264_HIGH422:
      return media_player::VideoStreamType::VideoProfile::kH264High422;
    case fuchsia::media::VideoProfile::H264_HIGH444_PREDICTIVE:
      return media_player::VideoStreamType::VideoProfile::
          kH264High444Predictive;
    case fuchsia::media::VideoProfile::H264_SCALABLE_BASELINE:
      return media_player::VideoStreamType::VideoProfile::kH264ScalableBaseline;
    case fuchsia::media::VideoProfile::H264_SCALABLE_HIGH:
      return media_player::VideoStreamType::VideoProfile::kH264ScalableHigh;
    case fuchsia::media::VideoProfile::H264_STEREO_HIGH:
      return media_player::VideoStreamType::VideoProfile::kH264StereoHigh;
    case fuchsia::media::VideoProfile::H264_MULTIVIEW_HIGH:
      return media_player::VideoStreamType::VideoProfile::kH264MultiviewHigh;
  }

  FXL_LOG(ERROR);
  abort();
}

media_player::VideoStreamType::PixelFormat
TypeConverter<media_player::VideoStreamType::PixelFormat,
              fuchsia::media::PixelFormat>::Convert(fuchsia::media::PixelFormat
                                                        pixel_format) {
  switch (pixel_format) {
    case fuchsia::media::PixelFormat::UNKNOWN:
      return media_player::VideoStreamType::PixelFormat::kUnknown;
    case fuchsia::media::PixelFormat::I420:
      return media_player::VideoStreamType::PixelFormat::kI420;
    case fuchsia::media::PixelFormat::YV12:
      return media_player::VideoStreamType::PixelFormat::kYv12;
    case fuchsia::media::PixelFormat::YV16:
      return media_player::VideoStreamType::PixelFormat::kYv16;
    case fuchsia::media::PixelFormat::YV12A:
      return media_player::VideoStreamType::PixelFormat::kYv12A;
    case fuchsia::media::PixelFormat::YV24:
      return media_player::VideoStreamType::PixelFormat::kYv24;
    case fuchsia::media::PixelFormat::NV12:
      return media_player::VideoStreamType::PixelFormat::kNv12;
    case fuchsia::media::PixelFormat::NV21:
      return media_player::VideoStreamType::PixelFormat::kNv21;
    case fuchsia::media::PixelFormat::UYVY:
      return media_player::VideoStreamType::PixelFormat::kUyvy;
    case fuchsia::media::PixelFormat::YUY2:
      return media_player::VideoStreamType::PixelFormat::kYuy2;
    case fuchsia::media::PixelFormat::ARGB:
      return media_player::VideoStreamType::PixelFormat::kArgb;
    case fuchsia::media::PixelFormat::XRGB:
      return media_player::VideoStreamType::PixelFormat::kXrgb;
    case fuchsia::media::PixelFormat::RGB24:
      return media_player::VideoStreamType::PixelFormat::kRgb24;
    case fuchsia::media::PixelFormat::RGB32:
      return media_player::VideoStreamType::PixelFormat::kRgb32;
    case fuchsia::media::PixelFormat::MJPEG:
      return media_player::VideoStreamType::PixelFormat::kMjpeg;
    case fuchsia::media::PixelFormat::MT21:
      return media_player::VideoStreamType::PixelFormat::kMt21;
  }

  return media_player::VideoStreamType::PixelFormat::kUnknown;
}

media_player::VideoStreamType::ColorSpace
TypeConverter<media_player::VideoStreamType::ColorSpace,
              fuchsia::media::ColorSpace>::Convert(fuchsia::media::ColorSpace
                                                       color_space) {
  switch (color_space) {
    case fuchsia::media::ColorSpace::UNKNOWN:
      return media_player::VideoStreamType::ColorSpace::kUnknown;
    case fuchsia::media::ColorSpace::NOT_APPLICABLE:
      return media_player::VideoStreamType::ColorSpace::kNotApplicable;
    case fuchsia::media::ColorSpace::JPEG:
      return media_player::VideoStreamType::ColorSpace::kJpeg;
    case fuchsia::media::ColorSpace::HD_REC709:
      return media_player::VideoStreamType::ColorSpace::kHdRec709;
    case fuchsia::media::ColorSpace::SD_REC601:
      return media_player::VideoStreamType::ColorSpace::kSdRec601;
  }

  return media_player::VideoStreamType::ColorSpace::kUnknown;
}

fuchsia::media::MediaTypeMedium TypeConverter<
    fuchsia::media::MediaTypeMedium,
    media_player::StreamType::Medium>::Convert(media_player::StreamType::Medium
                                                   medium) {
  switch (medium) {
    case media_player::StreamType::Medium::kAudio:
      return fuchsia::media::MediaTypeMedium::AUDIO;
    case media_player::StreamType::Medium::kVideo:
      return fuchsia::media::MediaTypeMedium::VIDEO;
    case media_player::StreamType::Medium::kText:
      return fuchsia::media::MediaTypeMedium::TEXT;
    case media_player::StreamType::Medium::kSubpicture:
      return fuchsia::media::MediaTypeMedium::SUBPICTURE;
  }

  FXL_LOG(ERROR) << "unrecognized medium";
  abort();
}

fuchsia::media::AudioSampleFormat
TypeConverter<fuchsia::media::AudioSampleFormat,
              media_player::AudioStreamType::SampleFormat>::
    Convert(media_player::AudioStreamType::SampleFormat sample_format) {
  switch (sample_format) {
    case media_player::AudioStreamType::SampleFormat::kNone:
      return fuchsia::media::AudioSampleFormat::NONE;
    case media_player::AudioStreamType::SampleFormat::kAny:
      return fuchsia::media::AudioSampleFormat::ANY;
    case media_player::AudioStreamType::SampleFormat::kUnsigned8:
      return fuchsia::media::AudioSampleFormat::UNSIGNED_8;
    case media_player::AudioStreamType::SampleFormat::kSigned16:
      return fuchsia::media::AudioSampleFormat::SIGNED_16;
    case media_player::AudioStreamType::SampleFormat::kSigned24In32:
      return fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32;
    case media_player::AudioStreamType::SampleFormat::kFloat:
      return fuchsia::media::AudioSampleFormat::FLOAT;
  }

  FXL_LOG(ERROR) << "unrecognized sample format";
  abort();
}

fuchsia::media::VideoProfile TypeConverter<
    fuchsia::media::VideoProfile, media_player::VideoStreamType::VideoProfile>::
    Convert(media_player::VideoStreamType::VideoProfile video_profile) {
  switch (video_profile) {
    case media_player::VideoStreamType::VideoProfile::kUnknown:
      return fuchsia::media::VideoProfile::UNKNOWN;
    case media_player::VideoStreamType::VideoProfile::kNotApplicable:
      return fuchsia::media::VideoProfile::NOT_APPLICABLE;
    case media_player::VideoStreamType::VideoProfile::kH264Baseline:
      return fuchsia::media::VideoProfile::H264_BASELINE;
    case media_player::VideoStreamType::VideoProfile::kH264Main:
      return fuchsia::media::VideoProfile::H264_MAIN;
    case media_player::VideoStreamType::VideoProfile::kH264Extended:
      return fuchsia::media::VideoProfile::H264_EXTENDED;
    case media_player::VideoStreamType::VideoProfile::kH264High:
      return fuchsia::media::VideoProfile::H264_HIGH;
    case media_player::VideoStreamType::VideoProfile::kH264High10:
      return fuchsia::media::VideoProfile::H264_HIGH10;
    case media_player::VideoStreamType::VideoProfile::kH264High422:
      return fuchsia::media::VideoProfile::H264_HIGH422;
    case media_player::VideoStreamType::VideoProfile::kH264High444Predictive:
      return fuchsia::media::VideoProfile::H264_HIGH444_PREDICTIVE;
    case media_player::VideoStreamType::VideoProfile::kH264ScalableBaseline:
      return fuchsia::media::VideoProfile::H264_SCALABLE_BASELINE;
    case media_player::VideoStreamType::VideoProfile::kH264ScalableHigh:
      return fuchsia::media::VideoProfile::H264_SCALABLE_HIGH;
    case media_player::VideoStreamType::VideoProfile::kH264StereoHigh:
      return fuchsia::media::VideoProfile::H264_STEREO_HIGH;
    case media_player::VideoStreamType::VideoProfile::kH264MultiviewHigh:
      return fuchsia::media::VideoProfile::H264_MULTIVIEW_HIGH;
  }

  FXL_LOG(ERROR) << "unrecognized video profile";
  abort();
}

fuchsia::media::PixelFormat TypeConverter<
    fuchsia::media::PixelFormat, media_player::VideoStreamType::PixelFormat>::
    Convert(media_player::VideoStreamType::PixelFormat pixel_format) {
  switch (pixel_format) {
    case media_player::VideoStreamType::PixelFormat::kUnknown:
      return fuchsia::media::PixelFormat::UNKNOWN;
    case media_player::VideoStreamType::PixelFormat::kI420:
      return fuchsia::media::PixelFormat::I420;
    case media_player::VideoStreamType::PixelFormat::kYv12:
      return fuchsia::media::PixelFormat::YV12;
    case media_player::VideoStreamType::PixelFormat::kYv16:
      return fuchsia::media::PixelFormat::YV16;
    case media_player::VideoStreamType::PixelFormat::kYv12A:
      return fuchsia::media::PixelFormat::YV12A;
    case media_player::VideoStreamType::PixelFormat::kYv24:
      return fuchsia::media::PixelFormat::YV24;
    case media_player::VideoStreamType::PixelFormat::kNv12:
      return fuchsia::media::PixelFormat::NV12;
    case media_player::VideoStreamType::PixelFormat::kNv21:
      return fuchsia::media::PixelFormat::NV21;
    case media_player::VideoStreamType::PixelFormat::kUyvy:
      return fuchsia::media::PixelFormat::UYVY;
    case media_player::VideoStreamType::PixelFormat::kYuy2:
      return fuchsia::media::PixelFormat::YUY2;
    case media_player::VideoStreamType::PixelFormat::kArgb:
      return fuchsia::media::PixelFormat::ARGB;
    case media_player::VideoStreamType::PixelFormat::kXrgb:
      return fuchsia::media::PixelFormat::XRGB;
    case media_player::VideoStreamType::PixelFormat::kRgb24:
      return fuchsia::media::PixelFormat::RGB24;
    case media_player::VideoStreamType::PixelFormat::kRgb32:
      return fuchsia::media::PixelFormat::RGB32;
    case media_player::VideoStreamType::PixelFormat::kMjpeg:
      return fuchsia::media::PixelFormat::MJPEG;
    case media_player::VideoStreamType::PixelFormat::kMt21:
      return fuchsia::media::PixelFormat::MT21;
  }

  FXL_LOG(ERROR) << "unrecognized pixel format";
  abort();
}

fuchsia::media::ColorSpace TypeConverter<
    fuchsia::media::ColorSpace, media_player::VideoStreamType::ColorSpace>::
    Convert(media_player::VideoStreamType::ColorSpace color_space) {
  switch (color_space) {
    case media_player::VideoStreamType::ColorSpace::kUnknown:
      return fuchsia::media::ColorSpace::UNKNOWN;
    case media_player::VideoStreamType::ColorSpace::kNotApplicable:
      return fuchsia::media::ColorSpace::NOT_APPLICABLE;
    case media_player::VideoStreamType::ColorSpace::kJpeg:
      return fuchsia::media::ColorSpace::JPEG;
    case media_player::VideoStreamType::ColorSpace::kHdRec709:
      return fuchsia::media::ColorSpace::HD_REC709;
    case media_player::VideoStreamType::ColorSpace::kSdRec601:
      return fuchsia::media::ColorSpace::SD_REC601;
  }

  FXL_LOG(ERROR) << "unrecognized color space";
  abort();
}

fuchsia::media::MediaType
TypeConverter<fuchsia::media::MediaType, media_player::StreamType>::Convert(
    const media_player::StreamType& input) {
  FXL_DCHECK(KnownEncodingsMatch());

  switch (input.medium()) {
    case media_player::StreamType::Medium::kAudio: {
      fuchsia::media::AudioMediaTypeDetails audio_details;
      audio_details.sample_format =
          To<fuchsia::media::AudioSampleFormat>(input.audio()->sample_format());
      audio_details.channels = input.audio()->channels();
      audio_details.frames_per_second = input.audio()->frames_per_second();
      fuchsia::media::MediaTypeDetails details;
      details.set_audio(std::move(audio_details));
      fuchsia::media::MediaType media_type;
      media_type.medium = fuchsia::media::MediaTypeMedium::AUDIO;
      media_type.details = std::move(details);
      media_type.encoding = input.encoding();
      media_type.encoding_parameters =
          To<fidl::VectorPtr<uint8_t>>(input.encoding_parameters());
      return media_type;
    }
    case media_player::StreamType::Medium::kVideo: {
      fuchsia::media::VideoMediaTypeDetails video_details;
      video_details.profile =
          To<fuchsia::media::VideoProfile>(input.video()->profile());
      video_details.pixel_format =
          To<fuchsia::media::PixelFormat>(input.video()->pixel_format());
      video_details.color_space =
          To<fuchsia::media::ColorSpace>(input.video()->color_space());
      video_details.width = input.video()->width();
      video_details.height = input.video()->height();
      video_details.coded_width = input.video()->coded_width();
      video_details.coded_height = input.video()->coded_height();
      video_details.pixel_aspect_ratio_width =
          input.video()->pixel_aspect_ratio_width();
      video_details.pixel_aspect_ratio_height =
          input.video()->pixel_aspect_ratio_height();
      video_details.line_stride =
          To<fidl::VectorPtr<uint32_t>>(input.video()->line_stride());
      video_details.plane_offset =
          To<fidl::VectorPtr<uint32_t>>(input.video()->plane_offset());
      fuchsia::media::MediaTypeDetails details;
      details.set_video(std::move(video_details));
      fuchsia::media::MediaType media_type;
      media_type.medium = fuchsia::media::MediaTypeMedium::VIDEO;
      media_type.details = std::move(details);
      media_type.encoding = input.encoding();
      media_type.encoding_parameters =
          To<fidl::VectorPtr<uint8_t>>(input.encoding_parameters());
      return media_type;
    }
    case media_player::StreamType::Medium::kText: {
      fuchsia::media::MediaTypeDetails details;
      details.set_text(fuchsia::media::TextMediaTypeDetails());
      fuchsia::media::MediaType media_type;
      media_type.medium = fuchsia::media::MediaTypeMedium::TEXT;
      media_type.details = std::move(details);
      media_type.encoding = input.encoding();
      media_type.encoding_parameters =
          To<fidl::VectorPtr<uint8_t>>(input.encoding_parameters());
      return media_type;
    }
    case media_player::StreamType::Medium::kSubpicture: {
      fuchsia::media::MediaTypeDetails details;
      details.set_subpicture(fuchsia::media::SubpictureMediaTypeDetails());
      fuchsia::media::MediaType media_type;
      media_type.medium = fuchsia::media::MediaTypeMedium::SUBPICTURE;
      media_type.details = std::move(details);
      media_type.encoding = input.encoding();
      media_type.encoding_parameters =
          To<fidl::VectorPtr<uint8_t>>(input.encoding_parameters());
      return media_type;
    }
  }

  FXL_LOG(ERROR) << "unrecognized medium";
  abort();
}

std::unique_ptr<media_player::StreamType> TypeConverter<
    std::unique_ptr<media_player::StreamType>,
    fuchsia::media::MediaType>::Convert(const fuchsia::media::MediaType&
                                            input) {
  FXL_DCHECK(KnownEncodingsMatch());

  switch (input.medium) {
    case fuchsia::media::MediaTypeMedium::AUDIO:
      return media_player::AudioStreamType::Create(
          input.encoding,
          To<std::unique_ptr<media_player::Bytes>>(input.encoding_parameters),
          To<media_player::AudioStreamType::SampleFormat>(
              input.details.audio().sample_format),
          input.details.audio().channels,
          input.details.audio().frames_per_second);
    case fuchsia::media::MediaTypeMedium::VIDEO:
      return media_player::VideoStreamType::Create(
          input.encoding,
          To<std::unique_ptr<media_player::Bytes>>(input.encoding_parameters),
          To<media_player::VideoStreamType::VideoProfile>(
              input.details.video().profile),
          To<media_player::VideoStreamType::PixelFormat>(
              input.details.video().pixel_format),
          To<media_player::VideoStreamType::ColorSpace>(
              input.details.video().color_space),
          input.details.video().width, input.details.video().height,
          input.details.video().coded_width, input.details.video().coded_height,
          input.details.video().pixel_aspect_ratio_width,
          input.details.video().pixel_aspect_ratio_height,
          input.details.video().line_stride,
          input.details.video().plane_offset);
    case fuchsia::media::MediaTypeMedium::TEXT:
      return media_player::TextStreamType::Create(
          input.encoding,
          To<std::unique_ptr<media_player::Bytes>>(input.encoding_parameters));
    case fuchsia::media::MediaTypeMedium::SUBPICTURE:
      return media_player::SubpictureStreamType::Create(
          input.encoding,
          To<std::unique_ptr<media_player::Bytes>>(input.encoding_parameters));
  }

  return nullptr;
}

fuchsia::mediaplayer::MediaMetadataPtr
TypeConverter<fuchsia::mediaplayer::MediaMetadataPtr,
              std::unique_ptr<media_player::Metadata>>::
    Convert(const std::unique_ptr<media_player::Metadata>& input) {
  return input == nullptr
             ? nullptr
             : fxl::To<fuchsia::mediaplayer::MediaMetadataPtr>(*input);
}

fuchsia::mediaplayer::MediaMetadataPtr TypeConverter<
    fuchsia::mediaplayer::MediaMetadataPtr,
    const media_player::Metadata*>::Convert(const media_player::Metadata*
                                                input) {
  return input == nullptr
             ? nullptr
             : fxl::To<fuchsia::mediaplayer::MediaMetadataPtr>(*input);
}

fuchsia::mediaplayer::MediaMetadataPtr TypeConverter<
    fuchsia::mediaplayer::MediaMetadataPtr,
    media_player::Metadata>::Convert(const media_player::Metadata& input) {
  auto result = fuchsia::mediaplayer::MediaMetadata::New();
  result->duration = input.duration_ns();
  result->title = input.title().empty() ? fidl::StringPtr()
                                        : fidl::StringPtr(input.title());
  result->artist = input.artist().empty() ? fidl::StringPtr()
                                          : fidl::StringPtr(input.artist());
  result->album = input.album().empty() ? fidl::StringPtr()
                                        : fidl::StringPtr(input.album());
  result->publisher = input.publisher().empty()
                          ? fidl::StringPtr()
                          : fidl::StringPtr(input.publisher());
  result->genre = input.genre().empty() ? fidl::StringPtr()
                                        : fidl::StringPtr(input.genre());
  result->composer = input.composer().empty()
                         ? fidl::StringPtr()
                         : fidl::StringPtr(input.composer());
  return result;
}

std::unique_ptr<media_player::Metadata>
TypeConverter<std::unique_ptr<media_player::Metadata>,
              fuchsia::mediaplayer::MediaMetadataPtr>::
    Convert(const fuchsia::mediaplayer::MediaMetadataPtr& input) {
  if (!input) {
    return nullptr;
  }

  return media_player::Metadata::Create(
      input->duration, input->title, input->artist, input->album,
      input->publisher, input->genre, input->composer);
}

fidl::VectorPtr<uint8_t>
TypeConverter<fidl::VectorPtr<uint8_t>, std::unique_ptr<media_player::Bytes>>::
    Convert(const std::unique_ptr<media_player::Bytes>& input) {
  if (input == nullptr) {
    return nullptr;
  }

  fidl::VectorPtr<uint8_t> array = fidl::VectorPtr<uint8_t>::New(input->size());
  std::memcpy(array->data(), input->data(), input->size());

  return array;
}

std::unique_ptr<media_player::Bytes> TypeConverter<
    std::unique_ptr<media_player::Bytes>,
    fidl::VectorPtr<uint8_t>>::Convert(const fidl::VectorPtr<uint8_t>& input) {
  if (input.is_null()) {
    return nullptr;
  }

  std::unique_ptr<media_player::Bytes> bytes =
      media_player::Bytes::Create(input->size());
  std::memcpy(bytes->data(), input->data(), input->size());

  return bytes;
}

}  // namespace fxl

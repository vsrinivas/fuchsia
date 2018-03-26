// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/fidl/fidl_type_conversions.h"

#include "garnet/bin/media/framework/types/audio_stream_type.h"
#include "garnet/bin/media/framework/types/subpicture_stream_type.h"
#include "garnet/bin/media/framework/types/text_stream_type.h"
#include "garnet/bin/media/framework/types/video_stream_type.h"

namespace fxl {

namespace {

bool KnownEncodingsMatch() {
  // clang-format off
  return !strcmp(media::StreamType::kAudioEncodingAac, media::kAudioEncodingAac) &&
         !strcmp(media::StreamType::kAudioEncodingAmrNb, media::kAudioEncodingAmrNb) &&
         !strcmp(media::StreamType::kAudioEncodingAmrWb, media::kAudioEncodingAmrWb) &&
         !strcmp(media::StreamType::kAudioEncodingFlac, media::kAudioEncodingFlac) &&
         !strcmp(media::StreamType::kAudioEncodingGsmMs, media::kAudioEncodingGsmMs) &&
         !strcmp(media::StreamType::kAudioEncodingLpcm, media::kAudioEncodingLpcm) &&
         !strcmp(media::StreamType::kAudioEncodingMp3, media::kAudioEncodingMp3) &&
         !strcmp(media::StreamType::kAudioEncodingPcmALaw, media::kAudioEncodingPcmALaw) &&
         !strcmp(media::StreamType::kAudioEncodingPcmMuLaw, media::kAudioEncodingPcmMuLaw) &&
         !strcmp(media::StreamType::kAudioEncodingVorbis, media::kAudioEncodingVorbis) &&
         !strcmp(media::StreamType::kVideoEncodingH263, media::kVideoEncodingH263) &&
         !strcmp(media::StreamType::kVideoEncodingH264, media::kVideoEncodingH264) &&
         !strcmp(media::StreamType::kVideoEncodingMpeg4, media::kVideoEncodingMpeg4) &&
         !strcmp(media::StreamType::kVideoEncodingTheora, media::kVideoEncodingTheora) &&
         !strcmp(media::StreamType::kVideoEncodingUncompressed, media::kVideoEncodingUncompressed) &&
         !strcmp(media::StreamType::kVideoEncodingVp3, media::kVideoEncodingVp3) &&
         !strcmp(media::StreamType::kVideoEncodingVp8, media::kVideoEncodingVp8) &&
         !strcmp(media::StreamType::kVideoEncodingVp9, media::kVideoEncodingVp9);
  // clang-format on
}

}  // namespace

media::Result TypeConverter<media::Result, media::MediaResult>::Convert(
    media::MediaResult media_result) {
  switch (media_result) {
    case media::MediaResult::OK:
      return media::Result::kOk;
    case media::MediaResult::INTERNAL_ERROR:
      return media::Result::kInternalError;
    case media::MediaResult::UNSUPPORTED_OPERATION:
    case media::MediaResult::NOT_IMPLEMENTED:
      return media::Result::kUnsupportedOperation;
    case media::MediaResult::INVALID_ARGUMENT:
      return media::Result::kInvalidArgument;
    case media::MediaResult::NOT_FOUND:
      return media::Result::kNotFound;
    case media::MediaResult::UNKNOWN_ERROR:
    case media::MediaResult::UNSUPPORTED_CONFIG:
    case media::MediaResult::INSUFFICIENT_RESOURCES:
    case media::MediaResult::BAD_STATE:
    case media::MediaResult::BUF_OVERFLOW:
    case media::MediaResult::FLUSHED:
    case media::MediaResult::BUSY:
    case media::MediaResult::PROTOCOL_ERROR:
    case media::MediaResult::ALREADY_EXISTS:
    case media::MediaResult::SHUTTING_DOWN:
    case media::MediaResult::CONNECTION_LOST:
      break;
  }

  return media::Result::kUnknownError;
}

media::StreamType::Medium
TypeConverter<media::StreamType::Medium, media::MediaTypeMedium>::Convert(
    media::MediaTypeMedium media_type_medium) {
  switch (media_type_medium) {
    case media::MediaTypeMedium::AUDIO:
      return media::StreamType::Medium::kAudio;
    case media::MediaTypeMedium::VIDEO:
      return media::StreamType::Medium::kVideo;
    case media::MediaTypeMedium::TEXT:
      return media::StreamType::Medium::kText;
    case media::MediaTypeMedium::SUBPICTURE:
      return media::StreamType::Medium::kSubpicture;
  }

  FXL_LOG(ERROR) << "unrecognized medium";
  abort();
}

media::AudioStreamType::SampleFormat
TypeConverter<media::AudioStreamType::SampleFormat, media::AudioSampleFormat>::
    Convert(media::AudioSampleFormat audio_sample_format) {
  switch (audio_sample_format) {
    case media::AudioSampleFormat::NONE:
      return media::AudioStreamType::SampleFormat::kNone;
    case media::AudioSampleFormat::ANY:
      return media::AudioStreamType::SampleFormat::kAny;
    case media::AudioSampleFormat::UNSIGNED_8:
      return media::AudioStreamType::SampleFormat::kUnsigned8;
    case media::AudioSampleFormat::SIGNED_16:
      return media::AudioStreamType::SampleFormat::kSigned16;
    case media::AudioSampleFormat::SIGNED_24_IN_32:
      return media::AudioStreamType::SampleFormat::kSigned24In32;
    case media::AudioSampleFormat::FLOAT:
      return media::AudioStreamType::SampleFormat::kFloat;
  }

  FXL_LOG(ERROR) << "unrecognized sample format";
  abort();
}

media::VideoStreamType::VideoProfile
TypeConverter<media::VideoStreamType::VideoProfile,
              media::VideoProfile>::Convert(media::VideoProfile video_profile) {
  switch (video_profile) {
    case media::VideoProfile::UNKNOWN:
      return media::VideoStreamType::VideoProfile::kUnknown;
    case media::VideoProfile::NOT_APPLICABLE:
      return media::VideoStreamType::VideoProfile::kNotApplicable;
    case media::VideoProfile::H264_BASELINE:
      return media::VideoStreamType::VideoProfile::kH264Baseline;
    case media::VideoProfile::H264_MAIN:
      return media::VideoStreamType::VideoProfile::kH264Main;
    case media::VideoProfile::H264_EXTENDED:
      return media::VideoStreamType::VideoProfile::kH264Extended;
    case media::VideoProfile::H264_HIGH:
      return media::VideoStreamType::VideoProfile::kH264High;
    case media::VideoProfile::H264_HIGH10:
      return media::VideoStreamType::VideoProfile::kH264High10;
    case media::VideoProfile::H264_HIGH422:
      return media::VideoStreamType::VideoProfile::kH264High422;
    case media::VideoProfile::H264_HIGH444_PREDICTIVE:
      return media::VideoStreamType::VideoProfile::kH264High444Predictive;
    case media::VideoProfile::H264_SCALABLE_BASELINE:
      return media::VideoStreamType::VideoProfile::kH264ScalableBaseline;
    case media::VideoProfile::H264_SCALABLE_HIGH:
      return media::VideoStreamType::VideoProfile::kH264ScalableHigh;
    case media::VideoProfile::H264_STEREO_HIGH:
      return media::VideoStreamType::VideoProfile::kH264StereoHigh;
    case media::VideoProfile::H264_MULTIVIEW_HIGH:
      return media::VideoStreamType::VideoProfile::kH264MultiviewHigh;
  }

  FXL_LOG(ERROR);
  abort();
}

media::VideoStreamType::PixelFormat
TypeConverter<media::VideoStreamType::PixelFormat, media::PixelFormat>::Convert(
    media::PixelFormat pixel_format) {
  switch (pixel_format) {
    case media::PixelFormat::UNKNOWN:
      return media::VideoStreamType::PixelFormat::kUnknown;
    case media::PixelFormat::I420:
      return media::VideoStreamType::PixelFormat::kI420;
    case media::PixelFormat::YV12:
      return media::VideoStreamType::PixelFormat::kYv12;
    case media::PixelFormat::YV16:
      return media::VideoStreamType::PixelFormat::kYv16;
    case media::PixelFormat::YV12A:
      return media::VideoStreamType::PixelFormat::kYv12A;
    case media::PixelFormat::YV24:
      return media::VideoStreamType::PixelFormat::kYv24;
    case media::PixelFormat::NV12:
      return media::VideoStreamType::PixelFormat::kNv12;
    case media::PixelFormat::NV21:
      return media::VideoStreamType::PixelFormat::kNv21;
    case media::PixelFormat::UYVY:
      return media::VideoStreamType::PixelFormat::kUyvy;
    case media::PixelFormat::YUY2:
      return media::VideoStreamType::PixelFormat::kYuy2;
    case media::PixelFormat::ARGB:
      return media::VideoStreamType::PixelFormat::kArgb;
    case media::PixelFormat::XRGB:
      return media::VideoStreamType::PixelFormat::kXrgb;
    case media::PixelFormat::RGB24:
      return media::VideoStreamType::PixelFormat::kRgb24;
    case media::PixelFormat::RGB32:
      return media::VideoStreamType::PixelFormat::kRgb32;
    case media::PixelFormat::MJPEG:
      return media::VideoStreamType::PixelFormat::kMjpeg;
    case media::PixelFormat::MT21:
      return media::VideoStreamType::PixelFormat::kMt21;
  }

  return media::VideoStreamType::PixelFormat::kUnknown;
}

media::VideoStreamType::ColorSpace
TypeConverter<media::VideoStreamType::ColorSpace, media::ColorSpace>::Convert(
    media::ColorSpace color_space) {
  switch (color_space) {
    case media::ColorSpace::UNKNOWN:
      return media::VideoStreamType::ColorSpace::kUnknown;
    case media::ColorSpace::NOT_APPLICABLE:
      return media::VideoStreamType::ColorSpace::kNotApplicable;
    case media::ColorSpace::JPEG:
      return media::VideoStreamType::ColorSpace::kJpeg;
    case media::ColorSpace::HD_REC709:
      return media::VideoStreamType::ColorSpace::kHdRec709;
    case media::ColorSpace::SD_REC601:
      return media::VideoStreamType::ColorSpace::kSdRec601;
  }

  return media::VideoStreamType::ColorSpace::kUnknown;
}

media::MediaTypeMedium
TypeConverter<media::MediaTypeMedium, media::StreamType::Medium>::Convert(
    media::StreamType::Medium medium) {
  switch (medium) {
    case media::StreamType::Medium::kAudio:
      return media::MediaTypeMedium::AUDIO;
    case media::StreamType::Medium::kVideo:
      return media::MediaTypeMedium::VIDEO;
    case media::StreamType::Medium::kText:
      return media::MediaTypeMedium::TEXT;
    case media::StreamType::Medium::kSubpicture:
      return media::MediaTypeMedium::SUBPICTURE;
  }

  FXL_LOG(ERROR) << "unrecognized medium";
  abort();
}

media::AudioSampleFormat
TypeConverter<media::AudioSampleFormat, media::AudioStreamType::SampleFormat>::
    Convert(media::AudioStreamType::SampleFormat sample_format) {
  switch (sample_format) {
    case media::AudioStreamType::SampleFormat::kNone:
      return media::AudioSampleFormat::NONE;
    case media::AudioStreamType::SampleFormat::kAny:
      return media::AudioSampleFormat::ANY;
    case media::AudioStreamType::SampleFormat::kUnsigned8:
      return media::AudioSampleFormat::UNSIGNED_8;
    case media::AudioStreamType::SampleFormat::kSigned16:
      return media::AudioSampleFormat::SIGNED_16;
    case media::AudioStreamType::SampleFormat::kSigned24In32:
      return media::AudioSampleFormat::SIGNED_24_IN_32;
    case media::AudioStreamType::SampleFormat::kFloat:
      return media::AudioSampleFormat::FLOAT;
  }

  FXL_LOG(ERROR) << "unrecognized sample format";
  abort();
}

media::VideoProfile
TypeConverter<media::VideoProfile, media::VideoStreamType::VideoProfile>::
    Convert(media::VideoStreamType::VideoProfile video_profile) {
  switch (video_profile) {
    case media::VideoStreamType::VideoProfile::kUnknown:
      return media::VideoProfile::UNKNOWN;
    case media::VideoStreamType::VideoProfile::kNotApplicable:
      return media::VideoProfile::NOT_APPLICABLE;
    case media::VideoStreamType::VideoProfile::kH264Baseline:
      return media::VideoProfile::H264_BASELINE;
    case media::VideoStreamType::VideoProfile::kH264Main:
      return media::VideoProfile::H264_MAIN;
    case media::VideoStreamType::VideoProfile::kH264Extended:
      return media::VideoProfile::H264_EXTENDED;
    case media::VideoStreamType::VideoProfile::kH264High:
      return media::VideoProfile::H264_HIGH;
    case media::VideoStreamType::VideoProfile::kH264High10:
      return media::VideoProfile::H264_HIGH10;
    case media::VideoStreamType::VideoProfile::kH264High422:
      return media::VideoProfile::H264_HIGH422;
    case media::VideoStreamType::VideoProfile::kH264High444Predictive:
      return media::VideoProfile::H264_HIGH444_PREDICTIVE;
    case media::VideoStreamType::VideoProfile::kH264ScalableBaseline:
      return media::VideoProfile::H264_SCALABLE_BASELINE;
    case media::VideoStreamType::VideoProfile::kH264ScalableHigh:
      return media::VideoProfile::H264_SCALABLE_HIGH;
    case media::VideoStreamType::VideoProfile::kH264StereoHigh:
      return media::VideoProfile::H264_STEREO_HIGH;
    case media::VideoStreamType::VideoProfile::kH264MultiviewHigh:
      return media::VideoProfile::H264_MULTIVIEW_HIGH;
  }

  FXL_LOG(ERROR) << "unrecognized video profile";
  abort();
}

media::PixelFormat
TypeConverter<media::PixelFormat, media::VideoStreamType::PixelFormat>::Convert(
    media::VideoStreamType::PixelFormat pixel_format) {
  switch (pixel_format) {
    case media::VideoStreamType::PixelFormat::kUnknown:
      return media::PixelFormat::UNKNOWN;
    case media::VideoStreamType::PixelFormat::kI420:
      return media::PixelFormat::I420;
    case media::VideoStreamType::PixelFormat::kYv12:
      return media::PixelFormat::YV12;
    case media::VideoStreamType::PixelFormat::kYv16:
      return media::PixelFormat::YV16;
    case media::VideoStreamType::PixelFormat::kYv12A:
      return media::PixelFormat::YV12A;
    case media::VideoStreamType::PixelFormat::kYv24:
      return media::PixelFormat::YV24;
    case media::VideoStreamType::PixelFormat::kNv12:
      return media::PixelFormat::NV12;
    case media::VideoStreamType::PixelFormat::kNv21:
      return media::PixelFormat::NV21;
    case media::VideoStreamType::PixelFormat::kUyvy:
      return media::PixelFormat::UYVY;
    case media::VideoStreamType::PixelFormat::kYuy2:
      return media::PixelFormat::YUY2;
    case media::VideoStreamType::PixelFormat::kArgb:
      return media::PixelFormat::ARGB;
    case media::VideoStreamType::PixelFormat::kXrgb:
      return media::PixelFormat::XRGB;
    case media::VideoStreamType::PixelFormat::kRgb24:
      return media::PixelFormat::RGB24;
    case media::VideoStreamType::PixelFormat::kRgb32:
      return media::PixelFormat::RGB32;
    case media::VideoStreamType::PixelFormat::kMjpeg:
      return media::PixelFormat::MJPEG;
    case media::VideoStreamType::PixelFormat::kMt21:
      return media::PixelFormat::MT21;
  }

  FXL_LOG(ERROR) << "unrecognized pixel format";
  abort();
}

media::ColorSpace
TypeConverter<media::ColorSpace, media::VideoStreamType::ColorSpace>::Convert(
    media::VideoStreamType::ColorSpace color_space) {
  switch (color_space) {
    case media::VideoStreamType::ColorSpace::kUnknown:
      return media::ColorSpace::UNKNOWN;
    case media::VideoStreamType::ColorSpace::kNotApplicable:
      return media::ColorSpace::NOT_APPLICABLE;
    case media::VideoStreamType::ColorSpace::kJpeg:
      return media::ColorSpace::JPEG;
    case media::VideoStreamType::ColorSpace::kHdRec709:
      return media::ColorSpace::HD_REC709;
    case media::VideoStreamType::ColorSpace::kSdRec601:
      return media::ColorSpace::SD_REC601;
  }

  FXL_LOG(ERROR) << "unrecognized color space";
  abort();
}

media::MediaType
TypeConverter<media::MediaType, std::unique_ptr<media::StreamType>>::Convert(
    const std::unique_ptr<media::StreamType>& input) {
  FXL_DCHECK(KnownEncodingsMatch());

  if (input == nullptr) {
    return media::MediaType();
  }

  switch (input->medium()) {
    case media::StreamType::Medium::kAudio: {
      media::AudioMediaTypeDetails audio_details;
      audio_details.sample_format =
          To<media::AudioSampleFormat>(input->audio()->sample_format());
      audio_details.channels = input->audio()->channels();
      audio_details.frames_per_second = input->audio()->frames_per_second();
      media::MediaTypeDetails details;
      details.set_audio(std::move(audio_details));
      media::MediaType media_type;
      media_type.medium = media::MediaTypeMedium::AUDIO;
      media_type.details = std::move(details);
      media_type.encoding = input->encoding();
      media_type.encoding_parameters =
          To<fidl::VectorPtr<uint8_t>>(input->encoding_parameters());
      return media_type;
    }
    case media::StreamType::Medium::kVideo: {
      media::VideoMediaTypeDetails video_details;
      video_details.profile =
          To<media::VideoProfile>(input->video()->profile());
      video_details.pixel_format =
          To<media::PixelFormat>(input->video()->pixel_format());
      video_details.color_space =
          To<media::ColorSpace>(input->video()->color_space());
      video_details.width = input->video()->width();
      video_details.height = input->video()->height();
      video_details.coded_width = input->video()->coded_width();
      video_details.coded_height = input->video()->coded_height();
      video_details.pixel_aspect_ratio_width =
          input->video()->pixel_aspect_ratio_width();
      video_details.pixel_aspect_ratio_height =
          input->video()->pixel_aspect_ratio_height();
      video_details.line_stride =
          To<fidl::VectorPtr<uint32_t>>(input->video()->line_stride());
      video_details.plane_offset =
          To<fidl::VectorPtr<uint32_t>>(input->video()->plane_offset());
      media::MediaTypeDetails details;
      details.set_video(std::move(video_details));
      media::MediaType media_type;
      media_type.medium = media::MediaTypeMedium::VIDEO;
      media_type.details = std::move(details);
      media_type.encoding = input->encoding();
      media_type.encoding_parameters =
          To<fidl::VectorPtr<uint8_t>>(input->encoding_parameters());
      return media_type;
    }
    case media::StreamType::Medium::kText: {
      media::MediaTypeDetails details;
      details.set_text(media::TextMediaTypeDetails());
      media::MediaType media_type;
      media_type.medium = media::MediaTypeMedium::TEXT;
      media_type.details = std::move(details);
      media_type.encoding = input->encoding();
      media_type.encoding_parameters =
          To<fidl::VectorPtr<uint8_t>>(input->encoding_parameters());
      return media_type;
    }
    case media::StreamType::Medium::kSubpicture: {
      media::MediaTypeDetails details;
      details.set_subpicture(media::SubpictureMediaTypeDetails());
      media::MediaType media_type;
      media_type.medium = media::MediaTypeMedium::SUBPICTURE;
      media_type.details = std::move(details);
      media_type.encoding = input->encoding();
      media_type.encoding_parameters =
          To<fidl::VectorPtr<uint8_t>>(input->encoding_parameters());
      return media_type;
    }
  }

  FXL_LOG(ERROR) << "unrecognized medium";
  abort();
}

std::unique_ptr<media::StreamType>
TypeConverter<std::unique_ptr<media::StreamType>, media::MediaType>::Convert(
    const media::MediaType& input) {
  FXL_DCHECK(KnownEncodingsMatch());

  switch (input.medium) {
    case media::MediaTypeMedium::AUDIO:
      return media::AudioStreamType::Create(
          input.encoding,
          To<std::unique_ptr<media::Bytes>>(input.encoding_parameters),
          To<media::AudioStreamType::SampleFormat>(
              input.details.audio().sample_format),
          input.details.audio().channels,
          input.details.audio().frames_per_second);
    case media::MediaTypeMedium::VIDEO:
      return media::VideoStreamType::Create(
          input.encoding,
          To<std::unique_ptr<media::Bytes>>(input.encoding_parameters),
          To<media::VideoStreamType::VideoProfile>(
              input.details.video().profile),
          To<media::VideoStreamType::PixelFormat>(
              input.details.video().pixel_format),
          To<media::VideoStreamType::ColorSpace>(
              input.details.video().color_space),
          input.details.video().width, input.details.video().height,
          input.details.video().coded_width, input.details.video().coded_height,
          input.details.video().pixel_aspect_ratio_width,
          input.details.video().pixel_aspect_ratio_height,
          input.details.video().line_stride,
          input.details.video().plane_offset);
    case media::MediaTypeMedium::TEXT:
      return media::TextStreamType::Create(
          input.encoding,
          To<std::unique_ptr<media::Bytes>>(input.encoding_parameters));
    case media::MediaTypeMedium::SUBPICTURE:
      return media::SubpictureStreamType::Create(
          input.encoding,
          To<std::unique_ptr<media::Bytes>>(input.encoding_parameters));
  }

  return nullptr;
}

media::MediaTypeSet
TypeConverter<media::MediaTypeSet, std::unique_ptr<media::StreamTypeSet>>::
    Convert(const std::unique_ptr<media::StreamTypeSet>& input) {
  FXL_DCHECK(KnownEncodingsMatch());

  if (input == nullptr) {
    return media::MediaTypeSet();
  }

  switch (input->medium()) {
    case media::StreamType::Medium::kAudio: {
      media::AudioMediaTypeSetDetails audio_details;
      audio_details.sample_format =
          To<media::AudioSampleFormat>(input->audio()->sample_format());
      audio_details.min_channels = input->audio()->channels().min;
      audio_details.max_channels = input->audio()->channels().max;
      audio_details.min_frames_per_second =
          input->audio()->frames_per_second().min;
      audio_details.max_frames_per_second =
          input->audio()->frames_per_second().max;
      media::MediaTypeSetDetails details;
      details.set_audio(std::move(audio_details));
      media::MediaTypeSet media_type_set;
      media_type_set.medium = media::MediaTypeMedium::AUDIO;
      media_type_set.details = std::move(details);
      media_type_set.encodings =
          To<fidl::VectorPtr<fidl::StringPtr>>(input->encodings());
      return media_type_set;
    }
    case media::StreamType::Medium::kVideo: {
      media::VideoMediaTypeSetDetails video_details;
      video_details.min_width = input->video()->width().min;
      video_details.max_width = input->video()->width().max;
      video_details.min_height = input->video()->height().min;
      video_details.max_height = input->video()->height().max;
      media::MediaTypeSetDetails details;
      details.set_video(std::move(video_details));
      media::MediaTypeSet media_type_set;
      media_type_set.medium = media::MediaTypeMedium::VIDEO;
      media_type_set.details = std::move(details);
      media_type_set.encodings =
          To<fidl::VectorPtr<fidl::StringPtr>>(input->encodings());
      return media_type_set;
    }
    case media::StreamType::Medium::kText: {
      media::MediaTypeSetDetails details;
      details.set_text(media::TextMediaTypeSetDetails());
      media::MediaTypeSet media_type_set;
      media_type_set.medium = media::MediaTypeMedium::TEXT;
      media_type_set.details = std::move(details);
      media_type_set.encodings =
          To<fidl::VectorPtr<fidl::StringPtr>>(input->encodings());
      return media_type_set;
    }
    case media::StreamType::Medium::kSubpicture: {
      media::MediaTypeSetDetails details;
      details.set_subpicture(media::SubpictureMediaTypeSetDetails());
      media::MediaTypeSet media_type_set;
      media_type_set.medium = media::MediaTypeMedium::SUBPICTURE;
      media_type_set.details = std::move(details);
      media_type_set.encodings =
          To<fidl::VectorPtr<fidl::StringPtr>>(input->encodings());
      return media_type_set;
    }
  }

  FXL_LOG(ERROR) << "unrecognized medium";
  abort();
}

std::unique_ptr<media::StreamTypeSet>
TypeConverter<std::unique_ptr<media::StreamTypeSet>,
              media::MediaTypeSet>::Convert(const media::MediaTypeSet& input) {
  FXL_DCHECK(KnownEncodingsMatch());

  switch (input.medium) {
    case media::MediaTypeMedium::AUDIO:
      return media::AudioStreamTypeSet::Create(
          To<std::vector<std::string>>(input.encodings),
          To<media::AudioStreamType::SampleFormat>(
              input.details.audio().sample_format),
          media::Range<uint32_t>(input.details.audio().min_channels,
                                 input.details.audio().max_channels),
          media::Range<uint32_t>(input.details.audio().min_frames_per_second,
                                 input.details.audio().max_frames_per_second));
    case media::MediaTypeMedium::VIDEO:
      return media::VideoStreamTypeSet::Create(
          To<std::vector<std::string>>(input.encodings),
          media::Range<uint32_t>(input.details.video().min_width,
                                 input.details.video().max_width),
          media::Range<uint32_t>(input.details.video().min_height,
                                 input.details.video().max_height));
    case media::MediaTypeMedium::TEXT:
      return media::TextStreamTypeSet::Create(
          To<std::vector<std::string>>(input.encodings));
    case media::MediaTypeMedium::SUBPICTURE:
      return media::SubpictureStreamTypeSet::Create(
          To<std::vector<std::string>>(input.encodings));
  }

  return nullptr;
}

media::MediaMetadataPtr
TypeConverter<media::MediaMetadataPtr, std::unique_ptr<media::Metadata>>::
    Convert(const std::unique_ptr<media::Metadata>& input) {
  if (input == nullptr) {
    return nullptr;
  }

  media::MediaMetadataPtr result = media::MediaMetadata::New();
  result->duration = input->duration_ns();
  result->title = input->title().empty() ? fidl::StringPtr()
                                         : fidl::StringPtr(input->title());
  result->artist = input->artist().empty() ? fidl::StringPtr()
                                           : fidl::StringPtr(input->artist());
  result->album = input->album().empty() ? fidl::StringPtr()
                                         : fidl::StringPtr(input->album());
  result->publisher = input->publisher().empty()
                          ? fidl::StringPtr()
                          : fidl::StringPtr(input->publisher());
  result->genre = input->genre().empty() ? fidl::StringPtr()
                                         : fidl::StringPtr(input->genre());
  result->composer = input->composer().empty()
                         ? fidl::StringPtr()
                         : fidl::StringPtr(input->composer());
  return result;
}

std::unique_ptr<media::Metadata> TypeConverter<
    std::unique_ptr<media::Metadata>,
    media::MediaMetadataPtr>::Convert(const media::MediaMetadataPtr& input) {
  if (!input) {
    return nullptr;
  }

  return media::Metadata::Create(input->duration, input->title, input->artist,
                                 input->album, input->publisher, input->genre,
                                 input->composer);
}

fidl::VectorPtr<uint8_t>
TypeConverter<fidl::VectorPtr<uint8_t>, std::unique_ptr<media::Bytes>>::Convert(
    const std::unique_ptr<media::Bytes>& input) {
  if (input == nullptr) {
    return nullptr;
  }

  fidl::VectorPtr<uint8_t> array = fidl::VectorPtr<uint8_t>::New(input->size());
  std::memcpy(array->data(), input->data(), input->size());

  return array;
}

std::unique_ptr<media::Bytes>
TypeConverter<std::unique_ptr<media::Bytes>, fidl::VectorPtr<uint8_t>>::Convert(
    const fidl::VectorPtr<uint8_t>& input) {
  if (input.is_null()) {
    return nullptr;
  }

  std::unique_ptr<media::Bytes> bytes = media::Bytes::Create(input->size());
  std::memcpy(bytes->data(), input->data(), input->size());

  return bytes;
}

}  // namespace fxl

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/fidl/fidl_type_conversions.h"

#include "garnet/bin/media/media_player/framework/types/audio_stream_type.h"
#include "garnet/bin/media/media_player/framework/types/subpicture_stream_type.h"
#include "garnet/bin/media/media_player/framework/types/text_stream_type.h"
#include "garnet/bin/media/media_player/framework/types/video_stream_type.h"

using media_player::MediaMetadata;
using media_player::MediaMetadataPtr;

namespace fxl {

namespace {

bool KnownEncodingsMatch() {
  return !strcmp(media_player::StreamType::kAudioEncodingAac,
                 media::kAudioEncodingAac) &&
         !strcmp(media_player::StreamType::kAudioEncodingAmrNb,
                 media::kAudioEncodingAmrNb) &&
         !strcmp(media_player::StreamType::kAudioEncodingAmrWb,
                 media::kAudioEncodingAmrWb) &&
         !strcmp(media_player::StreamType::kAudioEncodingFlac,
                 media::kAudioEncodingFlac) &&
         !strcmp(media_player::StreamType::kAudioEncodingGsmMs,
                 media::kAudioEncodingGsmMs) &&
         !strcmp(media_player::StreamType::kAudioEncodingLpcm,
                 media::kAudioEncodingLpcm) &&
         !strcmp(media_player::StreamType::kAudioEncodingMp3,
                 media::kAudioEncodingMp3) &&
         !strcmp(media_player::StreamType::kAudioEncodingPcmALaw,
                 media::kAudioEncodingPcmALaw) &&
         !strcmp(media_player::StreamType::kAudioEncodingPcmMuLaw,
                 media::kAudioEncodingPcmMuLaw) &&
         !strcmp(media_player::StreamType::kAudioEncodingVorbis,
                 media::kAudioEncodingVorbis) &&
         !strcmp(media_player::StreamType::kVideoEncodingH263,
                 media::kVideoEncodingH263) &&
         !strcmp(media_player::StreamType::kVideoEncodingH264,
                 media::kVideoEncodingH264) &&
         !strcmp(media_player::StreamType::kVideoEncodingMpeg4,
                 media::kVideoEncodingMpeg4) &&
         !strcmp(media_player::StreamType::kVideoEncodingTheora,
                 media::kVideoEncodingTheora) &&
         !strcmp(media_player::StreamType::kVideoEncodingUncompressed,
                 media::kVideoEncodingUncompressed) &&
         !strcmp(media_player::StreamType::kVideoEncodingVp3,
                 media::kVideoEncodingVp3) &&
         !strcmp(media_player::StreamType::kVideoEncodingVp8,
                 media::kVideoEncodingVp8) &&
         !strcmp(media_player::StreamType::kVideoEncodingVp9,
                 media::kVideoEncodingVp9);
}

}  // namespace

media_player::Result
TypeConverter<media_player::Result, media::MediaResult>::Convert(
    media::MediaResult media_result) {
  switch (media_result) {
    case media::MediaResult::OK:
      return media_player::Result::kOk;
    case media::MediaResult::INTERNAL_ERROR:
      return media_player::Result::kInternalError;
    case media::MediaResult::UNSUPPORTED_OPERATION:
    case media::MediaResult::NOT_IMPLEMENTED:
      return media_player::Result::kUnsupportedOperation;
    case media::MediaResult::INVALID_ARGUMENT:
      return media_player::Result::kInvalidArgument;
    case media::MediaResult::NOT_FOUND:
      return media_player::Result::kNotFound;
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

  return media_player::Result::kUnknownError;
}

media_player::StreamType::Medium TypeConverter<
    media_player::StreamType::Medium,
    media::MediaTypeMedium>::Convert(media::MediaTypeMedium media_type_medium) {
  switch (media_type_medium) {
    case media::MediaTypeMedium::AUDIO:
      return media_player::StreamType::Medium::kAudio;
    case media::MediaTypeMedium::VIDEO:
      return media_player::StreamType::Medium::kVideo;
    case media::MediaTypeMedium::TEXT:
      return media_player::StreamType::Medium::kText;
    case media::MediaTypeMedium::SUBPICTURE:
      return media_player::StreamType::Medium::kSubpicture;
  }

  FXL_LOG(ERROR) << "unrecognized medium";
  abort();
}

media_player::AudioStreamType::SampleFormat
TypeConverter<media_player::AudioStreamType::SampleFormat,
              media::AudioSampleFormat>::Convert(media::AudioSampleFormat
                                                     audio_sample_format) {
  switch (audio_sample_format) {
    case media::AudioSampleFormat::NONE:
      return media_player::AudioStreamType::SampleFormat::kNone;
    case media::AudioSampleFormat::ANY:
      return media_player::AudioStreamType::SampleFormat::kAny;
    case media::AudioSampleFormat::UNSIGNED_8:
      return media_player::AudioStreamType::SampleFormat::kUnsigned8;
    case media::AudioSampleFormat::SIGNED_16:
      return media_player::AudioStreamType::SampleFormat::kSigned16;
    case media::AudioSampleFormat::SIGNED_24_IN_32:
      return media_player::AudioStreamType::SampleFormat::kSigned24In32;
    case media::AudioSampleFormat::FLOAT:
      return media_player::AudioStreamType::SampleFormat::kFloat;
  }

  FXL_LOG(ERROR) << "unrecognized sample format";
  abort();
}

media_player::VideoStreamType::VideoProfile
TypeConverter<media_player::VideoStreamType::VideoProfile,
              media::VideoProfile>::Convert(media::VideoProfile video_profile) {
  switch (video_profile) {
    case media::VideoProfile::UNKNOWN:
      return media_player::VideoStreamType::VideoProfile::kUnknown;
    case media::VideoProfile::NOT_APPLICABLE:
      return media_player::VideoStreamType::VideoProfile::kNotApplicable;
    case media::VideoProfile::H264_BASELINE:
      return media_player::VideoStreamType::VideoProfile::kH264Baseline;
    case media::VideoProfile::H264_MAIN:
      return media_player::VideoStreamType::VideoProfile::kH264Main;
    case media::VideoProfile::H264_EXTENDED:
      return media_player::VideoStreamType::VideoProfile::kH264Extended;
    case media::VideoProfile::H264_HIGH:
      return media_player::VideoStreamType::VideoProfile::kH264High;
    case media::VideoProfile::H264_HIGH10:
      return media_player::VideoStreamType::VideoProfile::kH264High10;
    case media::VideoProfile::H264_HIGH422:
      return media_player::VideoStreamType::VideoProfile::kH264High422;
    case media::VideoProfile::H264_HIGH444_PREDICTIVE:
      return media_player::VideoStreamType::VideoProfile::
          kH264High444Predictive;
    case media::VideoProfile::H264_SCALABLE_BASELINE:
      return media_player::VideoStreamType::VideoProfile::kH264ScalableBaseline;
    case media::VideoProfile::H264_SCALABLE_HIGH:
      return media_player::VideoStreamType::VideoProfile::kH264ScalableHigh;
    case media::VideoProfile::H264_STEREO_HIGH:
      return media_player::VideoStreamType::VideoProfile::kH264StereoHigh;
    case media::VideoProfile::H264_MULTIVIEW_HIGH:
      return media_player::VideoStreamType::VideoProfile::kH264MultiviewHigh;
  }

  FXL_LOG(ERROR);
  abort();
}

media_player::VideoStreamType::PixelFormat
TypeConverter<media_player::VideoStreamType::PixelFormat,
              media::PixelFormat>::Convert(media::PixelFormat pixel_format) {
  switch (pixel_format) {
    case media::PixelFormat::UNKNOWN:
      return media_player::VideoStreamType::PixelFormat::kUnknown;
    case media::PixelFormat::I420:
      return media_player::VideoStreamType::PixelFormat::kI420;
    case media::PixelFormat::YV12:
      return media_player::VideoStreamType::PixelFormat::kYv12;
    case media::PixelFormat::YV16:
      return media_player::VideoStreamType::PixelFormat::kYv16;
    case media::PixelFormat::YV12A:
      return media_player::VideoStreamType::PixelFormat::kYv12A;
    case media::PixelFormat::YV24:
      return media_player::VideoStreamType::PixelFormat::kYv24;
    case media::PixelFormat::NV12:
      return media_player::VideoStreamType::PixelFormat::kNv12;
    case media::PixelFormat::NV21:
      return media_player::VideoStreamType::PixelFormat::kNv21;
    case media::PixelFormat::UYVY:
      return media_player::VideoStreamType::PixelFormat::kUyvy;
    case media::PixelFormat::YUY2:
      return media_player::VideoStreamType::PixelFormat::kYuy2;
    case media::PixelFormat::ARGB:
      return media_player::VideoStreamType::PixelFormat::kArgb;
    case media::PixelFormat::XRGB:
      return media_player::VideoStreamType::PixelFormat::kXrgb;
    case media::PixelFormat::RGB24:
      return media_player::VideoStreamType::PixelFormat::kRgb24;
    case media::PixelFormat::RGB32:
      return media_player::VideoStreamType::PixelFormat::kRgb32;
    case media::PixelFormat::MJPEG:
      return media_player::VideoStreamType::PixelFormat::kMjpeg;
    case media::PixelFormat::MT21:
      return media_player::VideoStreamType::PixelFormat::kMt21;
  }

  return media_player::VideoStreamType::PixelFormat::kUnknown;
}

media_player::VideoStreamType::ColorSpace
TypeConverter<media_player::VideoStreamType::ColorSpace,
              media::ColorSpace>::Convert(media::ColorSpace color_space) {
  switch (color_space) {
    case media::ColorSpace::UNKNOWN:
      return media_player::VideoStreamType::ColorSpace::kUnknown;
    case media::ColorSpace::NOT_APPLICABLE:
      return media_player::VideoStreamType::ColorSpace::kNotApplicable;
    case media::ColorSpace::JPEG:
      return media_player::VideoStreamType::ColorSpace::kJpeg;
    case media::ColorSpace::HD_REC709:
      return media_player::VideoStreamType::ColorSpace::kHdRec709;
    case media::ColorSpace::SD_REC601:
      return media_player::VideoStreamType::ColorSpace::kSdRec601;
  }

  return media_player::VideoStreamType::ColorSpace::kUnknown;
}

media::MediaTypeMedium
TypeConverter<media::MediaTypeMedium, media_player::StreamType::Medium>::
    Convert(media_player::StreamType::Medium medium) {
  switch (medium) {
    case media_player::StreamType::Medium::kAudio:
      return media::MediaTypeMedium::AUDIO;
    case media_player::StreamType::Medium::kVideo:
      return media::MediaTypeMedium::VIDEO;
    case media_player::StreamType::Medium::kText:
      return media::MediaTypeMedium::TEXT;
    case media_player::StreamType::Medium::kSubpicture:
      return media::MediaTypeMedium::SUBPICTURE;
  }

  FXL_LOG(ERROR) << "unrecognized medium";
  abort();
}

media::AudioSampleFormat
TypeConverter<media::AudioSampleFormat,
              media_player::AudioStreamType::SampleFormat>::
    Convert(media_player::AudioStreamType::SampleFormat sample_format) {
  switch (sample_format) {
    case media_player::AudioStreamType::SampleFormat::kNone:
      return media::AudioSampleFormat::NONE;
    case media_player::AudioStreamType::SampleFormat::kAny:
      return media::AudioSampleFormat::ANY;
    case media_player::AudioStreamType::SampleFormat::kUnsigned8:
      return media::AudioSampleFormat::UNSIGNED_8;
    case media_player::AudioStreamType::SampleFormat::kSigned16:
      return media::AudioSampleFormat::SIGNED_16;
    case media_player::AudioStreamType::SampleFormat::kSigned24In32:
      return media::AudioSampleFormat::SIGNED_24_IN_32;
    case media_player::AudioStreamType::SampleFormat::kFloat:
      return media::AudioSampleFormat::FLOAT;
  }

  FXL_LOG(ERROR) << "unrecognized sample format";
  abort();
}

media::VideoProfile TypeConverter<media::VideoProfile,
                                  media_player::VideoStreamType::VideoProfile>::
    Convert(media_player::VideoStreamType::VideoProfile video_profile) {
  switch (video_profile) {
    case media_player::VideoStreamType::VideoProfile::kUnknown:
      return media::VideoProfile::UNKNOWN;
    case media_player::VideoStreamType::VideoProfile::kNotApplicable:
      return media::VideoProfile::NOT_APPLICABLE;
    case media_player::VideoStreamType::VideoProfile::kH264Baseline:
      return media::VideoProfile::H264_BASELINE;
    case media_player::VideoStreamType::VideoProfile::kH264Main:
      return media::VideoProfile::H264_MAIN;
    case media_player::VideoStreamType::VideoProfile::kH264Extended:
      return media::VideoProfile::H264_EXTENDED;
    case media_player::VideoStreamType::VideoProfile::kH264High:
      return media::VideoProfile::H264_HIGH;
    case media_player::VideoStreamType::VideoProfile::kH264High10:
      return media::VideoProfile::H264_HIGH10;
    case media_player::VideoStreamType::VideoProfile::kH264High422:
      return media::VideoProfile::H264_HIGH422;
    case media_player::VideoStreamType::VideoProfile::kH264High444Predictive:
      return media::VideoProfile::H264_HIGH444_PREDICTIVE;
    case media_player::VideoStreamType::VideoProfile::kH264ScalableBaseline:
      return media::VideoProfile::H264_SCALABLE_BASELINE;
    case media_player::VideoStreamType::VideoProfile::kH264ScalableHigh:
      return media::VideoProfile::H264_SCALABLE_HIGH;
    case media_player::VideoStreamType::VideoProfile::kH264StereoHigh:
      return media::VideoProfile::H264_STEREO_HIGH;
    case media_player::VideoStreamType::VideoProfile::kH264MultiviewHigh:
      return media::VideoProfile::H264_MULTIVIEW_HIGH;
  }

  FXL_LOG(ERROR) << "unrecognized video profile";
  abort();
}

media::PixelFormat
TypeConverter<media::PixelFormat, media_player::VideoStreamType::PixelFormat>::
    Convert(media_player::VideoStreamType::PixelFormat pixel_format) {
  switch (pixel_format) {
    case media_player::VideoStreamType::PixelFormat::kUnknown:
      return media::PixelFormat::UNKNOWN;
    case media_player::VideoStreamType::PixelFormat::kI420:
      return media::PixelFormat::I420;
    case media_player::VideoStreamType::PixelFormat::kYv12:
      return media::PixelFormat::YV12;
    case media_player::VideoStreamType::PixelFormat::kYv16:
      return media::PixelFormat::YV16;
    case media_player::VideoStreamType::PixelFormat::kYv12A:
      return media::PixelFormat::YV12A;
    case media_player::VideoStreamType::PixelFormat::kYv24:
      return media::PixelFormat::YV24;
    case media_player::VideoStreamType::PixelFormat::kNv12:
      return media::PixelFormat::NV12;
    case media_player::VideoStreamType::PixelFormat::kNv21:
      return media::PixelFormat::NV21;
    case media_player::VideoStreamType::PixelFormat::kUyvy:
      return media::PixelFormat::UYVY;
    case media_player::VideoStreamType::PixelFormat::kYuy2:
      return media::PixelFormat::YUY2;
    case media_player::VideoStreamType::PixelFormat::kArgb:
      return media::PixelFormat::ARGB;
    case media_player::VideoStreamType::PixelFormat::kXrgb:
      return media::PixelFormat::XRGB;
    case media_player::VideoStreamType::PixelFormat::kRgb24:
      return media::PixelFormat::RGB24;
    case media_player::VideoStreamType::PixelFormat::kRgb32:
      return media::PixelFormat::RGB32;
    case media_player::VideoStreamType::PixelFormat::kMjpeg:
      return media::PixelFormat::MJPEG;
    case media_player::VideoStreamType::PixelFormat::kMt21:
      return media::PixelFormat::MT21;
  }

  FXL_LOG(ERROR) << "unrecognized pixel format";
  abort();
}

media::ColorSpace
TypeConverter<media::ColorSpace, media_player::VideoStreamType::ColorSpace>::
    Convert(media_player::VideoStreamType::ColorSpace color_space) {
  switch (color_space) {
    case media_player::VideoStreamType::ColorSpace::kUnknown:
      return media::ColorSpace::UNKNOWN;
    case media_player::VideoStreamType::ColorSpace::kNotApplicable:
      return media::ColorSpace::NOT_APPLICABLE;
    case media_player::VideoStreamType::ColorSpace::kJpeg:
      return media::ColorSpace::JPEG;
    case media_player::VideoStreamType::ColorSpace::kHdRec709:
      return media::ColorSpace::HD_REC709;
    case media_player::VideoStreamType::ColorSpace::kSdRec601:
      return media::ColorSpace::SD_REC601;
  }

  FXL_LOG(ERROR) << "unrecognized color space";
  abort();
}

media::MediaType
TypeConverter<media::MediaType, media_player::StreamType>::Convert(
    const media_player::StreamType& input) {
  FXL_DCHECK(KnownEncodingsMatch());

  switch (input.medium()) {
    case media_player::StreamType::Medium::kAudio: {
      media::AudioMediaTypeDetails audio_details;
      audio_details.sample_format =
          To<media::AudioSampleFormat>(input.audio()->sample_format());
      audio_details.channels = input.audio()->channels();
      audio_details.frames_per_second = input.audio()->frames_per_second();
      media::MediaTypeDetails details;
      details.set_audio(std::move(audio_details));
      media::MediaType media_type;
      media_type.medium = media::MediaTypeMedium::AUDIO;
      media_type.details = std::move(details);
      media_type.encoding = input.encoding();
      media_type.encoding_parameters =
          To<fidl::VectorPtr<uint8_t>>(input.encoding_parameters());
      return media_type;
    }
    case media_player::StreamType::Medium::kVideo: {
      media::VideoMediaTypeDetails video_details;
      video_details.profile = To<media::VideoProfile>(input.video()->profile());
      video_details.pixel_format =
          To<media::PixelFormat>(input.video()->pixel_format());
      video_details.color_space =
          To<media::ColorSpace>(input.video()->color_space());
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
      media::MediaTypeDetails details;
      details.set_video(std::move(video_details));
      media::MediaType media_type;
      media_type.medium = media::MediaTypeMedium::VIDEO;
      media_type.details = std::move(details);
      media_type.encoding = input.encoding();
      media_type.encoding_parameters =
          To<fidl::VectorPtr<uint8_t>>(input.encoding_parameters());
      return media_type;
    }
    case media_player::StreamType::Medium::kText: {
      media::MediaTypeDetails details;
      details.set_text(media::TextMediaTypeDetails());
      media::MediaType media_type;
      media_type.medium = media::MediaTypeMedium::TEXT;
      media_type.details = std::move(details);
      media_type.encoding = input.encoding();
      media_type.encoding_parameters =
          To<fidl::VectorPtr<uint8_t>>(input.encoding_parameters());
      return media_type;
    }
    case media_player::StreamType::Medium::kSubpicture: {
      media::MediaTypeDetails details;
      details.set_subpicture(media::SubpictureMediaTypeDetails());
      media::MediaType media_type;
      media_type.medium = media::MediaTypeMedium::SUBPICTURE;
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

std::unique_ptr<media_player::StreamType>
TypeConverter<std::unique_ptr<media_player::StreamType>,
              media::MediaType>::Convert(const media::MediaType& input) {
  FXL_DCHECK(KnownEncodingsMatch());

  switch (input.medium) {
    case media::MediaTypeMedium::AUDIO:
      return media_player::AudioStreamType::Create(
          input.encoding,
          To<std::unique_ptr<media_player::Bytes>>(input.encoding_parameters),
          To<media_player::AudioStreamType::SampleFormat>(
              input.details.audio().sample_format),
          input.details.audio().channels,
          input.details.audio().frames_per_second);
    case media::MediaTypeMedium::VIDEO:
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
    case media::MediaTypeMedium::TEXT:
      return media_player::TextStreamType::Create(
          input.encoding,
          To<std::unique_ptr<media_player::Bytes>>(input.encoding_parameters));
    case media::MediaTypeMedium::SUBPICTURE:
      return media_player::SubpictureStreamType::Create(
          input.encoding,
          To<std::unique_ptr<media_player::Bytes>>(input.encoding_parameters));
  }

  return nullptr;
}

media::MediaTypeSet
TypeConverter<media::MediaTypeSet,
              std::unique_ptr<media_player::StreamTypeSet>>::
    Convert(const std::unique_ptr<media_player::StreamTypeSet>& input) {
  FXL_DCHECK(KnownEncodingsMatch());

  if (input == nullptr) {
    return media::MediaTypeSet();
  }

  switch (input->medium()) {
    case media_player::StreamType::Medium::kAudio: {
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
    case media_player::StreamType::Medium::kVideo: {
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
    case media_player::StreamType::Medium::kText: {
      media::MediaTypeSetDetails details;
      details.set_text(media::TextMediaTypeSetDetails());
      media::MediaTypeSet media_type_set;
      media_type_set.medium = media::MediaTypeMedium::TEXT;
      media_type_set.details = std::move(details);
      media_type_set.encodings =
          To<fidl::VectorPtr<fidl::StringPtr>>(input->encodings());
      return media_type_set;
    }
    case media_player::StreamType::Medium::kSubpicture: {
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

std::unique_ptr<media_player::StreamTypeSet>
TypeConverter<std::unique_ptr<media_player::StreamTypeSet>,
              media::MediaTypeSet>::Convert(const media::MediaTypeSet& input) {
  FXL_DCHECK(KnownEncodingsMatch());

  switch (input.medium) {
    case media::MediaTypeMedium::AUDIO:
      return media_player::AudioStreamTypeSet::Create(
          To<std::vector<std::string>>(input.encodings),
          To<media_player::AudioStreamType::SampleFormat>(
              input.details.audio().sample_format),
          media_player::Range<uint32_t>(input.details.audio().min_channels,
                                        input.details.audio().max_channels),
          media_player::Range<uint32_t>(
              input.details.audio().min_frames_per_second,
              input.details.audio().max_frames_per_second));
    case media::MediaTypeMedium::VIDEO:
      return media_player::VideoStreamTypeSet::Create(
          To<std::vector<std::string>>(input.encodings),
          media_player::Range<uint32_t>(input.details.video().min_width,
                                        input.details.video().max_width),
          media_player::Range<uint32_t>(input.details.video().min_height,
                                        input.details.video().max_height));
    case media::MediaTypeMedium::TEXT:
      return media_player::TextStreamTypeSet::Create(
          To<std::vector<std::string>>(input.encodings));
    case media::MediaTypeMedium::SUBPICTURE:
      return media_player::SubpictureStreamTypeSet::Create(
          To<std::vector<std::string>>(input.encodings));
  }

  return nullptr;
}

MediaMetadataPtr
TypeConverter<MediaMetadataPtr, std::unique_ptr<media_player::Metadata>>::
    Convert(const std::unique_ptr<media_player::Metadata>& input) {
  return input == nullptr ? nullptr : fxl::To<MediaMetadataPtr>(*input);
}

MediaMetadataPtr
TypeConverter<MediaMetadataPtr, const media_player::Metadata*>::Convert(
    const media_player::Metadata* input) {
  return input == nullptr ? nullptr : fxl::To<MediaMetadataPtr>(*input);
}

MediaMetadataPtr
TypeConverter<MediaMetadataPtr, media_player::Metadata>::Convert(
    const media_player::Metadata& input) {
  auto result = media_player::MediaMetadata::New();
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
              MediaMetadataPtr>::Convert(const MediaMetadataPtr& input) {
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

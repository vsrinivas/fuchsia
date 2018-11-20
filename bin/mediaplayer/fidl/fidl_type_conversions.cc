// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/fidl/fidl_type_conversions.h"

#include "garnet/bin/mediaplayer/graph/types/audio_stream_type.h"
#include "garnet/bin/mediaplayer/graph/types/subpicture_stream_type.h"
#include "garnet/bin/mediaplayer/graph/types/text_stream_type.h"
#include "garnet/bin/mediaplayer/graph/types/video_stream_type.h"

namespace fxl {

namespace {

static const char kAudioMimeTypeLpcm[] = "audio/raw";

static const char kVideoMimeTypeUncompressed[] = "video/raw";
static const char kVideoMimeTypeH264[] = "video/h264";
// TODO(dalesat): (or dustingreen) Enable after amlogic-video VP9 decode
// is fully working.
//
//static const char kVideoMimeTypeVp9[] = "video/vp9";
// TODO(dalesat): Add MPEG2.

static inline constexpr uint32_t make_fourcc(uint8_t a, uint8_t b, uint8_t c,
                                             uint8_t d) {
  return (static_cast<uint32_t>(d) << 24) | (static_cast<uint32_t>(c) << 16) |
         (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(a);
}

static constexpr uint32_t kNv12FourCc = make_fourcc('N', 'V', '1', '2');

bool KnownEncodingsMatch() {
  return !strcmp(media_player::StreamType::kAudioEncodingAac,
                 fuchsia::media::AUDIO_ENCODING_AAC) &&
         !strcmp(media_player::StreamType::kAudioEncodingAmrNb,
                 fuchsia::media::AUDIO_ENCODING_AMRNB) &&
         !strcmp(media_player::StreamType::kAudioEncodingAmrWb,
                 fuchsia::media::AUDIO_ENCODING_AMRWB) &&
         !strcmp(media_player::StreamType::kAudioEncodingFlac,
                 fuchsia::media::AUDIO_ENCODING_FLAC) &&
         !strcmp(media_player::StreamType::kAudioEncodingGsmMs,
                 fuchsia::media::AUDIO_ENCODING_GSMMS) &&
         !strcmp(media_player::StreamType::kAudioEncodingLpcm,
                 fuchsia::media::AUDIO_ENCODING_LPCM) &&
         !strcmp(media_player::StreamType::kAudioEncodingMp3,
                 fuchsia::media::AUDIO_ENCODING_MP3) &&
         !strcmp(media_player::StreamType::kAudioEncodingPcmALaw,
                 fuchsia::media::AUDIO_ENCODING_PCMALAW) &&
         !strcmp(media_player::StreamType::kAudioEncodingPcmMuLaw,
                 fuchsia::media::AUDIO_ENCODING_PCMMULAW) &&
         !strcmp(media_player::StreamType::kAudioEncodingVorbis,
                 fuchsia::media::AUDIO_ENCODING_VORBIS) &&
         !strcmp(media_player::StreamType::kVideoEncodingH263,
                 fuchsia::media::VIDEO_ENCODING_H263) &&
         !strcmp(media_player::StreamType::kVideoEncodingH264,
                 fuchsia::media::VIDEO_ENCODING_H264) &&
         !strcmp(media_player::StreamType::kVideoEncodingMpeg4,
                 fuchsia::media::VIDEO_ENCODING_MPEG4) &&
         !strcmp(media_player::StreamType::kVideoEncodingTheora,
                 fuchsia::media::VIDEO_ENCODING_THEORA) &&
         !strcmp(media_player::StreamType::kVideoEncodingUncompressed,
                 fuchsia::media::VIDEO_ENCODING_UNCOMPRESSED) &&
         !strcmp(media_player::StreamType::kVideoEncodingVp3,
                 fuchsia::media::VIDEO_ENCODING_VP3) &&
         !strcmp(media_player::StreamType::kVideoEncodingVp8,
                 fuchsia::media::VIDEO_ENCODING_VP8) &&
         !strcmp(media_player::StreamType::kVideoEncodingVp9,
                 fuchsia::media::VIDEO_ENCODING_VP9);
}

}  // namespace

media_player::Result
TypeConverter<media_player::Result, fuchsia::mediaplayer::SeekingReaderResult>::
    Convert(fuchsia::mediaplayer::SeekingReaderResult media_result) {
  switch (media_result) {
    case fuchsia::mediaplayer::SeekingReaderResult::OK:
      return media_player::Result::kOk;
    case fuchsia::mediaplayer::SeekingReaderResult::INVALID_ARGUMENT:
      return media_player::Result::kInvalidArgument;
    case fuchsia::mediaplayer::SeekingReaderResult::NOT_FOUND:
      return media_player::Result::kNotFound;
    case fuchsia::mediaplayer::SeekingReaderResult::UNKNOWN_ERROR:
      break;
  }

  return media_player::Result::kUnknownError;
}

media_player::AudioStreamType::SampleFormat
TypeConverter<media_player::AudioStreamType::SampleFormat,
              fuchsia::media::AudioSampleFormat>::
    Convert(fuchsia::media::AudioSampleFormat audio_sample_format) {
  switch (audio_sample_format) {
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

fuchsia::media::AudioSampleFormat
TypeConverter<fuchsia::media::AudioSampleFormat,
              media_player::AudioStreamType::SampleFormat>::
    Convert(media_player::AudioStreamType::SampleFormat sample_format) {
  switch (sample_format) {
    case media_player::AudioStreamType::SampleFormat::kUnsigned8:
      return fuchsia::media::AudioSampleFormat::UNSIGNED_8;
    case media_player::AudioStreamType::SampleFormat::kSigned16:
      return fuchsia::media::AudioSampleFormat::SIGNED_16;
    case media_player::AudioStreamType::SampleFormat::kSigned24In32:
      return fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32;
    case media_player::AudioStreamType::SampleFormat::kFloat:
      return fuchsia::media::AudioSampleFormat::FLOAT;
    default:
      break;
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

fuchsia::media::StreamType
TypeConverter<fuchsia::media::StreamType, media_player::StreamType>::Convert(
    const media_player::StreamType& input) {
  FXL_DCHECK(KnownEncodingsMatch());

  switch (input.medium()) {
    case media_player::StreamType::Medium::kAudio: {
      fuchsia::media::AudioStreamType audio_stream_type;
      audio_stream_type.sample_format =
          To<fuchsia::media::AudioSampleFormat>(input.audio()->sample_format());
      audio_stream_type.channels = input.audio()->channels();
      audio_stream_type.frames_per_second = input.audio()->frames_per_second();
      fuchsia::media::MediumSpecificStreamType medium_specific_stream_type;
      medium_specific_stream_type.set_audio(std::move(audio_stream_type));
      fuchsia::media::StreamType stream_type;
      stream_type.medium_specific = std::move(medium_specific_stream_type);
      stream_type.encoding = input.encoding();
      stream_type.encoding_parameters =
          To<fidl::VectorPtr<uint8_t>>(input.encoding_parameters());
      return stream_type;
    }
    case media_player::StreamType::Medium::kVideo: {
      fuchsia::media::VideoStreamType video_stream_type;
      video_stream_type.profile =
          To<fuchsia::media::VideoProfile>(input.video()->profile());
      video_stream_type.pixel_format =
          To<fuchsia::media::PixelFormat>(input.video()->pixel_format());
      video_stream_type.color_space =
          To<fuchsia::media::ColorSpace>(input.video()->color_space());
      video_stream_type.width = input.video()->width();
      video_stream_type.height = input.video()->height();
      video_stream_type.coded_width = input.video()->coded_width();
      video_stream_type.coded_height = input.video()->coded_height();
      video_stream_type.pixel_aspect_ratio_width =
          input.video()->pixel_aspect_ratio_width();
      video_stream_type.pixel_aspect_ratio_height =
          input.video()->pixel_aspect_ratio_height();
      video_stream_type.line_stride =
          To<fidl::VectorPtr<uint32_t>>(input.video()->line_stride());
      video_stream_type.plane_offset =
          To<fidl::VectorPtr<uint32_t>>(input.video()->plane_offset());
      fuchsia::media::MediumSpecificStreamType medium_specific_stream_type;
      medium_specific_stream_type.set_video(std::move(video_stream_type));
      fuchsia::media::StreamType stream_type;
      stream_type.medium_specific = std::move(medium_specific_stream_type);
      stream_type.encoding = input.encoding();
      stream_type.encoding_parameters =
          To<fidl::VectorPtr<uint8_t>>(input.encoding_parameters());
      return stream_type;
    }
    case media_player::StreamType::Medium::kText: {
      fuchsia::media::MediumSpecificStreamType medium_specific_stream_type;
      medium_specific_stream_type.set_text(fuchsia::media::TextStreamType());
      fuchsia::media::StreamType stream_type;
      stream_type.medium_specific = std::move(medium_specific_stream_type);
      stream_type.encoding = input.encoding();
      stream_type.encoding_parameters =
          To<fidl::VectorPtr<uint8_t>>(input.encoding_parameters());
      return stream_type;
    }
    case media_player::StreamType::Medium::kSubpicture: {
      fuchsia::media::MediumSpecificStreamType medium_specific_stream_type;
      medium_specific_stream_type.set_subpicture(
          fuchsia::media::SubpictureStreamType());
      fuchsia::media::StreamType stream_type;
      stream_type.medium_specific = std::move(medium_specific_stream_type);
      stream_type.encoding = input.encoding();
      stream_type.encoding_parameters =
          To<fidl::VectorPtr<uint8_t>>(input.encoding_parameters());
      return stream_type;
    }
  }

  FXL_LOG(ERROR) << "unrecognized medium";
  abort();
}

std::unique_ptr<media_player::StreamType> TypeConverter<
    std::unique_ptr<media_player::StreamType>,
    fuchsia::media::StreamType>::Convert(const fuchsia::media::StreamType&
                                             input) {
  FXL_DCHECK(KnownEncodingsMatch());

  switch (input.medium_specific.Which()) {
    case fuchsia::media::MediumSpecificStreamType::Tag::kAudio:
      return media_player::AudioStreamType::Create(
          input.encoding,
          To<std::unique_ptr<media_player::Bytes>>(input.encoding_parameters),
          To<media_player::AudioStreamType::SampleFormat>(
              input.medium_specific.audio().sample_format),
          input.medium_specific.audio().channels,
          input.medium_specific.audio().frames_per_second);
    case fuchsia::media::MediumSpecificStreamType::Tag::kVideo:
      return media_player::VideoStreamType::Create(
          input.encoding,
          To<std::unique_ptr<media_player::Bytes>>(input.encoding_parameters),
          To<media_player::VideoStreamType::VideoProfile>(
              input.medium_specific.video().profile),
          To<media_player::VideoStreamType::PixelFormat>(
              input.medium_specific.video().pixel_format),
          To<media_player::VideoStreamType::ColorSpace>(
              input.medium_specific.video().color_space),
          input.medium_specific.video().width,
          input.medium_specific.video().height,
          input.medium_specific.video().coded_width,
          input.medium_specific.video().coded_height,
          input.medium_specific.video().pixel_aspect_ratio_width,
          input.medium_specific.video().pixel_aspect_ratio_height,
          input.medium_specific.video().line_stride,
          input.medium_specific.video().plane_offset);
    case fuchsia::media::MediumSpecificStreamType::Tag::kText:
      return media_player::TextStreamType::Create(
          input.encoding,
          To<std::unique_ptr<media_player::Bytes>>(input.encoding_parameters));
    case fuchsia::media::MediumSpecificStreamType::Tag::kSubpicture:
      return media_player::SubpictureStreamType::Create(
          input.encoding,
          To<std::unique_ptr<media_player::Bytes>>(input.encoding_parameters));
    default:
      break;
  }

  return nullptr;
}

fuchsia::mediaplayer::Metadata
TypeConverter<fuchsia::mediaplayer::Metadata, media_player::Metadata>::Convert(
    const media_player::Metadata& input) {
  fuchsia::mediaplayer::Metadata result;
  for (auto& pair : input) {
    fuchsia::mediaplayer::Property property;
    property.label = pair.first;
    property.value = pair.second;
    result.properties->push_back(property);
  }

  return result;
}

media_player::Metadata
TypeConverter<media_player::Metadata, fuchsia::mediaplayer::Metadata>::Convert(
    const fuchsia::mediaplayer::Metadata& input) {
  media_player::Metadata result(input.properties->size());
  for (auto& property : *input.properties) {
    result.emplace(property.label, property.value);
  }
  return result;
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

fuchsia::mediacodec::CodecFormatDetailsPtr TypeConverter<
    fuchsia::mediacodec::CodecFormatDetailsPtr,
    media_player::StreamType>::Convert(const media_player::StreamType& input) {
  const char* mime_type = nullptr;

  switch (input.medium()) {
    case media_player::StreamType::Medium::kAudio:
      // TODO(dalesat): Add aac-adts support.
      // We have an aac-adts decoder, but we don't have an encoding defined in
      // |media_player::StreamType| for that.
      break;
    case media_player::StreamType::Medium::kVideo:
      if (input.encoding() == media_player::StreamType::kVideoEncodingH264) {
        mime_type = kVideoMimeTypeH264;
      } else if (input.encoding() ==
                 media_player::StreamType::kVideoEncodingVp9) {
        // TODO(dalesat): (or dustingreen) Enable after amlogic-video VP9 decode
        // is fully working.
        //
        // mime_type = kVideoMimeTypeVp9;
      }
      break;
    case media_player::StreamType::Medium::kSubpicture:
    case media_player::StreamType::Medium::kText:
      break;
  }

  if (mime_type == nullptr) {
    return nullptr;
  }

  auto result = fuchsia::mediacodec::CodecFormatDetails::New();
  result->format_details_version_ordinal = 0;
  result->mime_type = mime_type;
  if (input.encoding_parameters()) {
    result->codec_oob_bytes =
        fxl::To<fidl::VectorPtr<uint8_t>>(input.encoding_parameters());
  }

  return result;
}

std::unique_ptr<media_player::StreamType>
TypeConverter<std::unique_ptr<media_player::StreamType>,
              fuchsia::mediacodec::CodecFormatDetails>::
    Convert(const fuchsia::mediacodec::CodecFormatDetails& input) {
  if (input.mime_type == kAudioMimeTypeLpcm) {
    if (!input.domain->is_audio() || !input.domain->audio().is_uncompressed() ||
        !input.domain->audio().uncompressed().is_pcm()) {
      return nullptr;
    }

    auto& format = input.domain->audio().uncompressed().pcm();
    if (format.pcm_mode != fuchsia::mediacodec::AudioPcmMode::LINEAR) {
      return nullptr;
    }

    media_player::AudioStreamType::SampleFormat sample_format;
    switch (format.bits_per_sample) {
      case 8:
        sample_format = media_player::AudioStreamType::SampleFormat::kUnsigned8;
        break;
      case 16:
        sample_format = media_player::AudioStreamType::SampleFormat::kSigned16;
        break;
      default:
        return nullptr;
    }

    return media_player::AudioStreamType::Create(
        media_player::StreamType::kAudioEncodingLpcm, nullptr, sample_format,
        format.channel_map->size(), format.frames_per_second);
  }

  if (input.mime_type == kVideoMimeTypeUncompressed) {
    if (!input.domain->is_video() || !input.domain->video().is_uncompressed()) {
      return nullptr;
    }

    auto& format = input.domain->video().uncompressed();

    if (format.fourcc != kNv12FourCc) {
      return nullptr;
    }

    std::vector<uint32_t> line_stride;
    std::vector<uint32_t> plane_offset;

    if (format.planar) {
      line_stride.push_back(format.primary_line_stride_bytes);
      line_stride.push_back(format.secondary_line_stride_bytes);

      plane_offset.push_back(format.primary_start_offset);
      plane_offset.push_back(format.secondary_start_offset);

      if (format.tertiary_start_offset != format.secondary_start_offset + 1) {
        // secondary_line_stride_bytes is resused here.
        line_stride.push_back(format.secondary_line_stride_bytes);
        plane_offset.push_back(format.tertiary_start_offset);
      }
    }

    // This doesn't care if has_pixel_aspect_ratio is true or false, as
    // pixel_aspect_ratio_width == 1, pixel_aspect_ratio_height == 1 is as good
    // a default as any, at least for now.
    return media_player::VideoStreamType::Create(
        media_player::StreamType::kVideoEncodingUncompressed, nullptr,
        media_player::VideoStreamType::VideoProfile::kUnknown,
        media_player::VideoStreamType::PixelFormat::kNv12,
        media_player::VideoStreamType::ColorSpace::kUnknown,
        format.primary_display_width_pixels,
        format.primary_display_height_pixels, format.primary_width_pixels,
        format.primary_height_pixels, format.pixel_aspect_ratio_width,
        format.pixel_aspect_ratio_height, line_stride, plane_offset);
  }

  return nullptr;
}

}  // namespace fxl

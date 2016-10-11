// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/mojo/mojo_type_conversions.h"

#include <mojo/system/result.h>

#include "apps/media/src/framework/types/audio_stream_type.h"
#include "apps/media/src/framework/types/subpicture_stream_type.h"
#include "apps/media/src/framework/types/text_stream_type.h"
#include "apps/media/src/framework/types/video_stream_type.h"

namespace mojo {
namespace media {

Result ConvertResult(MojoResult mojo_result) {
  switch (mojo_result) {
    case MOJO_RESULT_OK:
      return Result::kOk;
    case MOJO_SYSTEM_RESULT_INTERNAL:
      return Result::kInternalError;
    case MOJO_SYSTEM_RESULT_UNIMPLEMENTED:
      return Result::kUnsupportedOperation;
    case MOJO_SYSTEM_RESULT_INVALID_ARGUMENT:
      return Result::kInvalidArgument;
    case MOJO_SYSTEM_RESULT_NOT_FOUND:
      return Result::kNotFound;
    case MOJO_SYSTEM_RESULT_CANCELLED:
    case MOJO_SYSTEM_RESULT_UNKNOWN:
    case MOJO_SYSTEM_RESULT_DEADLINE_EXCEEDED:
    case MOJO_SYSTEM_RESULT_ALREADY_EXISTS:
    case MOJO_SYSTEM_RESULT_PERMISSION_DENIED:
    case MOJO_SYSTEM_RESULT_RESOURCE_EXHAUSTED:
    case MOJO_SYSTEM_RESULT_FAILED_PRECONDITION:
    case MOJO_SYSTEM_RESULT_ABORTED:
    case MOJO_SYSTEM_RESULT_OUT_OF_RANGE:
    case MOJO_SYSTEM_RESULT_UNAVAILABLE:
    case MOJO_SYSTEM_RESULT_DATA_LOSS:
    case MOJO_SYSTEM_RESULT_BUSY:
    case MOJO_SYSTEM_RESULT_SHOULD_WAIT:
    default:
      break;
  }
  return Result::kUnknownError;
}

Result Convert(MediaResult media_result) {
  switch (media_result) {
    case MediaResult::OK:
      return Result::kOk;
    case MediaResult::INTERNAL_ERROR:
      return Result::kInternalError;
    case MediaResult::UNSUPPORTED_OPERATION:
    case MediaResult::NOT_IMPLEMENTED:
      return Result::kUnsupportedOperation;
    case MediaResult::INVALID_ARGUMENT:
      return Result::kInvalidArgument;
    case MediaResult::NOT_FOUND:
      return Result::kNotFound;
    case MediaResult::UNKNOWN_ERROR:
    case MediaResult::UNSUPPORTED_CONFIG:
    case MediaResult::INSUFFICIENT_RESOURCES:
    case MediaResult::BAD_STATE:
    case MediaResult::BUF_OVERFLOW:
    case MediaResult::FLUSHED:
    case MediaResult::BUSY:
    case MediaResult::PROTOCOL_ERROR:
    case MediaResult::ALREADY_EXISTS:
    case MediaResult::SHUTTING_DOWN:
    case MediaResult::CONNECTION_LOST:
      break;
  }
  return Result::kUnknownError;
}

StreamType::Medium Convert(MediaTypeMedium media_type_medium) {
  switch (media_type_medium) {
    case MediaTypeMedium::AUDIO:
      return StreamType::Medium::kAudio;
    case MediaTypeMedium::VIDEO:
      return StreamType::Medium::kVideo;
    case MediaTypeMedium::TEXT:
      return StreamType::Medium::kText;
    case MediaTypeMedium::SUBPICTURE:
      return StreamType::Medium::kSubpicture;
  }
  FTL_LOG(ERROR) << "unrecognized medium";
  abort();
}

AudioStreamType::SampleFormat Convert(AudioSampleFormat audio_sample_format) {
  switch (audio_sample_format) {
    case AudioSampleFormat::ANY:
      return AudioStreamType::SampleFormat::kAny;
    case AudioSampleFormat::UNSIGNED_8:
      return AudioStreamType::SampleFormat::kUnsigned8;
    case AudioSampleFormat::SIGNED_16:
      return AudioStreamType::SampleFormat::kSigned16;
    case AudioSampleFormat::SIGNED_24_IN_32:
      return AudioStreamType::SampleFormat::kSigned24In32;
    case AudioSampleFormat::FLOAT:
      return AudioStreamType::SampleFormat::kFloat;
  }
  FTL_LOG(ERROR) << "unrecognized sample format";
  abort();
}

VideoStreamType::VideoProfile Convert(VideoProfile video_profile) {
  switch (video_profile) {
    case VideoProfile::UNKNOWN:
      return VideoStreamType::VideoProfile::kUnknown;
    case VideoProfile::NOT_APPLICABLE:
      return VideoStreamType::VideoProfile::kNotApplicable;
    case VideoProfile::H264_BASELINE:
      return VideoStreamType::VideoProfile::kH264Baseline;
    case VideoProfile::H264_MAIN:
      return VideoStreamType::VideoProfile::kH264Main;
    case VideoProfile::H264_EXTENDED:
      return VideoStreamType::VideoProfile::kH264Extended;
    case VideoProfile::H264_HIGH:
      return VideoStreamType::VideoProfile::kH264High;
    case VideoProfile::H264_HIGH10:
      return VideoStreamType::VideoProfile::kH264High10;
    case VideoProfile::H264_HIGH422:
      return VideoStreamType::VideoProfile::kH264High422;
    case VideoProfile::H264_HIGH444_PREDICTIVE:
      return VideoStreamType::VideoProfile::kH264High444Predictive;
    case VideoProfile::H264_SCALABLE_BASELINE:
      return VideoStreamType::VideoProfile::kH264ScalableBaseline;
    case VideoProfile::H264_SCALABLE_HIGH:
      return VideoStreamType::VideoProfile::kH264ScalableHigh;
    case VideoProfile::H264_STEREO_HIGH:
      return VideoStreamType::VideoProfile::kH264StereoHigh;
    case VideoProfile::H264_MULTIVIEW_HIGH:
      return VideoStreamType::VideoProfile::kH264MultiviewHigh;
  }
  FTL_LOG(ERROR);
  abort();
}

VideoStreamType::PixelFormat Convert(PixelFormat pixel_format) {
  switch (pixel_format) {
    case PixelFormat::UNKNOWN:
      return VideoStreamType::PixelFormat::kUnknown;
    case PixelFormat::I420:
      return VideoStreamType::PixelFormat::kI420;
    case PixelFormat::YV12:
      return VideoStreamType::PixelFormat::kYv12;
    case PixelFormat::YV16:
      return VideoStreamType::PixelFormat::kYv16;
    case PixelFormat::YV12A:
      return VideoStreamType::PixelFormat::kYv12A;
    case PixelFormat::YV24:
      return VideoStreamType::PixelFormat::kYv24;
    case PixelFormat::NV12:
      return VideoStreamType::PixelFormat::kNv12;
    case PixelFormat::NV21:
      return VideoStreamType::PixelFormat::kNv21;
    case PixelFormat::UYVY:
      return VideoStreamType::PixelFormat::kUyvy;
    case PixelFormat::YUY2:
      return VideoStreamType::PixelFormat::kYuy2;
    case PixelFormat::ARGB:
      return VideoStreamType::PixelFormat::kArgb;
    case PixelFormat::XRGB:
      return VideoStreamType::PixelFormat::kXrgb;
    case PixelFormat::RGB24:
      return VideoStreamType::PixelFormat::kRgb24;
    case PixelFormat::RGB32:
      return VideoStreamType::PixelFormat::kRgb32;
    case PixelFormat::MJPEG:
      return VideoStreamType::PixelFormat::kMjpeg;
    case PixelFormat::MT21:
      return VideoStreamType::PixelFormat::kMt21;
  }
  return VideoStreamType::PixelFormat::kUnknown;
}

VideoStreamType::ColorSpace Convert(ColorSpace color_space) {
  switch (color_space) {
    case ColorSpace::UNKNOWN:
      return VideoStreamType::ColorSpace::kUnknown;
    case ColorSpace::NOT_APPLICABLE:
      return VideoStreamType::ColorSpace::kNotApplicable;
    case ColorSpace::JPEG:
      return VideoStreamType::ColorSpace::kJpeg;
    case ColorSpace::HD_REC709:
      return VideoStreamType::ColorSpace::kHdRec709;
    case ColorSpace::SD_REC601:
      return VideoStreamType::ColorSpace::kSdRec601;
  }
  return VideoStreamType::ColorSpace::kUnknown;
}

MediaTypeMedium Convert(StreamType::Medium medium) {
  switch (medium) {
    case StreamType::Medium::kAudio:
      return MediaTypeMedium::AUDIO;
    case StreamType::Medium::kVideo:
      return MediaTypeMedium::VIDEO;
    case StreamType::Medium::kText:
      return MediaTypeMedium::TEXT;
    case StreamType::Medium::kSubpicture:
      return MediaTypeMedium::SUBPICTURE;
  }

  FTL_LOG(ERROR) << "unrecognized medium";
  abort();
}

AudioSampleFormat Convert(AudioStreamType::SampleFormat sample_format) {
  switch (sample_format) {
    case AudioStreamType::SampleFormat::kAny:
      return AudioSampleFormat::ANY;
    case AudioStreamType::SampleFormat::kUnsigned8:
      return AudioSampleFormat::UNSIGNED_8;
    case AudioStreamType::SampleFormat::kSigned16:
      return AudioSampleFormat::SIGNED_16;
    case AudioStreamType::SampleFormat::kSigned24In32:
      return AudioSampleFormat::SIGNED_24_IN_32;
    case AudioStreamType::SampleFormat::kFloat:
      return AudioSampleFormat::FLOAT;
  }

  FTL_LOG(ERROR) << "unrecognized sample format";
  abort();
}

VideoProfile Convert(VideoStreamType::VideoProfile video_profile) {
  switch (video_profile) {
    case VideoStreamType::VideoProfile::kUnknown:
      return VideoProfile::UNKNOWN;
    case VideoStreamType::VideoProfile::kNotApplicable:
      return VideoProfile::NOT_APPLICABLE;
    case VideoStreamType::VideoProfile::kH264Baseline:
      return VideoProfile::H264_BASELINE;
    case VideoStreamType::VideoProfile::kH264Main:
      return VideoProfile::H264_MAIN;
    case VideoStreamType::VideoProfile::kH264Extended:
      return VideoProfile::H264_EXTENDED;
    case VideoStreamType::VideoProfile::kH264High:
      return VideoProfile::H264_HIGH;
    case VideoStreamType::VideoProfile::kH264High10:
      return VideoProfile::H264_HIGH10;
    case VideoStreamType::VideoProfile::kH264High422:
      return VideoProfile::H264_HIGH422;
    case VideoStreamType::VideoProfile::kH264High444Predictive:
      return VideoProfile::H264_HIGH444_PREDICTIVE;
    case VideoStreamType::VideoProfile::kH264ScalableBaseline:
      return VideoProfile::H264_SCALABLE_BASELINE;
    case VideoStreamType::VideoProfile::kH264ScalableHigh:
      return VideoProfile::H264_SCALABLE_HIGH;
    case VideoStreamType::VideoProfile::kH264StereoHigh:
      return VideoProfile::H264_STEREO_HIGH;
    case VideoStreamType::VideoProfile::kH264MultiviewHigh:
      return VideoProfile::H264_MULTIVIEW_HIGH;
  }

  FTL_LOG(ERROR) << "unrecognized video profile";
  abort();
}

PixelFormat Convert(VideoStreamType::PixelFormat pixel_format) {
  switch (pixel_format) {
    case VideoStreamType::PixelFormat::kUnknown:
      return PixelFormat::UNKNOWN;
    case VideoStreamType::PixelFormat::kI420:
      return PixelFormat::I420;
    case VideoStreamType::PixelFormat::kYv12:
      return PixelFormat::YV12;
    case VideoStreamType::PixelFormat::kYv16:
      return PixelFormat::YV16;
    case VideoStreamType::PixelFormat::kYv12A:
      return PixelFormat::YV12A;
    case VideoStreamType::PixelFormat::kYv24:
      return PixelFormat::YV24;
    case VideoStreamType::PixelFormat::kNv12:
      return PixelFormat::NV12;
    case VideoStreamType::PixelFormat::kNv21:
      return PixelFormat::NV21;
    case VideoStreamType::PixelFormat::kUyvy:
      return PixelFormat::UYVY;
    case VideoStreamType::PixelFormat::kYuy2:
      return PixelFormat::YUY2;
    case VideoStreamType::PixelFormat::kArgb:
      return PixelFormat::ARGB;
    case VideoStreamType::PixelFormat::kXrgb:
      return PixelFormat::XRGB;
    case VideoStreamType::PixelFormat::kRgb24:
      return PixelFormat::RGB24;
    case VideoStreamType::PixelFormat::kRgb32:
      return PixelFormat::RGB32;
    case VideoStreamType::PixelFormat::kMjpeg:
      return PixelFormat::MJPEG;
    case VideoStreamType::PixelFormat::kMt21:
      return PixelFormat::MT21;
  }

  FTL_LOG(ERROR) << "unrecognized pixel format";
  abort();
}

ColorSpace Convert(VideoStreamType::ColorSpace color_space) {
  switch (color_space) {
    case VideoStreamType::ColorSpace::kUnknown:
      return ColorSpace::UNKNOWN;
    case VideoStreamType::ColorSpace::kNotApplicable:
      return ColorSpace::NOT_APPLICABLE;
    case VideoStreamType::ColorSpace::kJpeg:
      return ColorSpace::JPEG;
    case VideoStreamType::ColorSpace::kHdRec709:
      return ColorSpace::HD_REC709;
    case VideoStreamType::ColorSpace::kSdRec601:
      return ColorSpace::SD_REC601;
  }

  FTL_LOG(ERROR) << "unrecognized color space";
  abort();
}

}  // namespace media

namespace {

bool KnownEncodingsMatch() {
  return media::StreamType::kAudioEncodingAac ==
             media::MediaType::kAudioEncodingAac &&
         media::StreamType::kAudioEncodingAmrNb ==
             media::MediaType::kAudioEncodingAmrNb &&
         media::StreamType::kAudioEncodingAmrWb ==
             media::MediaType::kAudioEncodingAmrWb &&
         media::StreamType::kAudioEncodingFlac ==
             media::MediaType::kAudioEncodingFlac &&
         media::StreamType::kAudioEncodingGsmMs ==
             media::MediaType::kAudioEncodingGsmMs &&
         media::StreamType::kAudioEncodingLpcm ==
             media::MediaType::kAudioEncodingLpcm &&
         media::StreamType::kAudioEncodingMp3 ==
             media::MediaType::kAudioEncodingMp3 &&
         media::StreamType::kAudioEncodingPcmALaw ==
             media::MediaType::kAudioEncodingPcmALaw &&
         media::StreamType::kAudioEncodingPcmMuLaw ==
             media::MediaType::kAudioEncodingPcmMuLaw &&
         media::StreamType::kAudioEncodingVorbis ==
             media::MediaType::kAudioEncodingVorbis &&
         media::StreamType::kVideoEncodingH263 ==
             media::MediaType::kVideoEncodingH263 &&
         media::StreamType::kVideoEncodingH264 ==
             media::MediaType::kVideoEncodingH264 &&
         media::StreamType::kVideoEncodingMpeg4 ==
             media::MediaType::kVideoEncodingMpeg4 &&
         media::StreamType::kVideoEncodingTheora ==
             media::MediaType::kVideoEncodingTheora &&
         media::StreamType::kVideoEncodingUncompressed ==
             media::MediaType::kVideoEncodingUncompressed &&
         media::StreamType::kVideoEncodingVp3 ==
             media::MediaType::kVideoEncodingVp3 &&
         media::StreamType::kVideoEncodingVp8 ==
             media::MediaType::kVideoEncodingVp8;
}

}  // namespace

media::MediaTypePtr
TypeConverter<media::MediaTypePtr, std::unique_ptr<media::StreamType>>::Convert(
    const std::unique_ptr<media::StreamType>& input) {
  FTL_DCHECK(KnownEncodingsMatch());

  if (input == nullptr) {
    return nullptr;
  }

  switch (input->medium()) {
    case media::StreamType::Medium::kAudio: {
      media::AudioMediaTypeDetailsPtr audio_details =
          media::AudioMediaTypeDetails::New();
      audio_details->sample_format =
          media::Convert(input->audio()->sample_format());
      audio_details->channels = input->audio()->channels();
      audio_details->frames_per_second = input->audio()->frames_per_second();
      media::MediaTypeDetailsPtr details = media::MediaTypeDetails::New();
      details->set_audio(audio_details.Pass());
      media::MediaTypePtr media_type = media::MediaType::New();
      media_type->medium = media::MediaTypeMedium::AUDIO;
      media_type->details = details.Pass();
      media_type->encoding = input->encoding();
      media_type->encoding_parameters =
          Array<uint8_t>::From(input->encoding_parameters());
      return media_type;
    }
    case media::StreamType::Medium::kVideo: {
      media::VideoMediaTypeDetailsPtr video_details =
          media::VideoMediaTypeDetails::New();
      video_details->profile = media::Convert(input->video()->profile());
      video_details->pixel_format =
          media::Convert(input->video()->pixel_format());
      video_details->color_space =
          media::Convert(input->video()->color_space());
      video_details->width = input->video()->width();
      video_details->height = input->video()->height();
      video_details->coded_width = input->video()->coded_width();
      video_details->coded_height = input->video()->coded_height();
      media::MediaTypeDetailsPtr details = media::MediaTypeDetails::New();
      details->set_video(video_details.Pass());
      media::MediaTypePtr media_type = media::MediaType::New();
      media_type->medium = media::MediaTypeMedium::VIDEO;
      media_type->details = details.Pass();
      media_type->encoding = input->encoding();
      media_type->encoding_parameters =
          Array<uint8_t>::From(input->encoding_parameters());
      return media_type;
    }
    case media::StreamType::Medium::kText: {
      media::MediaTypeDetailsPtr details = media::MediaTypeDetails::New();
      details->set_text(media::TextMediaTypeDetails::New());
      media::MediaTypePtr media_type = media::MediaType::New();
      media_type->medium = media::MediaTypeMedium::TEXT;
      media_type->details = details.Pass();
      media_type->encoding = input->encoding();
      media_type->encoding_parameters =
          Array<uint8_t>::From(input->encoding_parameters());
      return media_type;
    }
    case media::StreamType::Medium::kSubpicture: {
      media::MediaTypeDetailsPtr details = media::MediaTypeDetails::New();
      details->set_subpicture(media::SubpictureMediaTypeDetails::New());
      media::MediaTypePtr media_type = media::MediaType::New();
      media_type->medium = media::MediaTypeMedium::SUBPICTURE;
      media_type->details = details.Pass();
      media_type->encoding = input->encoding();
      media_type->encoding_parameters =
          Array<uint8_t>::From(input->encoding_parameters());
      return media_type;
    }
  }

  FTL_LOG(ERROR) << "unrecognized medium";
  abort();
}

std::unique_ptr<media::StreamType>
TypeConverter<std::unique_ptr<media::StreamType>, media::MediaTypePtr>::Convert(
    const media::MediaTypePtr& input) {
  FTL_DCHECK(KnownEncodingsMatch());

  if (!input) {
    return nullptr;
  }

  switch (input->medium) {
    case media::MediaTypeMedium::AUDIO:
      return media::AudioStreamType::Create(
          input->encoding,
          input->encoding_parameters.To<std::unique_ptr<media::Bytes>>(),
          media::Convert(input->details->get_audio()->sample_format),
          input->details->get_audio()->channels,
          input->details->get_audio()->frames_per_second);
    case media::MediaTypeMedium::VIDEO:
      return media::VideoStreamType::Create(
          input->encoding,
          input->encoding_parameters.To<std::unique_ptr<media::Bytes>>(),
          media::Convert(input->details->get_video()->profile),
          media::Convert(input->details->get_video()->pixel_format),
          media::Convert(input->details->get_video()->color_space),
          input->details->get_video()->width,
          input->details->get_video()->height,
          input->details->get_video()->coded_width,
          input->details->get_video()->coded_height);
    case media::MediaTypeMedium::TEXT:
      return media::TextStreamType::Create(
          input->encoding,
          input->encoding_parameters.To<std::unique_ptr<media::Bytes>>());
    case media::MediaTypeMedium::SUBPICTURE:
      return media::SubpictureStreamType::Create(
          input->encoding,
          input->encoding_parameters.To<std::unique_ptr<media::Bytes>>());
  }
  return nullptr;
}

media::MediaTypeSetPtr
TypeConverter<media::MediaTypeSetPtr, std::unique_ptr<media::StreamTypeSet>>::
    Convert(const std::unique_ptr<media::StreamTypeSet>& input) {
  FTL_DCHECK(KnownEncodingsMatch());

  if (input == nullptr) {
    return nullptr;
  }

  switch (input->medium()) {
    case media::StreamType::Medium::kAudio: {
      media::AudioMediaTypeSetDetailsPtr audio_details =
          media::AudioMediaTypeSetDetails::New();
      audio_details->sample_format =
          media::Convert(input->audio()->sample_format());
      audio_details->min_channels = input->audio()->channels().min;
      audio_details->max_channels = input->audio()->channels().max;
      audio_details->min_frames_per_second =
          input->audio()->frames_per_second().min;
      audio_details->max_frames_per_second =
          input->audio()->frames_per_second().max;
      media::MediaTypeSetDetailsPtr details = media::MediaTypeSetDetails::New();
      details->set_audio(audio_details.Pass());
      media::MediaTypeSetPtr media_type_set = media::MediaTypeSet::New();
      media_type_set->medium = media::MediaTypeMedium::AUDIO;
      media_type_set->details = details.Pass();
      media_type_set->encodings = Array<String>::From(input->encodings());
      return media_type_set;
    }
    case media::StreamType::Medium::kVideo: {
      media::VideoMediaTypeSetDetailsPtr video_details =
          media::VideoMediaTypeSetDetails::New();
      video_details->min_width = input->video()->width().min;
      video_details->max_width = input->video()->width().max;
      video_details->min_height = input->video()->height().min;
      video_details->max_height = input->video()->height().max;
      media::MediaTypeSetDetailsPtr details = media::MediaTypeSetDetails::New();
      details->set_video(video_details.Pass());
      media::MediaTypeSetPtr media_type_set = media::MediaTypeSet::New();
      media_type_set->medium = media::MediaTypeMedium::VIDEO;
      media_type_set->details = details.Pass();
      media_type_set->encodings = Array<String>::From(input->encodings());
      return media_type_set;
    }
    case media::StreamType::Medium::kText: {
      media::MediaTypeSetDetailsPtr details = media::MediaTypeSetDetails::New();
      details->set_text(media::TextMediaTypeSetDetails::New());
      media::MediaTypeSetPtr media_type_set = media::MediaTypeSet::New();
      media_type_set->medium = media::MediaTypeMedium::TEXT;
      media_type_set->details = details.Pass();
      media_type_set->encodings = Array<String>::From(input->encodings());
      return media_type_set;
    }
    case media::StreamType::Medium::kSubpicture: {
      media::MediaTypeSetDetailsPtr details = media::MediaTypeSetDetails::New();
      details->set_subpicture(media::SubpictureMediaTypeSetDetails::New());
      media::MediaTypeSetPtr media_type_set = media::MediaTypeSet::New();
      media_type_set->medium = media::MediaTypeMedium::SUBPICTURE;
      media_type_set->details = details.Pass();
      media_type_set->encodings = Array<String>::From(input->encodings());
      return media_type_set;
    }
  }

  FTL_LOG(ERROR) << "unrecognized medium";
  abort();
}

std::unique_ptr<media::StreamTypeSet> TypeConverter<
    std::unique_ptr<media::StreamTypeSet>,
    media::MediaTypeSetPtr>::Convert(const media::MediaTypeSetPtr& input) {
  FTL_DCHECK(KnownEncodingsMatch());

  if (!input) {
    return nullptr;
  }

  switch (input->medium) {
    case media::MediaTypeMedium::AUDIO:
      return media::AudioStreamTypeSet::Create(
          input->encodings.To<std::vector<std::string>>(),
          media::Convert(input->details->get_audio()->sample_format),
          media::Range<uint32_t>(input->details->get_audio()->min_channels,
                                 input->details->get_audio()->max_channels),
          media::Range<uint32_t>(
              input->details->get_audio()->min_frames_per_second,
              input->details->get_audio()->max_frames_per_second));
    case media::MediaTypeMedium::VIDEO:
      return media::VideoStreamTypeSet::Create(
          input->encodings.To<std::vector<std::string>>(),
          media::Range<uint32_t>(input->details->get_video()->min_width,
                                 input->details->get_video()->max_width),
          media::Range<uint32_t>(input->details->get_video()->min_height,
                                 input->details->get_video()->max_height));
    case media::MediaTypeMedium::TEXT:
      return media::TextStreamTypeSet::Create(
          input->encodings.To<std::vector<std::string>>());
    case media::MediaTypeMedium::SUBPICTURE:
      return media::SubpictureStreamTypeSet::Create(
          input->encodings.To<std::vector<std::string>>());
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
  result->title = input->title().empty() ? String() : String(input->title());
  result->artist = input->artist().empty() ? String() : String(input->artist());
  result->album = input->album().empty() ? String() : String(input->album());
  result->publisher =
      input->publisher().empty() ? String() : String(input->publisher());
  result->genre = input->genre().empty() ? String() : String(input->genre());
  result->composer =
      input->composer().empty() ? String() : String(input->composer());
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

Array<media::MediaTypePtr> TypeConverter<
    Array<media::MediaTypePtr>,
    std::unique_ptr<std::vector<std::unique_ptr<media::StreamType>>>>::
    Convert(
        const std::unique_ptr<std::vector<std::unique_ptr<media::StreamType>>>&
            input) {
  if (input == nullptr) {
    return nullptr;
  }

  Array<media::MediaTypePtr> result =
      Array<media::MediaTypePtr>::New(input->size());
  for (const std::unique_ptr<media::StreamType>& stream_type : *input) {
    result.push_back(media::MediaType::From(stream_type));
  }
  return result;
}

std::unique_ptr<std::vector<std::unique_ptr<media::StreamType>>> TypeConverter<
    std::unique_ptr<std::vector<std::unique_ptr<media::StreamType>>>,
    Array<media::MediaTypePtr>>::Convert(const Array<media::MediaTypePtr>&
                                             input) {
  if (!input) {
    return nullptr;
  }

  std::unique_ptr<std::vector<std::unique_ptr<media::StreamType>>> result =
      std::unique_ptr<std::vector<std::unique_ptr<media::StreamType>>>(
          new std::vector<std::unique_ptr<media::StreamType>>(input.size()));
  for (size_t i = 0; i < input.size(); i++) {
    (*result)[i] = input[i].To<std::unique_ptr<media::StreamType>>();
  }
  return result;
}

Array<media::MediaTypeSetPtr> TypeConverter<
    Array<media::MediaTypeSetPtr>,
    std::unique_ptr<std::vector<std::unique_ptr<media::StreamTypeSet>>>>::
    Convert(const std::unique_ptr<
            std::vector<std::unique_ptr<media::StreamTypeSet>>>& input) {
  if (input == nullptr) {
    return nullptr;
  }

  Array<media::MediaTypeSetPtr> result =
      Array<media::MediaTypeSetPtr>::New(input->size());
  for (const std::unique_ptr<media::StreamTypeSet>& stream_type_set : *input) {
    result.push_back(media::MediaTypeSet::From(stream_type_set));
  }
  return result;
}

std::unique_ptr<std::vector<std::unique_ptr<media::StreamTypeSet>>>
TypeConverter<
    std::unique_ptr<std::vector<std::unique_ptr<media::StreamTypeSet>>>,
    Array<media::MediaTypeSetPtr>>::Convert(const Array<media::MediaTypeSetPtr>&
                                                input) {
  if (!input) {
    return nullptr;
  }

  std::vector<std::unique_ptr<media::StreamTypeSet>>* result =
      new std::vector<std::unique_ptr<media::StreamTypeSet>>(input.size());
  for (size_t i = 0; i < input.size(); i++) {
    (*result)[i] = input[i].To<std::unique_ptr<media::StreamTypeSet>>();
  }
  return std::unique_ptr<std::vector<std::unique_ptr<media::StreamTypeSet>>>(
      result);
}

Array<uint8_t>
TypeConverter<Array<uint8_t>, std::unique_ptr<media::Bytes>>::Convert(
    const std::unique_ptr<media::Bytes>& input) {
  if (input == nullptr) {
    return nullptr;
  }

  Array<uint8_t> array = Array<uint8_t>::New(input->size());
  std::memcpy(array.data(), input->data(), input->size());

  return array;
}

std::unique_ptr<media::Bytes>
TypeConverter<std::unique_ptr<media::Bytes>, Array<uint8_t>>::Convert(
    const Array<uint8_t>& input) {
  if (input.is_null()) {
    return nullptr;
  }

  std::unique_ptr<media::Bytes> bytes = media::Bytes::Create(input.size());
  std::memcpy(bytes->data(), input.data(), input.size());

  return bytes;
}

}  // namespace mojo

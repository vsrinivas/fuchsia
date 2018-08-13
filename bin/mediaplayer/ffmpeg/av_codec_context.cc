// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/ffmpeg/av_codec_context.h"

#include "garnet/bin/mediaplayer/ffmpeg/ffmpeg_init.h"
#include "garnet/bin/mediaplayer/ffmpeg/ffmpeg_video_frame_layout.h"
#include "garnet/bin/mediaplayer/framework/types/audio_stream_type.h"
#include "garnet/bin/mediaplayer/framework/types/subpicture_stream_type.h"
#include "garnet/bin/mediaplayer/framework/types/text_stream_type.h"
#include "garnet/bin/mediaplayer/framework/types/video_stream_type.h"
#include "lib/fxl/logging.h"
extern "C" {
#include "libavformat/avformat.h"
}

namespace media_player {

namespace {

// Converts an AVSampleFormat into an AudioStreamType::SampleFormat.
AudioStreamType::SampleFormat Convert(AVSampleFormat av_sample_format) {
  switch (av_sample_format) {
    case AV_SAMPLE_FMT_NONE:
      return AudioStreamType::SampleFormat::kNone;
    case AV_SAMPLE_FMT_U8:
    case AV_SAMPLE_FMT_U8P:
      return AudioStreamType::SampleFormat::kUnsigned8;
    case AV_SAMPLE_FMT_S16:
    case AV_SAMPLE_FMT_S16P:
      return AudioStreamType::SampleFormat::kSigned16;
    case AV_SAMPLE_FMT_S32:
    case AV_SAMPLE_FMT_S32P:
      return AudioStreamType::SampleFormat::kSigned24In32;
    case AV_SAMPLE_FMT_FLT:
    case AV_SAMPLE_FMT_FLTP:
      return AudioStreamType::SampleFormat::kFloat;
    case AV_SAMPLE_FMT_DBL:
    case AV_SAMPLE_FMT_DBLP:
    case AV_SAMPLE_FMT_NB:
    default:
      FXL_LOG(ERROR) << "unsupported av_sample_format " << av_sample_format;
      abort();
  }
}

// Copies a buffer from Bytes into context->extradata. The result is malloc'ed
// and must be freed.
void ExtraDataFromBytes(const Bytes& bytes, const AvCodecContextPtr& context) {
  size_t byte_count = bytes.size();
  uint8_t* copy = reinterpret_cast<uint8_t*>(malloc(byte_count));
  std::memcpy(copy, bytes.data(), byte_count);
  context->extradata = copy;
  context->extradata_size = byte_count;
}

// Gets the encoding from a codec_id.
const char* EncodingFromCodecId(AVCodecID from) {
  switch (from) {
    case AV_CODEC_ID_AAC:
      return StreamType::kAudioEncodingAac;
    case AV_CODEC_ID_AMR_NB:
      return StreamType::kAudioEncodingAmrNb;
    case AV_CODEC_ID_AMR_WB:
      return StreamType::kAudioEncodingAmrWb;
    case AV_CODEC_ID_FLAC:
      return StreamType::kAudioEncodingFlac;
    case AV_CODEC_ID_GSM_MS:
      return StreamType::kAudioEncodingGsmMs;
    case AV_CODEC_ID_MP3:
      return StreamType::kAudioEncodingMp3;
    case AV_CODEC_ID_PCM_ALAW:
      return StreamType::kAudioEncodingPcmALaw;
    case AV_CODEC_ID_PCM_MULAW:
      return StreamType::kAudioEncodingPcmMuLaw;
    case AV_CODEC_ID_VORBIS:
      return StreamType::kAudioEncodingVorbis;
    case AV_CODEC_ID_H263:
      return StreamType::kVideoEncodingH263;
    case AV_CODEC_ID_H264:
      return StreamType::kVideoEncodingH264;
    case AV_CODEC_ID_MPEG4:
      return StreamType::kVideoEncodingMpeg4;
    case AV_CODEC_ID_THEORA:
      return StreamType::kVideoEncodingTheora;
    case AV_CODEC_ID_VP3:
      return StreamType::kVideoEncodingVp3;
    case AV_CODEC_ID_VP8:
      return StreamType::kVideoEncodingVp8;
    case AV_CODEC_ID_VP9:
      return StreamType::kVideoEncodingVp9;
    default:
      FXL_LOG(WARNING) << "unsupported codec_id " << from;
      return StreamType::kMediaEncodingUnsupported;
  }
}

// Determines if codec_id represents an LPCM audio format.
bool IsLpcm(AVCodecID codec_id) {
  switch (codec_id) {
    case AV_CODEC_ID_PCM_S16BE:
    case AV_CODEC_ID_PCM_S16LE:
    case AV_CODEC_ID_PCM_S24BE:
    case AV_CODEC_ID_PCM_S24LE:
    case AV_CODEC_ID_PCM_U8:
      return true;
    default:
      return false;
  }
}

// Creates a StreamType from an AVCodecContext describing an audio type.
std::unique_ptr<StreamType> StreamTypeFromAudioCodecContext(
    const AVCodecContext& from) {
  bool decoded = from.codec != nullptr || IsLpcm(from.codec_id);

  return AudioStreamType::Create(
      decoded ? StreamType::kAudioEncodingLpcm
              : EncodingFromCodecId(from.codec_id),
      (decoded || from.extradata_size == 0)
          ? nullptr
          : Bytes::Create(from.extradata, from.extradata_size),
      Convert(from.sample_fmt), from.channels, from.sample_rate);
}

// Creates a StreamType from an AVCodecContext describing an audio type.
std::unique_ptr<StreamType> StreamTypeFromAudioCodecParameters(
    const AVCodecParameters& from) {
  bool decoded = IsLpcm(from.codec_id);

  return AudioStreamType::Create(
      decoded ? StreamType::kAudioEncodingLpcm
              : EncodingFromCodecId(from.codec_id),
      (decoded || from.extradata_size == 0)
          ? nullptr
          : Bytes::Create(from.extradata, from.extradata_size),
      Convert(static_cast<AVSampleFormat>(from.format)), from.channels,
      from.sample_rate);
}

// Converts AVColorSpace and AVColorRange to ColorSpace.
VideoStreamType::ColorSpace ColorSpaceFromAVColorSpaceAndRange(
    AVColorSpace color_space, AVColorRange color_range) {
  // TODO(dalesat): Blindly copied from Chromium.
  if (color_range == AVCOL_RANGE_JPEG) {
    return VideoStreamType::ColorSpace::kJpeg;
  }

  switch (color_space) {
    case AVCOL_SPC_UNSPECIFIED:
      return VideoStreamType::ColorSpace::kNotApplicable;
    case AVCOL_SPC_BT709:
      return VideoStreamType::ColorSpace::kHdRec709;
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_BT470BG:
      return VideoStreamType::ColorSpace::kSdRec601;
    default:
      return VideoStreamType::ColorSpace::kUnknown;
  }
}

// Converts VideoProfile to an ffmpeg profile.
int FfmpegProfileFromVideoProfile(VideoStreamType::VideoProfile video_profile) {
  // TODO(dalesat): Blindly copied from Chromium.
  switch (video_profile) {
    case VideoStreamType::VideoProfile::kH264Baseline:
      return FF_PROFILE_H264_BASELINE;
    case VideoStreamType::VideoProfile::kH264Main:
      return FF_PROFILE_H264_MAIN;
    case VideoStreamType::VideoProfile::kH264Extended:
      return FF_PROFILE_H264_EXTENDED;
    case VideoStreamType::VideoProfile::kH264High:
      return FF_PROFILE_H264_HIGH;
    case VideoStreamType::VideoProfile::kH264High10:
      return FF_PROFILE_H264_HIGH_10;
    case VideoStreamType::VideoProfile::kH264High422:
      return FF_PROFILE_H264_HIGH_422;
    case VideoStreamType::VideoProfile::kH264High444Predictive:
      return FF_PROFILE_H264_HIGH_444_PREDICTIVE;
    case VideoStreamType::VideoProfile::kUnknown:
    case VideoStreamType::VideoProfile::kNotApplicable:
    case VideoStreamType::VideoProfile::kH264ScalableBaseline:
    case VideoStreamType::VideoProfile::kH264ScalableHigh:
    case VideoStreamType::VideoProfile::kH264StereoHigh:
    case VideoStreamType::VideoProfile::kH264MultiviewHigh:
    default:
      return FF_PROFILE_UNKNOWN;
  }
}

// Creates a StreamType from an AVCodecContext describing a video type.
std::unique_ptr<StreamType> StreamTypeFromVideoCodecContext(
    const AVCodecContext& from) {
  VideoStreamType::PixelFormat pixel_format =
      PixelFormatFromAVPixelFormat(from.pix_fmt);

  std::vector<uint32_t> line_stride;
  std::vector<uint32_t> plane_offset;
  FfmpegVideoFrameLayout::LayoutFrame(
      pixel_format,
      VideoStreamType::Extent(from.coded_width, from.coded_height),
      &line_stride, &plane_offset);

  uint32_t aspect_ratio_width = from.sample_aspect_ratio.num;
  uint32_t aspect_ratio_height = from.sample_aspect_ratio.den;
  if (aspect_ratio_width == 0 || aspect_ratio_height == 0) {
    aspect_ratio_width = 1;
    aspect_ratio_height = 1;
  }

  return VideoStreamType::Create(
      from.codec == nullptr ? EncodingFromCodecId(from.codec_id)
                            : StreamType::kVideoEncodingUncompressed,
      (from.codec != nullptr || from.extradata_size == 0)
          ? nullptr
          : Bytes::Create(from.extradata, from.extradata_size),
      VideoStreamType::VideoProfile::kNotApplicable, pixel_format,
      ColorSpaceFromAVColorSpaceAndRange(from.colorspace, from.color_range),
      from.width, from.height, from.coded_width, from.coded_height,
      aspect_ratio_width, aspect_ratio_height, line_stride, plane_offset);
}

// Creates a StreamType from an AVStream describing a video type.
std::unique_ptr<StreamType> StreamTypeFromVideoStream(const AVStream& from) {
  const AVCodecParameters parameters = *from.codecpar;

  VideoStreamType::PixelFormat pixel_format = PixelFormatFromAVPixelFormat(
      static_cast<AVPixelFormat>(parameters.format));

  AVRational pixel_aspect_ratio = {1, 1};
  if (from.sample_aspect_ratio.num != 0 && from.sample_aspect_ratio.den != 0) {
    pixel_aspect_ratio = from.sample_aspect_ratio;
  } else if (parameters.sample_aspect_ratio.num != 0 &&
             parameters.sample_aspect_ratio.den != 0) {
    pixel_aspect_ratio = parameters.sample_aspect_ratio;
  }

  return VideoStreamType::Create(
      EncodingFromCodecId(parameters.codec_id),
      parameters.extradata_size == 0
          ? nullptr
          : Bytes::Create(parameters.extradata, parameters.extradata_size),
      VideoStreamType::VideoProfile::kNotApplicable, pixel_format,
      ColorSpaceFromAVColorSpaceAndRange(parameters.color_space,
                                         parameters.color_range),
      parameters.width, parameters.height, 0, 0, pixel_aspect_ratio.num,
      pixel_aspect_ratio.den, std::vector<uint32_t>(), std::vector<uint32_t>());
}

// Creates a StreamType from an AVCodecContext describing a data type.
std::unique_ptr<StreamType> StreamTypeFromDataCodecContext(
    const AVCodecContext& from) {
  // TODO(dalesat): Implement.
  return TextStreamType::Create("UNSUPPORTED TYPE (FFMPEG DATA)", nullptr);
}

// Creates a StreamType from AVCodecParameters describing a data type.
std::unique_ptr<StreamType> StreamTypeFromDataCodecParameters(
    const AVCodecParameters& from) {
  // TODO(dalesat): Implement.
  return TextStreamType::Create("UNSUPPORTED TYPE (FFMPEG DATA)", nullptr);
}

// Creates a StreamType from an AVCodecContext describing a subtitle type.
std::unique_ptr<StreamType> StreamTypeFromSubtitleCodecContext(
    const AVCodecContext& from) {
  // TODO(dalesat): Implement.
  return SubpictureStreamType::Create("UNSUPPORTED TYPE (FFMPEG SUBTITLE)",
                                      nullptr);
}

// Creates a StreamType from AVCodecParameters describing a subtitle type.
std::unique_ptr<StreamType> StreamTypeFromSubtitleCodecParameters(
    const AVCodecParameters& from) {
  // TODO(dalesat): Implement.
  return SubpictureStreamType::Create("UNSUPPORTED TYPE (FFMPEG SUBTITLE)",
                                      nullptr);
}

// Creates an AVCodecContext from an AudioStreamType.
AvCodecContextPtr AVCodecContextFromAudioStreamType(
    const AudioStreamType& stream_type) {
  FXL_DCHECK(stream_type.medium() == StreamType::Medium::kAudio);

  AVCodecID codec_id;
  AVSampleFormat sample_format = AV_SAMPLE_FMT_NONE;

  if (stream_type.encoding() == StreamType::kAudioEncodingLpcm) {
    switch (stream_type.sample_format()) {
      case AudioStreamType::SampleFormat::kUnsigned8:
        codec_id = AV_CODEC_ID_PCM_U8;
        sample_format = AV_SAMPLE_FMT_U8;
        break;
      case AudioStreamType::SampleFormat::kSigned16:
        codec_id = AV_CODEC_ID_PCM_S16LE;
        sample_format = AV_SAMPLE_FMT_S16;
        break;
      case AudioStreamType::SampleFormat::kSigned24In32:
        codec_id = AV_CODEC_ID_PCM_S24LE;
        sample_format = AV_SAMPLE_FMT_S32;
        break;
      case AudioStreamType::SampleFormat::kFloat:
        codec_id = AV_CODEC_ID_PCM_F32LE;
        sample_format = AV_SAMPLE_FMT_FLT;
        break;
      default:
        FXL_LOG(ERROR) << "unsupported sample format";
        abort();
    }
  } else if (stream_type.encoding() == StreamType::kAudioEncodingAac) {
    codec_id = AV_CODEC_ID_AAC;
  } else if (stream_type.encoding() == StreamType::kAudioEncodingAmrNb) {
    codec_id = AV_CODEC_ID_AMR_NB;
  } else if (stream_type.encoding() == StreamType::kAudioEncodingAmrWb) {
    codec_id = AV_CODEC_ID_AMR_WB;
  } else if (stream_type.encoding() == StreamType::kAudioEncodingFlac) {
    codec_id = AV_CODEC_ID_FLAC;
  } else if (stream_type.encoding() == StreamType::kAudioEncodingGsmMs) {
    codec_id = AV_CODEC_ID_GSM_MS;
  } else if (stream_type.encoding() == StreamType::kAudioEncodingMp3) {
    codec_id = AV_CODEC_ID_MP3;
  } else if (stream_type.encoding() == StreamType::kAudioEncodingPcmALaw) {
    codec_id = AV_CODEC_ID_PCM_ALAW;
  } else if (stream_type.encoding() == StreamType::kAudioEncodingPcmMuLaw) {
    codec_id = AV_CODEC_ID_PCM_MULAW;
  } else if (stream_type.encoding() == StreamType::kAudioEncodingVorbis) {
    codec_id = AV_CODEC_ID_VORBIS;
  } else if (stream_type.encoding() == StreamType::kMediaEncodingUnsupported) {
    codec_id = AV_CODEC_ID_NONE;
  } else {
    FXL_LOG(WARNING) << "unsupported encoding " << stream_type.encoding();
    codec_id = AV_CODEC_ID_NONE;
  }

  AvCodecContextPtr context(avcodec_alloc_context3(nullptr));

  context->codec_type = AVMEDIA_TYPE_AUDIO;
  context->codec_id = codec_id;
  context->sample_fmt = sample_format;
  context->channels = stream_type.channels();
  context->sample_rate = stream_type.frames_per_second();

  if (stream_type.encoding_parameters()) {
    ExtraDataFromBytes(*stream_type.encoding_parameters(), context);
  }

  return context;
}

// Creats an AVCodecContext from a VideoStreamType.
AvCodecContextPtr AVCodecContextFromVideoStreamType(
    const VideoStreamType& stream_type) {
  AVCodecID codec_id = AV_CODEC_ID_NONE;

  if (stream_type.encoding() == StreamType::kVideoEncodingH263) {
    codec_id = AV_CODEC_ID_H263;
  } else if (stream_type.encoding() == StreamType::kVideoEncodingH264) {
    codec_id = AV_CODEC_ID_H264;
  } else if (stream_type.encoding() == StreamType::kVideoEncodingMpeg4) {
    codec_id = AV_CODEC_ID_MPEG4;
  } else if (stream_type.encoding() == StreamType::kVideoEncodingTheora) {
    codec_id = AV_CODEC_ID_THEORA;
  } else if (stream_type.encoding() == StreamType::kVideoEncodingVp3) {
    codec_id = AV_CODEC_ID_VP3;
  } else if (stream_type.encoding() == StreamType::kVideoEncodingVp8) {
    codec_id = AV_CODEC_ID_VP8;
  } else if (stream_type.encoding() == StreamType::kVideoEncodingVp9) {
    codec_id = AV_CODEC_ID_VP9;
  } else if (stream_type.encoding() == StreamType::kMediaEncodingUnsupported) {
    codec_id = AV_CODEC_ID_NONE;
  } else {
    FXL_LOG(WARNING) << "unsupported encoding " << stream_type.encoding();
    codec_id = AV_CODEC_ID_NONE;
  }

  if (codec_id == AV_CODEC_ID_NONE) {
    return nullptr;
  }

  AvCodecContextPtr context(avcodec_alloc_context3(nullptr));

  context->codec_type = AVMEDIA_TYPE_VIDEO;
  context->codec_id = codec_id;
  context->profile = FfmpegProfileFromVideoProfile(stream_type.profile());
  context->pix_fmt = AVPixelFormatFromPixelFormat(stream_type.pixel_format());
  if (stream_type.color_space() == VideoStreamType::ColorSpace::kJpeg) {
    context->color_range = AVCOL_RANGE_JPEG;
  }
  context->coded_width = stream_type.coded_width();
  context->coded_height = stream_type.coded_height();
  context->sample_aspect_ratio.num = stream_type.pixel_aspect_ratio_width();
  context->sample_aspect_ratio.den = stream_type.pixel_aspect_ratio_height();

  if (stream_type.encoding_parameters()) {
    ExtraDataFromBytes(*stream_type.encoding_parameters(), context);
  }

  return context;
}

// Creats an AVCodecContext from a TextStreamType.
AvCodecContextPtr AVCodecContextFromTextStreamType(
    const TextStreamType& stream_type) {
  // TODO(dalesat): Implement.
  FXL_LOG(ERROR) << "AVCodecContextFromTextStreamType not implemented";
  abort();
}

// Creats an AVCodecContext from a SubpictureStreamType.
AvCodecContextPtr AVCodecContextFromSubpictureStreamType(
    const SubpictureStreamType& stream_type) {
  // TODO(dalesat): Implement.
  FXL_LOG(ERROR) << "AVCodecContextFromSupictureStreamType not implemented";
  abort();
}

}  // namespace

VideoStreamType::PixelFormat PixelFormatFromAVPixelFormat(
    AVPixelFormat av_pixel_format) {
  // TODO(dalesat): Blindly copied from Chromium.
  switch (av_pixel_format) {
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUVJ422P:
      return VideoStreamType::PixelFormat::kYv16;
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUVJ444P:
      return VideoStreamType::PixelFormat::kYv24;
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
      return VideoStreamType::PixelFormat::kYv12;
    case AV_PIX_FMT_YUVA420P:
      return VideoStreamType::PixelFormat::kYv12A;
    default:
      return VideoStreamType::PixelFormat::kUnknown;
  }
}

AVPixelFormat AVPixelFormatFromPixelFormat(
    VideoStreamType::PixelFormat pixel_format) {
  // TODO(dalesat): Blindly copied from Chromium.
  switch (pixel_format) {
    case VideoStreamType::PixelFormat::kYv12:
      return AV_PIX_FMT_YUV420P;
    case VideoStreamType::PixelFormat::kYv16:
      return AV_PIX_FMT_YUV422P;
    case VideoStreamType::PixelFormat::kYv12A:
      return AV_PIX_FMT_YUVA420P;
    case VideoStreamType::PixelFormat::kYv24:
      return AV_PIX_FMT_YUV444P;
    case VideoStreamType::PixelFormat::kUnknown:
    case VideoStreamType::PixelFormat::kI420:
    case VideoStreamType::PixelFormat::kNv12:
    case VideoStreamType::PixelFormat::kNv21:
    case VideoStreamType::PixelFormat::kUyvy:
    case VideoStreamType::PixelFormat::kYuy2:
    case VideoStreamType::PixelFormat::kArgb:
    case VideoStreamType::PixelFormat::kXrgb:
    case VideoStreamType::PixelFormat::kRgb24:
    case VideoStreamType::PixelFormat::kRgb32:
    case VideoStreamType::PixelFormat::kMjpeg:
    case VideoStreamType::PixelFormat::kMt21:
    default:
      return AV_PIX_FMT_NONE;
  }
}

// static
std::unique_ptr<StreamType> AvCodecContext::GetStreamType(
    const AVCodecContext& from) {
  switch (from.codec_type) {
    case AVMEDIA_TYPE_AUDIO:
      return StreamTypeFromAudioCodecContext(from);
    case AVMEDIA_TYPE_VIDEO:
      return StreamTypeFromVideoCodecContext(from);
    case AVMEDIA_TYPE_UNKNOWN:
    // Treated as AVMEDIA_TYPE_DATA.
    case AVMEDIA_TYPE_DATA:
      return StreamTypeFromDataCodecContext(from);
    case AVMEDIA_TYPE_SUBTITLE:
      return StreamTypeFromSubtitleCodecContext(from);
    case AVMEDIA_TYPE_ATTACHMENT:
    case AVMEDIA_TYPE_NB:
    default:
      FXL_LOG(ERROR) << "unsupported code type " << from.codec_type;
      abort();
  }
}

// static
std::unique_ptr<StreamType> AvCodecContext::GetStreamType(
    const AVStream& from) {
  switch (from.codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
      return StreamTypeFromAudioCodecParameters(*from.codecpar);
    case AVMEDIA_TYPE_VIDEO:
      return StreamTypeFromVideoStream(from);
    case AVMEDIA_TYPE_UNKNOWN:
    // Treated as AVMEDIA_TYPE_DATA.
    case AVMEDIA_TYPE_DATA:
      return StreamTypeFromDataCodecParameters(*from.codecpar);
    case AVMEDIA_TYPE_SUBTITLE:
      return StreamTypeFromSubtitleCodecParameters(*from.codecpar);
    case AVMEDIA_TYPE_ATTACHMENT:
    case AVMEDIA_TYPE_NB:
    default:
      FXL_LOG(ERROR) << "unsupported code type " << from.codecpar->codec_type;
      abort();
  }
}

// static
AvCodecContextPtr AvCodecContext::Create(const StreamType& stream_type) {
  InitFfmpeg();

  switch (stream_type.medium()) {
    case StreamType::Medium::kAudio:
      return AVCodecContextFromAudioStreamType(*stream_type.audio());
    case StreamType::Medium::kVideo:
      return AVCodecContextFromVideoStreamType(*stream_type.video());
    case StreamType::Medium::kText:
      return AVCodecContextFromTextStreamType(*stream_type.text());
    case StreamType::Medium::kSubpicture:
      return AVCodecContextFromSubpictureStreamType(*stream_type.subpicture());
    default:
      return nullptr;
  }
}

}  // namespace media_player

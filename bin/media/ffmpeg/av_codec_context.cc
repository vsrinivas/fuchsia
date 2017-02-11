// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/ffmpeg/av_codec_context.h"

#include "apps/media/src/ffmpeg/ffmpeg_init.h"
#include "apps/media/src/framework/types/audio_stream_type.h"
#include "apps/media/src/framework/types/subpicture_stream_type.h"
#include "apps/media/src/framework/types/text_stream_type.h"
#include "apps/media/src/framework/types/video_stream_type.h"
#include "lib/ftl/logging.h"
extern "C" {
#include "third_party/ffmpeg/libavformat/avformat.h"
}

namespace media {

namespace {

static const uint32_t kFrameSizeAlignment = 16;
static const uint32_t kFrameSizePadding = 16;

// Converts an AVSampleFormat into an AudioStreamType::SampleFormat.
AudioStreamType::SampleFormat Convert(AVSampleFormat av_sample_format) {
  switch (av_sample_format) {
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
    case AV_SAMPLE_FMT_NONE:
    case AV_SAMPLE_FMT_DBL:
    case AV_SAMPLE_FMT_DBLP:
    case AV_SAMPLE_FMT_NB:
    default:
      FTL_LOG(ERROR) << "unsupported av_sample_format " << av_sample_format;
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

// Creates a StreamType from an AVCodecContext describing an LPCM type.
std::unique_ptr<StreamType> StreamTypeFromLpcmCodecContext(
    const AVCodecContext& from) {
  return AudioStreamType::Create(StreamType::kAudioEncodingLpcm, nullptr,
                                 Convert(from.sample_fmt), from.channels,
                                 from.sample_rate);
}

// Creates a StreamType from an AVCodecContext describing a compressed audio
// type.
std::unique_ptr<StreamType> StreamTypeFromCompressedAudioCodecContext(
    const AVCodecContext& from) {
  const char* encoding;
  switch (from.codec_id) {
    case AV_CODEC_ID_AAC:
      encoding = StreamType::kAudioEncodingAac;
      break;
    case AV_CODEC_ID_AMR_NB:
      encoding = StreamType::kAudioEncodingAmrNb;
      break;
    case AV_CODEC_ID_AMR_WB:
      encoding = StreamType::kAudioEncodingAmrWb;
      break;
    case AV_CODEC_ID_FLAC:
      encoding = StreamType::kAudioEncodingFlac;
      break;
    case AV_CODEC_ID_GSM_MS:
      encoding = StreamType::kAudioEncodingGsmMs;
      break;
    case AV_CODEC_ID_MP3:
      encoding = StreamType::kAudioEncodingMp3;
      break;
    case AV_CODEC_ID_PCM_ALAW:
      encoding = StreamType::kAudioEncodingPcmALaw;
      break;
    case AV_CODEC_ID_PCM_MULAW:
      encoding = StreamType::kAudioEncodingPcmMuLaw;
      break;
    case AV_CODEC_ID_VORBIS:
      encoding = StreamType::kAudioEncodingVorbis;
      break;
    default:
      FTL_LOG(ERROR) << "unsupported codec_id " << from.codec_id;
      abort();
  }

  return AudioStreamType::Create(
      encoding, from.extradata_size == 0
                    ? nullptr
                    : Bytes::Create(from.extradata, from.extradata_size),
      Convert(from.sample_fmt), from.channels, from.sample_rate);
}

// Converts AVColorSpace and AVColorRange to ColorSpace.
VideoStreamType::ColorSpace ColorSpaceFromAVColorSpaceAndRange(
    AVColorSpace color_space,
    AVColorRange color_range) {
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

// Rounds up |value| to the nearest multiple of |alignment|. |alignment| must
// be a power of 2.
static inline uint32_t RoundUpToAlign(uint32_t value, uint32_t alignment) {
  return ((value + (alignment - 1)) & ~(alignment - 1));
}

VideoStreamType::Extent FfmpegCommonAlignment(
    const VideoStreamType::PixelFormatInfo& info) {
  uint32_t max_sample_width = 0;
  uint32_t max_sample_height = 0;
  for (uint32_t plane = 0; plane < info.plane_count_; ++plane) {
    const VideoStreamType::Extent sample_size =
        info.sample_size_for_plane(plane);
    max_sample_width = std::max(max_sample_width, sample_size.width());
    max_sample_height = std::max(max_sample_height, sample_size.height());
  }
  return VideoStreamType::Extent(max_sample_width, max_sample_height);
}

VideoStreamType::Extent FfmpegAlignedSize(
    const VideoStreamType::Extent& unaligned_size,
    const VideoStreamType::PixelFormatInfo& info) {
  const VideoStreamType::Extent alignment = FfmpegCommonAlignment(info);
  const VideoStreamType::Extent adjusted = VideoStreamType::Extent(
      RoundUpToAlign(unaligned_size.width(), alignment.width()),
      RoundUpToAlign(unaligned_size.height(), alignment.height()));
  FTL_DCHECK((adjusted.width() % alignment.width() == 0) &&
             (adjusted.height() % alignment.height() == 0));
  return adjusted;
}

// Creates a StreamType from an AVCodecContext describing a compressed video
// type.
std::unique_ptr<StreamType> StreamTypeFromCompressedVideoCodecContext(
    const AVCodecContext& from) {
  const char* encoding;
  switch (from.codec_id) {
    case AV_CODEC_ID_H263:
      encoding = StreamType::kVideoEncodingH263;
      break;
    case AV_CODEC_ID_H264:
      encoding = StreamType::kVideoEncodingH264;
      break;
    case AV_CODEC_ID_MPEG4:
      encoding = StreamType::kVideoEncodingMpeg4;
      break;
    case AV_CODEC_ID_THEORA:
      encoding = StreamType::kVideoEncodingTheora;
      break;
    case AV_CODEC_ID_VP3:
      encoding = StreamType::kVideoEncodingVp3;
      break;
    case AV_CODEC_ID_VP8:
      encoding = StreamType::kVideoEncodingVp8;
      break;
    case AV_CODEC_ID_VP9:
      encoding = StreamType::kVideoEncodingVp9;
      break;
    default:
      FTL_LOG(ERROR) << "unsupported codec_id " << from.codec_id;
      abort();
  }

  VideoStreamType::PixelFormat pixel_format =
      PixelFormatFromAVPixelFormat(from.pix_fmt);

  std::vector<uint32_t> line_stride;
  std::vector<uint32_t> plane_offset;
  LayoutFrame(pixel_format,
              VideoStreamType::Extent(from.coded_width, from.coded_height),
              &line_stride, &plane_offset);

  return VideoStreamType::Create(
      encoding, from.extradata_size == 0
                    ? nullptr
                    : Bytes::Create(from.extradata, from.extradata_size),
      VideoStreamType::VideoProfile::kNotApplicable, pixel_format,
      ColorSpaceFromAVColorSpaceAndRange(from.colorspace, from.color_range),
      from.width, from.height, from.coded_width, from.coded_height, line_stride,
      plane_offset);
}

// Creates a StreamType from an AVCodecContext describing an uncompressed video
// type.
std::unique_ptr<StreamType> StreamTypeFromUncompressedVideoCodecContext(
    const AVCodecContext& from) {
  VideoStreamType::PixelFormat pixel_format =
      PixelFormatFromAVPixelFormat(from.pix_fmt);

  std::vector<uint32_t> line_stride;
  std::vector<uint32_t> plane_offset;
  LayoutFrame(pixel_format,
              VideoStreamType::Extent(from.coded_width, from.coded_height),
              &line_stride, &plane_offset);

  return VideoStreamType::Create(
      StreamType::kVideoEncodingUncompressed, nullptr,
      VideoStreamType::VideoProfile::kNotApplicable, pixel_format,
      ColorSpaceFromAVColorSpaceAndRange(from.colorspace, from.color_range),
      from.width, from.height, from.coded_width, from.coded_height, line_stride,
      plane_offset);
}

// Creates a StreamType from an AVCodecContext describing a data type.
std::unique_ptr<StreamType> StreamTypeFromDataCodecContext(
    const AVCodecContext& from) {
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

// Creates an AVCodecContext from an AudioStreamType.
AvCodecContextPtr AVCodecContextFromAudioStreamType(
    const AudioStreamType& stream_type) {
  FTL_DCHECK(stream_type.medium() == StreamType::Medium::kAudio);

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
        FTL_LOG(ERROR) << "unsupported sample format";
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
  } else {
    FTL_LOG(ERROR) << "unsupported encoding " << stream_type.encoding();
    abort();
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
  } else {
    FTL_LOG(ERROR) << "unsupported encoding " << stream_type.encoding();
    abort();
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

  if (stream_type.encoding_parameters()) {
    ExtraDataFromBytes(*stream_type.encoding_parameters(), context);
  }

  return context;
}

// Creats an AVCodecContext from a TextStreamType.
AvCodecContextPtr AVCodecContextFromTextStreamType(
    const TextStreamType& stream_type) {
  // TODO(dalesat): Implement.
  FTL_LOG(ERROR) << "AVCodecContextFromTextStreamType not implemented";
  abort();
}

// Creats an AVCodecContext from a SubpictureStreamType.
AvCodecContextPtr AVCodecContextFromSubpictureStreamType(
    const SubpictureStreamType& stream_type) {
  // TODO(dalesat): Implement.
  FTL_LOG(ERROR) << "AVCodecContextFromSupictureStreamType not implemented";
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

size_t LayoutFrame(VideoStreamType::PixelFormat pixel_format,
                   const VideoStreamType::Extent& coded_size,
                   std::vector<uint32_t>* line_stride_out,
                   std::vector<uint32_t>* plane_offset_out) {
  FTL_DCHECK(line_stride_out != nullptr);
  FTL_DCHECK(plane_offset_out != nullptr);

  const VideoStreamType::PixelFormatInfo& info =
      VideoStreamType::InfoForPixelFormat(pixel_format);

  line_stride_out->resize(info.plane_count_);
  plane_offset_out->resize(info.plane_count_);

  uint32_t plane_offset = 0;
  VideoStreamType::Extent aligned_size = FfmpegAlignedSize(coded_size, info);

  for (uint32_t plane = 0; plane < info.plane_count_; ++plane) {
    // The *2 in alignment for height is because some formats (e.g. h264)
    // allow interlaced coding, and then the size needs to be a multiple of two
    // macroblocks (vertically). See avcodec_align_dimensions2.
    const uint32_t height = RoundUpToAlign(
        info.RowCount(plane, aligned_size.height()), kFrameSizeAlignment * 2);
    (*line_stride_out)[plane] = RoundUpToAlign(
        info.BytesPerRow(plane, aligned_size.width()), kFrameSizeAlignment);
    (*plane_offset_out)[plane] = plane_offset;
    plane_offset += height * (*line_stride_out)[plane];
  }

  // The extra line of UV being allocated is because h264 chroma MC overreads
  // by one line in some cases, see avcodec_align_dimensions2() and
  // h264_chromamc.asm:put_h264_chroma_mc4_ssse3().
  //
  // This is a bit of a hack and is really only here because of ffmpeg-specific
  // issues. It works because we happen to know that info.plane_count_ - 1 is
  // the U plane for the format we currently care about.
  return static_cast<size_t>(plane_offset +
                             (*line_stride_out)[info.plane_count_ - 1] +
                             kFrameSizePadding);
}

// static
std::unique_ptr<StreamType> AvCodecContext::GetStreamType(
    const AVCodecContext& from) {
  switch (from.codec_type) {
    case AVMEDIA_TYPE_AUDIO:
      switch (from.codec_id) {
        case AV_CODEC_ID_PCM_S16BE:
        case AV_CODEC_ID_PCM_S16LE:
        case AV_CODEC_ID_PCM_S24BE:
        case AV_CODEC_ID_PCM_S24LE:
        case AV_CODEC_ID_PCM_U8:
          return StreamTypeFromLpcmCodecContext(from);
        default:
          if (from.codec == nullptr) {
            return StreamTypeFromCompressedAudioCodecContext(from);
          } else {
            return StreamTypeFromLpcmCodecContext(from);
          }
      }
    case AVMEDIA_TYPE_VIDEO:
      if (from.codec == nullptr) {
        return StreamTypeFromCompressedVideoCodecContext(from);
      } else {
        return StreamTypeFromUncompressedVideoCodecContext(from);
      }
    case AVMEDIA_TYPE_UNKNOWN:
    // Treated as AVMEDIA_TYPE_DATA.
    case AVMEDIA_TYPE_DATA:
      return StreamTypeFromDataCodecContext(from);
    case AVMEDIA_TYPE_SUBTITLE:
      return StreamTypeFromSubtitleCodecContext(from);
    case AVMEDIA_TYPE_ATTACHMENT:
    case AVMEDIA_TYPE_NB:
    default:
      FTL_LOG(ERROR) << "unsupported code type " << from.codec_type;
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

}  // namespace media

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/ffmpeg/av_codec_context.h"

#include <endian.h>
#include <lib/syslog/cpp/macros.h>

#include "src/media/playback/mediaplayer/ffmpeg/ffmpeg_init.h"
#include "src/media/playback/mediaplayer/graph/types/audio_stream_type.h"
#include "src/media/playback/mediaplayer/graph/types/subpicture_stream_type.h"
#include "src/media/playback/mediaplayer/graph/types/text_stream_type.h"
#include "src/media/playback/mediaplayer/graph/types/video_stream_type.h"
extern "C" {
#include "libavformat/avformat.h"
#include "libavutil/encryption_info.h"
}

namespace media_player {

namespace {

constexpr uint32_t kPsshType = 0x70737368;  // fourcc 'pssh'
constexpr uint32_t kSystemIdSize = 16;      // System IDs in pssh boxes are always 16 bytes.
constexpr uint32_t kKeyIdSize = 16;         // Key IDs in pssh boxes are always 16 bytes.

// Deposits |size| bytes copied from |data| at |*p_in_out| and increases |*p_in_out| by |size|.
void Deposit(const uint8_t* data, size_t size, uint8_t** p_in_out) {
  FX_DCHECK(size);
  FX_DCHECK(data);
  FX_DCHECK(p_in_out);
  FX_DCHECK(*p_in_out);
  memcpy(*p_in_out, data, size);
  *p_in_out += size;
}

// Deposits |t| at |*p_in_out| and increases |*p_in_out| by |sizeof(t)|.
template <typename T>
void Deposit(T t, uint8_t** p_in_out) {
  Deposit(reinterpret_cast<const uint8_t*>(&t), sizeof(t), p_in_out);
}

// Creates a PSSH box as raw bytes from encryption init data on a stream, if there is any, otherwise
// returns nullptr.
std::unique_ptr<Bytes> EncryptionParametersFromStream(const AVStream& from) {
// TODO(fxr/87639): remove once we're committed to the new version.
#if LIBAVFORMAT_VERSION_MAJOR == 58
  int encryption_side_data_size;
#else
  size_t encryption_side_data_size;
#endif
  uint8_t* encryption_side_data =
      av_stream_get_side_data(&from, AV_PKT_DATA_ENCRYPTION_INIT_INFO, &encryption_side_data_size);
  if (encryption_side_data == nullptr) {
    return nullptr;
  }

  // Create an |AVEncryptionInitInfo| structure from the side data. This is deleted below by
  // calling |av_encryption_init_info_free|.
  AVEncryptionInitInfo* encryption_init_info =
      av_encryption_init_info_get_side_data(encryption_side_data, encryption_side_data_size);

  // A pssh box has the following structure. Numeric values are big-endian.
  //
  // uint32_t size;
  // uint32_t type; // fourcc 'pssh'
  // uint8_t version;
  // uint8_t flags[3]; // all zeros
  // uint8_t system_id[16];
  // if (version_ > 0) {
  //   uint32_t key_id_count;
  //   uint8_t key_ids[16][kid_count];
  // }
  // uint32_t data_size;
  // uint8_t data[data_size];

  struct __attribute__((packed)) PsshBoxInvariantPrefix {
    uint32_t size_;
    uint32_t type_;
    uint8_t version_;
    uint8_t flags_[3];
    uint8_t system_id_[kSystemIdSize];
  };

  // Determine the size of the pssh box.
  uint32_t box_size =
      sizeof(PsshBoxInvariantPrefix) + sizeof(uint32_t) + encryption_init_info->data_size;
  if (encryption_init_info->num_key_ids != 0) {
    box_size += sizeof(uint32_t) + kKeyIdSize * encryption_init_info->num_key_ids;
    FX_DCHECK(encryption_init_info->key_id_size == kKeyIdSize);
  }

  // Create a buffer of the correct size.
  auto result = Bytes::Create(box_size);

  // Align |prefix| with the start of the buffer and populate it.
  auto prefix = new (result->data()) PsshBoxInvariantPrefix;
  prefix->size_ = htobe32(box_size);
  prefix->type_ = htobe32(kPsshType);
  prefix->version_ = (encryption_init_info->num_key_ids == 0) ? 0 : 1;
  prefix->flags_[0] = 0;
  prefix->flags_[1] = 0;
  prefix->flags_[2] = 0;

  FX_DCHECK(encryption_init_info->system_id_size == kSystemIdSize);
  memcpy(prefix->system_id_, encryption_init_info->system_id, kSystemIdSize);

  // Set |p| to point to the first byte after |prefix|. |p| will be our write pointer into
  // |result->data()| as we write the key IDs, data size and data.
  auto p = reinterpret_cast<uint8_t*>(prefix + 1);

  // Deposit the key IDs.
  if (encryption_init_info->num_key_ids != 0) {
    Deposit(htobe32(encryption_init_info->num_key_ids), &p);
    for (uint32_t i = 0; i < encryption_init_info->num_key_ids; ++i) {
      FX_DCHECK(encryption_init_info->key_ids[i]);
      Deposit(encryption_init_info->key_ids[i], kKeyIdSize, &p);
    }
  }

  // Deposit the data size and data.
  Deposit(htobe32(encryption_init_info->data_size), &p);
  Deposit(encryption_init_info->data, encryption_init_info->data_size, &p);

  FX_DCHECK(p == result->data() + box_size);

  av_encryption_init_info_free(encryption_init_info);

  return result;
}

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
      FX_LOGS(ERROR) << "unsupported av_sample_format " << av_sample_format;
      abort();
  }
}

template <typename T>
std::unique_ptr<Bytes> BytesFromExtraData(T& from) {
  return from.extradata_size == 0 ? nullptr : Bytes::Create(from.extradata, from.extradata_size);
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
    case AV_CODEC_ID_AAC_LATM:
      return StreamType::kAudioEncodingAacLatm;
    case AV_CODEC_ID_AMR_NB:
      return StreamType::kAudioEncodingAmrNb;
    case AV_CODEC_ID_AMR_WB:
      return StreamType::kAudioEncodingAmrWb;
    case AV_CODEC_ID_APTX:
      return StreamType::kAudioEncodingAptX;
    case AV_CODEC_ID_FLAC:
      return StreamType::kAudioEncodingFlac;
    case AV_CODEC_ID_GSM_MS:
      return StreamType::kAudioEncodingGsmMs;
    case AV_CODEC_ID_MP3:
      return StreamType::kAudioEncodingMp3;
    case AV_CODEC_ID_OPUS:
      return StreamType::kAudioEncodingOpus;
    case AV_CODEC_ID_PCM_ALAW:
      return StreamType::kAudioEncodingPcmALaw;
    case AV_CODEC_ID_PCM_MULAW:
      return StreamType::kAudioEncodingPcmMuLaw;
    case AV_CODEC_ID_SBC:
      return StreamType::kAudioEncodingSbc;
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
      FX_LOGS(WARNING) << "unsupported codec_id " << avcodec_get_name(from);
      return StreamType::kMediaEncodingUnsupported;
  }
}

// Determines if codec_id represents an LPCM audio format.
bool IsLpcm(AVCodecID codec_id) {
  switch (codec_id) {
    case AV_CODEC_ID_PCM_F32LE:
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
std::unique_ptr<StreamType> StreamTypeFromAudioCodecContext(const AVCodecContext& from) {
  bool decoded = from.codec != nullptr || IsLpcm(from.codec_id);

  return AudioStreamType::Create(
      nullptr, decoded ? StreamType::kAudioEncodingLpcm : EncodingFromCodecId(from.codec_id),
      decoded ? nullptr : BytesFromExtraData(from), Convert(from.sample_fmt), from.channels,
      from.sample_rate);
}

// Creates a StreamType from an AVCodecContext describing an audio type.
std::unique_ptr<StreamType> StreamTypeFromAudioStream(const AVStream& from) {
  FX_DCHECK(from.codecpar);

  auto& codecpar = *from.codecpar;
  bool decoded = IsLpcm(codecpar.codec_id);

  return AudioStreamType::Create(
      EncryptionParametersFromStream(from),
      decoded ? StreamType::kAudioEncodingLpcm : EncodingFromCodecId(codecpar.codec_id),
      decoded ? nullptr : BytesFromExtraData(codecpar),
      Convert(static_cast<AVSampleFormat>(codecpar.format)), codecpar.channels,
      codecpar.sample_rate);
}

// Converts AVColorSpace and AVColorRange to ColorSpace.
VideoStreamType::ColorSpace ColorSpaceFromAVColorSpaceAndRange(AVColorSpace color_space,
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

// Creates a StreamType from an AVCodecContext describing a video type.
std::unique_ptr<StreamType> StreamTypeFromVideoCodecContext(const AVCodecContext& from) {
  int coded_width = from.coded_width;
  int coded_height = from.coded_height;
  avcodec_align_dimensions(const_cast<AVCodecContext*>(&from), &coded_width, &coded_height);
  FX_DCHECK(coded_width >= from.coded_width);
  FX_DCHECK(coded_height >= from.coded_height);

  uint32_t aspect_ratio_width = from.sample_aspect_ratio.num;
  uint32_t aspect_ratio_height = from.sample_aspect_ratio.den;
  if (aspect_ratio_width == 0 || aspect_ratio_height == 0) {
    aspect_ratio_width = 1;
    aspect_ratio_height = 1;
  }

  uint32_t line_stride;
  switch (from.pix_fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
      line_stride = coded_width;
      break;
    default:
      line_stride = 0;
      FX_LOGS(FATAL) << "Unrecognized pixel format " << from.pix_fmt;
  }

  return VideoStreamType::Create(
      nullptr,
      from.codec == nullptr ? EncodingFromCodecId(from.codec_id)
                            : StreamType::kVideoEncodingUncompressed,
      from.codec != nullptr ? nullptr : BytesFromExtraData(from),
      PixelFormatFromAVPixelFormat(from.pix_fmt),
      ColorSpaceFromAVColorSpaceAndRange(from.colorspace, from.color_range), from.width,
      from.height, coded_width, coded_height, aspect_ratio_width, aspect_ratio_height, line_stride);
}

// Creates a StreamType from an AVStream describing a video type.
std::unique_ptr<StreamType> StreamTypeFromVideoStream(const AVStream& from) {
  const AVCodecParameters parameters = *from.codecpar;

  VideoStreamType::PixelFormat pixel_format =
      PixelFormatFromAVPixelFormat(static_cast<AVPixelFormat>(parameters.format));

  AVRational pixel_aspect_ratio = {1, 1};
  if (from.sample_aspect_ratio.num != 0 && from.sample_aspect_ratio.den != 0) {
    pixel_aspect_ratio = from.sample_aspect_ratio;
  } else if (parameters.sample_aspect_ratio.num != 0 && parameters.sample_aspect_ratio.den != 0) {
    pixel_aspect_ratio = parameters.sample_aspect_ratio;
  }

  return VideoStreamType::Create(
      EncryptionParametersFromStream(from), EncodingFromCodecId(parameters.codec_id),
      BytesFromExtraData(parameters), pixel_format,
      ColorSpaceFromAVColorSpaceAndRange(parameters.color_space, parameters.color_range),
      parameters.width, parameters.height, 0, 0, pixel_aspect_ratio.num, pixel_aspect_ratio.den, 0);
}

// Creates a StreamType from an AVCodecContext describing a data type.
std::unique_ptr<StreamType> StreamTypeFromDataCodecContext(const AVCodecContext& from) {
  // TODO(dalesat): Implement.
  return TextStreamType::Create(nullptr, "UNSUPPORTED TYPE (FFMPEG DATA)", nullptr);
}

// Creates a StreamType from AVCodecParameters describing a data type.
std::unique_ptr<StreamType> StreamTypeFromDataCodecParameters(const AVCodecParameters& from) {
  // TODO(dalesat): Implement.
  return TextStreamType::Create(nullptr, "UNSUPPORTED TYPE (FFMPEG DATA)", nullptr);
}

// Creates a StreamType from an AVCodecContext describing a subtitle type.
std::unique_ptr<StreamType> StreamTypeFromSubtitleCodecContext(const AVCodecContext& from) {
  // TODO(dalesat): Implement.
  return SubpictureStreamType::Create(nullptr, "UNSUPPORTED TYPE (FFMPEG SUBTITLE)", nullptr);
}

// Creates a StreamType from AVCodecParameters describing a subtitle type.
std::unique_ptr<StreamType> StreamTypeFromSubtitleCodecParameters(const AVCodecParameters& from) {
  // TODO(dalesat): Implement.
  return SubpictureStreamType::Create(nullptr, "UNSUPPORTED TYPE (FFMPEG SUBTITLE)", nullptr);
}

// Creates an AVCodecContext from an AudioStreamType.
AvCodecContextPtr AVCodecContextFromAudioStreamType(const AudioStreamType& stream_type) {
  FX_DCHECK(stream_type.medium() == StreamType::Medium::kAudio);

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
        FX_LOGS(ERROR) << "unsupported sample format";
        abort();
    }
  } else if (stream_type.encoding() == StreamType::kAudioEncodingAac) {
    codec_id = AV_CODEC_ID_AAC;
  } else if (stream_type.encoding() == StreamType::kAudioEncodingAacLatm) {
    codec_id = AV_CODEC_ID_AAC_LATM;
  } else if (stream_type.encoding() == StreamType::kAudioEncodingAmrNb) {
    codec_id = AV_CODEC_ID_AMR_NB;
  } else if (stream_type.encoding() == StreamType::kAudioEncodingAmrWb) {
    codec_id = AV_CODEC_ID_AMR_WB;
  } else if (stream_type.encoding() == StreamType::kAudioEncodingAptX) {
    codec_id = AV_CODEC_ID_APTX;
  } else if (stream_type.encoding() == StreamType::kAudioEncodingFlac) {
    codec_id = AV_CODEC_ID_FLAC;
  } else if (stream_type.encoding() == StreamType::kAudioEncodingGsmMs) {
    codec_id = AV_CODEC_ID_GSM_MS;
  } else if (stream_type.encoding() == StreamType::kAudioEncodingMp3) {
    codec_id = AV_CODEC_ID_MP3;
  } else if (stream_type.encoding() == StreamType::kAudioEncodingOpus) {
    codec_id = AV_CODEC_ID_OPUS;
  } else if (stream_type.encoding() == StreamType::kAudioEncodingPcmALaw) {
    codec_id = AV_CODEC_ID_PCM_ALAW;
  } else if (stream_type.encoding() == StreamType::kAudioEncodingPcmMuLaw) {
    codec_id = AV_CODEC_ID_PCM_MULAW;
  } else if (stream_type.encoding() == StreamType::kAudioEncodingSbc) {
    codec_id = AV_CODEC_ID_SBC;
    switch (stream_type.sample_format()) {
      case AudioStreamType::SampleFormat::kUnsigned8:
        sample_format = AV_SAMPLE_FMT_U8P;
        break;
      case AudioStreamType::SampleFormat::kSigned16:
        sample_format = AV_SAMPLE_FMT_S16P;
        break;
      case AudioStreamType::SampleFormat::kSigned24In32:
        sample_format = AV_SAMPLE_FMT_S32P;
        break;
      case AudioStreamType::SampleFormat::kFloat:
        sample_format = AV_SAMPLE_FMT_FLTP;
        break;
      default:
        FX_LOGS(ERROR) << "unsupported sample format";
        abort();
    }
  } else if (stream_type.encoding() == StreamType::kAudioEncodingVorbis) {
    codec_id = AV_CODEC_ID_VORBIS;
  } else if (stream_type.encoding() == StreamType::kMediaEncodingUnsupported) {
    codec_id = AV_CODEC_ID_NONE;
  } else {
    FX_LOGS(WARNING) << "unsupported encoding " << stream_type.encoding();
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
AvCodecContextPtr AVCodecContextFromVideoStreamType(const VideoStreamType& stream_type) {
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
    FX_LOGS(WARNING) << "unsupported encoding " << stream_type.encoding();
    codec_id = AV_CODEC_ID_NONE;
  }

  if (codec_id == AV_CODEC_ID_NONE) {
    return nullptr;
  }

  AvCodecContextPtr context(avcodec_alloc_context3(nullptr));

  context->codec_type = AVMEDIA_TYPE_VIDEO;
  context->codec_id = codec_id;
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
AvCodecContextPtr AVCodecContextFromTextStreamType(const TextStreamType& stream_type) {
  // TODO(dalesat): Implement.
  FX_LOGS(ERROR) << "AVCodecContextFromTextStreamType not implemented";
  abort();
}

// Creats an AVCodecContext from a SubpictureStreamType.
AvCodecContextPtr AVCodecContextFromSubpictureStreamType(const SubpictureStreamType& stream_type) {
  // TODO(dalesat): Implement.
  FX_LOGS(ERROR) << "AVCodecContextFromSupictureStreamType not implemented";
  abort();
}

}  // namespace

VideoStreamType::PixelFormat PixelFormatFromAVPixelFormat(AVPixelFormat av_pixel_format) {
  switch (av_pixel_format) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
      return VideoStreamType::PixelFormat::kI420;
    default:
      return VideoStreamType::PixelFormat::kUnknown;
  }
}

AVPixelFormat AVPixelFormatFromPixelFormat(VideoStreamType::PixelFormat pixel_format) {
  switch (pixel_format) {
    case VideoStreamType::PixelFormat::kI420:
      return AV_PIX_FMT_YUV420P;
    case VideoStreamType::PixelFormat::kUnknown:
    case VideoStreamType::PixelFormat::kArgb:
    case VideoStreamType::PixelFormat::kYuy2:
    case VideoStreamType::PixelFormat::kNv12:
    case VideoStreamType::PixelFormat::kYv12:
    default:
      return AV_PIX_FMT_NONE;
  }
}

// static
std::unique_ptr<StreamType> AvCodecContext::GetStreamType(const AVCodecContext& from) {
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
      FX_LOGS(ERROR) << "unsupported code type " << from.codec_type;
      abort();
  }
}

// static
std::unique_ptr<StreamType> AvCodecContext::GetStreamType(const AVStream& from) {
  switch (from.codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
      return StreamTypeFromAudioStream(from);
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
      FX_LOGS(ERROR) << "unsupported code type " << from.codecpar->codec_type;
      abort();
  }
}

// static
AvCodecContextPtr AvCodecContext::Create(const StreamType& stream_type) {
  FX_DCHECK(!stream_type.encrypted());

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

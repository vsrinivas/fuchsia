// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/ffmpeg/av_codec_context.h"

#include <endian.h>
#include <lib/syslog/cpp/macros.h>

#include "src/media/vnext/lib/ffmpeg/ffmpeg_init.h"
extern "C" {
#include "libavformat/avformat.h"
#include "libavutil/encryption_info.h"
}

namespace fmlib {
namespace {

constexpr uint32_t kPsshType = 0x70737368;  // fourcc 'pssh'
constexpr uint32_t kSystemIdSize = 16;      // System IDs in pssh boxes are always 16 bytes.
constexpr uint32_t kKeyIdSize = 16;         // Key IDs in pssh boxes are always 16 bytes.
const char* kUnsupportedCodecIdCompressionType = "UNSUPPORTED CODEC ID";

// Deposits |size| bytes copied from |data| at |*p_in_out| and increases |*p_in_out| by |size|.
void Deposit(const uint8_t* data, size_t size, uint8_t** p_in_out) {
  FX_CHECK(size);
  FX_CHECK(data);
  FX_CHECK(p_in_out);
  FX_CHECK(*p_in_out);
  memcpy(*p_in_out, data, size);
  *p_in_out += size;
}

// Deposits |t| at |*p_in_out| and increases |*p_in_out| by |sizeof(t)|.
template <typename T>
void Deposit(T t, uint8_t** p_in_out) {
  Deposit(reinterpret_cast<const uint8_t*>(&t), sizeof(t), p_in_out);
}

// Creates a PSSH box as raw bytes from encryption init data on a stream, if there is any, otherwise
// returns a null VectorPtr.
fidl::VectorPtr<uint8_t> EncryptionParametersFromStream(const AVStream& from) {
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
    box_size +=
        static_cast<uint32_t>(sizeof(uint32_t)) + kKeyIdSize * encryption_init_info->num_key_ids;
    FX_CHECK(encryption_init_info->key_id_size == kKeyIdSize);
  }

  // Create a buffer of the correct size.
  auto result = std::vector<uint8_t>(box_size);

  // Align |prefix| with the start of the buffer and populate it.
  auto prefix = new (result.data()) PsshBoxInvariantPrefix;
  prefix->size_ = htobe32(box_size);
  prefix->type_ = htobe32(kPsshType);
  prefix->version_ = (encryption_init_info->num_key_ids == 0) ? 0 : 1;
  prefix->flags_[0] = 0;
  prefix->flags_[1] = 0;
  prefix->flags_[2] = 0;

  FX_CHECK(encryption_init_info->system_id_size == kSystemIdSize);
  memcpy(prefix->system_id_, encryption_init_info->system_id, kSystemIdSize);

  // Set |p| to point to the first byte after |prefix|. |p| will be our write pointer into
  // |result.data()| as we write the key IDs, data size and data.
  auto p = reinterpret_cast<uint8_t*>(prefix + 1);

  // Deposit the key IDs.
  if (encryption_init_info->num_key_ids != 0) {
    Deposit(htobe32(encryption_init_info->num_key_ids), &p);
    for (uint32_t i = 0; i < encryption_init_info->num_key_ids; ++i) {
      FX_CHECK(encryption_init_info->key_ids[i]);
      Deposit(encryption_init_info->key_ids[i], kKeyIdSize, &p);
    }
  }

  // Deposit the data size and data.
  Deposit(htobe32(encryption_init_info->data_size), &p);
  Deposit(encryption_init_info->data, encryption_init_info->data_size, &p);

  FX_CHECK(p == result.data() + box_size);

  av_encryption_init_info_free(encryption_init_info);

  return result;
}

std::unique_ptr<fmlib::Encryption> EncryptionFromStream(const AVStream& from) {
  auto parameters = EncryptionParametersFromStream(from);
  if (!parameters) {
    return nullptr;
  }

  // TODO(dalesat): complete this.
  return std::make_unique<fmlib::Encryption>("TODO(dalesat): scheme", nullptr,
                                             std::move(parameters), nullptr);
}

// Converts an AVSampleFormat into an fuchsia::mediastreams::AudioSampleFormat.
fuchsia::mediastreams::AudioSampleFormat Convert(AVSampleFormat av_sample_format,
                                                 AVCodecID codec_id) {
  FX_CHECK(av_sample_format != AV_SAMPLE_FMT_NONE);
  switch (av_sample_format) {
    case AV_SAMPLE_FMT_U8:
    case AV_SAMPLE_FMT_U8P:
      return fuchsia::mediastreams::AudioSampleFormat::UNSIGNED_8;
    case AV_SAMPLE_FMT_S16:
    case AV_SAMPLE_FMT_S16P:
      return fuchsia::mediastreams::AudioSampleFormat::SIGNED_16;
    case AV_SAMPLE_FMT_S32:
    case AV_SAMPLE_FMT_S32P:
      return codec_id == AV_CODEC_ID_PCM_S32LE
                 ? fuchsia::mediastreams::AudioSampleFormat::SIGNED_32
                 : fuchsia::mediastreams::AudioSampleFormat::SIGNED_24_IN_32;
    case AV_SAMPLE_FMT_FLT:
    case AV_SAMPLE_FMT_FLTP:
      return fuchsia::mediastreams::AudioSampleFormat::FLOAT;
    case AV_SAMPLE_FMT_DBL:
    case AV_SAMPLE_FMT_DBLP:
    case AV_SAMPLE_FMT_NB:
    default:
      FX_LOGS(ERROR) << "unsupported av_sample_format " << av_sample_format;
      abort();
  }
}

template <typename T>
fidl::VectorPtr<uint8_t> BytesFromExtraData(T& from) {
  if (from.extradata_size == 0) {
    return nullptr;
  }

  FX_CHECK(from.extradata_size > 0);
  std::vector<uint8_t> v(from.extradata_size);
  std::memcpy(v.data(), from.extradata, v.size());
  return fidl::VectorPtr(std::move(v));
}

// Copies a buffer from Bytes into context->extradata. The result is malloc'ed
// and must be freed.
void ExtraDataFromBytes(const fidl::VectorPtr<uint8_t>& bytes, const AvCodecContextPtr& context) {
  if (!bytes.has_value()) {
    context->extradata = nullptr;
    context->extradata_size = 0;
    return;
  }

  size_t byte_count = bytes.value().size();
  uint8_t* copy = reinterpret_cast<uint8_t*>(malloc(byte_count));
  std::memcpy(copy, bytes.value().data(), byte_count);
  context->extradata = copy;
  context->extradata_size = static_cast<int>(byte_count);
}

// Gets the compression type from a codec_id.
const char* CompressionTypeFromCodecId(AVCodecID from) {
  switch (from) {
    case AV_CODEC_ID_AAC:
      return fuchsia::mediastreams::AUDIO_COMPRESSION_AAC;
    case AV_CODEC_ID_AAC_LATM:
      return fuchsia::mediastreams::AUDIO_COMPRESSION_AACLATM;
    case AV_CODEC_ID_AMR_NB:
      return fuchsia::mediastreams::AUDIO_COMPRESSION_AMRNB;
    case AV_CODEC_ID_AMR_WB:
      return fuchsia::mediastreams::AUDIO_COMPRESSION_AMRWB;
    case AV_CODEC_ID_APTX:
      return fuchsia::mediastreams::AUDIO_COMPRESSION_APTX;
    case AV_CODEC_ID_FLAC:
      return fuchsia::mediastreams::AUDIO_COMPRESSION_FLAC;
    case AV_CODEC_ID_GSM_MS:
      return fuchsia::mediastreams::AUDIO_COMPRESSION_GSMMS;
    case AV_CODEC_ID_MP3:
      return fuchsia::mediastreams::AUDIO_COMPRESSION_MP3;
    case AV_CODEC_ID_OPUS:
      return fuchsia::mediastreams::AUDIO_COMPRESSION_OPUS;
    case AV_CODEC_ID_PCM_ALAW:
      return fuchsia::mediastreams::AUDIO_COMPRESSION_PCMALAW;
    case AV_CODEC_ID_PCM_MULAW:
      return fuchsia::mediastreams::AUDIO_COMPRESSION_PCMMULAW;
    case AV_CODEC_ID_SBC:
      return fuchsia::mediastreams::AUDIO_COMPRESSION_SBC;
    case AV_CODEC_ID_VORBIS:
      return fuchsia::mediastreams::AUDIO_COMPRESSION_VORBIS;
    case AV_CODEC_ID_H263:
      return fuchsia::mediastreams::VIDEO_COMPRESSION_H263;
    case AV_CODEC_ID_H264:
      return fuchsia::mediastreams::VIDEO_COMPRESSION_H264;
    case AV_CODEC_ID_MPEG4:
      return fuchsia::mediastreams::VIDEO_COMPRESSION_MPEG4;
    case AV_CODEC_ID_THEORA:
      return fuchsia::mediastreams::VIDEO_COMPRESSION_THEORA;
    case AV_CODEC_ID_VP3:
      return fuchsia::mediastreams::VIDEO_COMPRESSION_VP3;
    case AV_CODEC_ID_VP8:
      return fuchsia::mediastreams::VIDEO_COMPRESSION_VP8;
    case AV_CODEC_ID_VP9:
      return fuchsia::mediastreams::VIDEO_COMPRESSION_VP9;
    default:
      FX_LOGS(WARNING) << "unsupported codec_id " << avcodec_get_name(from);
      return kUnsupportedCodecIdCompressionType;
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
    case AV_CODEC_ID_PCM_S32BE:
    case AV_CODEC_ID_PCM_S32LE:
    case AV_CODEC_ID_PCM_U8:
      return true;
    default:
      return false;
  }
}

std::unique_ptr<fmlib::Compression> CompressionFromCodecContext(const AVCodecContext& from) {
  if (from.codec != nullptr || IsLpcm(from.codec_id)) {
    return nullptr;
  }

  return std::make_unique<fmlib::Compression>(CompressionTypeFromCodecId(from.codec_id),
                                              BytesFromExtraData(from));
}

std::unique_ptr<fmlib::Compression> CompressionFromCodecParameters(const AVCodecParameters& from) {
  if (IsLpcm(from.codec_id)) {
    return nullptr;
  }

  return std::make_unique<fmlib::Compression>(CompressionTypeFromCodecId(from.codec_id),
                                              BytesFromExtraData(from));
}

// Creates an audio format and compression from an AVCodecContext describing an audio stream.
fmlib::AudioFormat AudioFormatFromCodecContext(const AVCodecContext& from) {
  return fmlib::AudioFormat(
      Convert(from.sample_fmt, from.codec_id), static_cast<uint32_t>(from.channels),
      static_cast<uint32_t>(from.sample_rate), CompressionFromCodecContext(from));
}

// Creates an audio format, compression and encryption from an AVStream describing an audio stream.
fmlib::AudioFormat AudioFormatFromStream(const AVStream& from) {
  FX_CHECK(from.codecpar);
  auto& codecpar = *from.codecpar;
  return fmlib::AudioFormat(
      Convert(static_cast<AVSampleFormat>(codecpar.format), codecpar.codec_id),
      static_cast<uint32_t>(codecpar.channels), static_cast<uint32_t>(codecpar.sample_rate),
      CompressionFromCodecParameters(codecpar), EncryptionFromStream(from));
}

// Converts AVColorSpace and AVColorRange to ColorSpace.
fuchsia::mediastreams::ColorSpace ColorSpaceFromAVColorSpaceAndRange(AVColorSpace color_space,
                                                                     AVColorRange color_range) {
  switch (color_space) {
    case AVCOL_SPC_UNSPECIFIED:
    case AVCOL_SPC_BT709:
      return fuchsia::mediastreams::ColorSpace::REC709;
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_BT470BG:
      return fuchsia::mediastreams::ColorSpace::REC601_NTSC;
    default:
      return fuchsia::mediastreams::ColorSpace::INVALID;
  }
}

// Creates a video format from an AVCodecContext describing a video type.
fmlib::VideoFormat VideoFormatFromCodecContext(const AVCodecContext& from) {
  int coded_width = from.coded_width;
  int coded_height = from.coded_height;
  avcodec_align_dimensions(const_cast<AVCodecContext*>(&from), &coded_width, &coded_height);
  FX_CHECK(coded_width >= from.coded_width);
  FX_CHECK(coded_height >= from.coded_height);

  fuchsia::math::SizePtr aspect_ratio;
  if (from.sample_aspect_ratio.num != 0 && from.sample_aspect_ratio.den != 0) {
    aspect_ratio = std::make_unique<fuchsia::math::Size>();
    aspect_ratio->width = from.sample_aspect_ratio.num;
    aspect_ratio->height = from.sample_aspect_ratio.den;
  }

  return fmlib::VideoFormat(PixelFormatFromAVPixelFormat(from.pix_fmt),
                            ColorSpaceFromAVColorSpaceAndRange(from.colorspace, from.color_range),
                            {.width = coded_width, .height = coded_height},
                            {.width = from.width, .height = from.height}, std::move(aspect_ratio),
                            CompressionFromCodecContext(from));
}

// Creates a video format, compression and encryption from an AVStream describing an video stream.
fmlib::VideoFormat VideoFormatFromStream(const AVStream& from) {
  const AVCodecParameters parameters = *from.codecpar;

  fuchsia::math::SizePtr aspect_ratio;
  if (from.sample_aspect_ratio.num != 0 && from.sample_aspect_ratio.den != 0) {
    aspect_ratio = std::make_unique<fuchsia::math::Size>();
    aspect_ratio->width = from.sample_aspect_ratio.num;
    aspect_ratio->height = from.sample_aspect_ratio.den;
  } else if (parameters.sample_aspect_ratio.num != 0 && parameters.sample_aspect_ratio.den != 0) {
    aspect_ratio = std::make_unique<fuchsia::math::Size>();
    aspect_ratio->width = parameters.sample_aspect_ratio.num;
    aspect_ratio->height = parameters.sample_aspect_ratio.den;
  }

  return fmlib::VideoFormat(
      PixelFormatFromAVPixelFormat(static_cast<AVPixelFormat>(parameters.format)),
      ColorSpaceFromAVColorSpaceAndRange(parameters.color_space, parameters.color_range),
      {.width = parameters.width, .height = parameters.height},
      {.width = parameters.width, .height = parameters.height}, std::move(aspect_ratio),
      CompressionFromCodecParameters(parameters), EncryptionFromStream(from));
}

// Creates an AVCodecContext from an AudioStreamType.
AvCodecContextPtr AVCodecContextFromAudioFormat(const fmlib::AudioFormat& format) {
  if (format.is_encrypted()) {
    // Can't create AVCodecContext for encrypted streams. Don't see a need for this.
    return nullptr;
  }

  AVCodecID codec_id;
  AVSampleFormat sample_format = AV_SAMPLE_FMT_NONE;

  if (!format.is_compressed()) {
    switch (format.sample_format()) {
      case fuchsia::mediastreams::AudioSampleFormat::UNSIGNED_8:
        codec_id = AV_CODEC_ID_PCM_U8;
        sample_format = AV_SAMPLE_FMT_U8;
        break;
      case fuchsia::mediastreams::AudioSampleFormat::SIGNED_16:
        codec_id = AV_CODEC_ID_PCM_S16LE;
        sample_format = AV_SAMPLE_FMT_S16;
        break;
      case fuchsia::mediastreams::AudioSampleFormat::SIGNED_24_IN_32:
        codec_id = AV_CODEC_ID_PCM_S24LE;
        sample_format = AV_SAMPLE_FMT_S32;
        break;
      case fuchsia::mediastreams::AudioSampleFormat::SIGNED_32:
        codec_id = AV_CODEC_ID_PCM_S32LE;
        sample_format = AV_SAMPLE_FMT_S32;
        break;
      case fuchsia::mediastreams::AudioSampleFormat::FLOAT:
        codec_id = AV_CODEC_ID_PCM_F32LE;
        sample_format = AV_SAMPLE_FMT_FLT;
        break;
    }
  } else if (format.compression().type() == fuchsia::mediastreams::AUDIO_COMPRESSION_AAC) {
    codec_id = AV_CODEC_ID_AAC;
  } else if (format.compression().type() == fuchsia::mediastreams::AUDIO_COMPRESSION_AACLATM) {
    codec_id = AV_CODEC_ID_AAC_LATM;
  } else if (format.compression().type() == fuchsia::mediastreams::AUDIO_COMPRESSION_AMRNB) {
    codec_id = AV_CODEC_ID_AMR_NB;
  } else if (format.compression().type() == fuchsia::mediastreams::AUDIO_COMPRESSION_AMRWB) {
    codec_id = AV_CODEC_ID_AMR_WB;
  } else if (format.compression().type() == fuchsia::mediastreams::AUDIO_COMPRESSION_APTX) {
    codec_id = AV_CODEC_ID_APTX;
  } else if (format.compression().type() == fuchsia::mediastreams::AUDIO_COMPRESSION_FLAC) {
    codec_id = AV_CODEC_ID_FLAC;
  } else if (format.compression().type() == fuchsia::mediastreams::AUDIO_COMPRESSION_GSMMS) {
    codec_id = AV_CODEC_ID_GSM_MS;
  } else if (format.compression().type() == fuchsia::mediastreams::AUDIO_COMPRESSION_MP3) {
    codec_id = AV_CODEC_ID_MP3;
  } else if (format.compression().type() == fuchsia::mediastreams::AUDIO_COMPRESSION_OPUS) {
    codec_id = AV_CODEC_ID_OPUS;
  } else if (format.compression().type() == fuchsia::mediastreams::AUDIO_COMPRESSION_PCMALAW) {
    codec_id = AV_CODEC_ID_PCM_ALAW;
  } else if (format.compression().type() == fuchsia::mediastreams::AUDIO_COMPRESSION_PCMMULAW) {
    codec_id = AV_CODEC_ID_PCM_MULAW;
  } else if (format.compression().type() == fuchsia::mediastreams::AUDIO_COMPRESSION_SBC) {
    codec_id = AV_CODEC_ID_SBC;
    switch (format.sample_format()) {
      case fuchsia::mediastreams::AudioSampleFormat::UNSIGNED_8:
        sample_format = AV_SAMPLE_FMT_U8P;
        break;
      case fuchsia::mediastreams::AudioSampleFormat::SIGNED_16:
        sample_format = AV_SAMPLE_FMT_S16P;
        break;
      case fuchsia::mediastreams::AudioSampleFormat::SIGNED_24_IN_32:
      case fuchsia::mediastreams::AudioSampleFormat::SIGNED_32:
        sample_format = AV_SAMPLE_FMT_S32P;
        break;
      case fuchsia::mediastreams::AudioSampleFormat::FLOAT:
        sample_format = AV_SAMPLE_FMT_FLTP;
        break;
    }
  } else if (format.compression().type() == fuchsia::mediastreams::AUDIO_COMPRESSION_VORBIS) {
    codec_id = AV_CODEC_ID_VORBIS;
  } else {
    FX_LOGS(WARNING) << "unsupported compression " << format.compression().type();
    return nullptr;
  }

  AvCodecContextPtr context(avcodec_alloc_context3(nullptr));

  context->codec_type = AVMEDIA_TYPE_AUDIO;
  context->codec_id = codec_id;
  context->sample_fmt = sample_format;
  context->channels = static_cast<int>(format.channel_count());
  context->sample_rate = static_cast<int>(format.frames_per_second());

  if (format.is_compressed() && format.compression().parameters().has_value()) {
    ExtraDataFromBytes(format.compression().parameters().value(), context);
  }

  return context;
}

// Creates an AVCodecContext from a VideoFormat.
AvCodecContextPtr AVCodecContextFromVideoFormat(const fmlib::VideoFormat& format) {
  if (!format.is_compressed()) {
    return nullptr;
  }

  if (format.is_encrypted()) {
    // Can't create AVCodecContext for encrypted streams. Don't see a need for this.
    return nullptr;
  }

  AVCodecID codec_id;

  if (format.compression().type() == fuchsia::mediastreams::VIDEO_COMPRESSION_H263) {
    codec_id = AV_CODEC_ID_H263;
  } else if (format.compression().type() == fuchsia::mediastreams::VIDEO_COMPRESSION_H264) {
    codec_id = AV_CODEC_ID_H264;
  } else if (format.compression().type() == fuchsia::mediastreams::VIDEO_COMPRESSION_MPEG4) {
    codec_id = AV_CODEC_ID_MPEG4;
  } else if (format.compression().type() == fuchsia::mediastreams::VIDEO_COMPRESSION_THEORA) {
    codec_id = AV_CODEC_ID_THEORA;
  } else if (format.compression().type() == fuchsia::mediastreams::VIDEO_COMPRESSION_VP3) {
    codec_id = AV_CODEC_ID_VP3;
  } else if (format.compression().type() == fuchsia::mediastreams::VIDEO_COMPRESSION_VP8) {
    codec_id = AV_CODEC_ID_VP8;
  } else if (format.compression().type() == fuchsia::mediastreams::VIDEO_COMPRESSION_VP9) {
    codec_id = AV_CODEC_ID_VP9;
  } else {
    FX_LOGS(WARNING) << "unsupported compression " << format.compression().type();
    return nullptr;
  }

  AvCodecContextPtr context(avcodec_alloc_context3(nullptr));

  context->codec_type = AVMEDIA_TYPE_VIDEO;
  context->codec_id = codec_id;
  context->pix_fmt = AVPixelFormatFromPixelFormat(format.pixel_format());
  context->coded_width = format.coded_size().width;
  context->coded_height = format.coded_size().height;
  context->sample_aspect_ratio.num = format.aspect_ratio() ? format.aspect_ratio()->width : 0;
  context->sample_aspect_ratio.den = format.aspect_ratio() ? format.aspect_ratio()->height : 0;

  if (format.is_compressed() && format.compression().parameters().has_value()) {
    ExtraDataFromBytes(format.compression().parameters().value(), context);
  }

  return context;
}

// static
std::vector<std::string> GetCompressionTypes(bool encoder, AVMediaType type) {
  std::vector<std::string> result;

  AVCodec* codec = nullptr;
  while ((codec = av_codec_next(codec)) != nullptr) {
    if (!(encoder ? av_codec_is_encoder(codec) : av_codec_is_decoder(codec)) ||
        codec->type != type) {
      continue;
    }

    // We currently don't use any of the PCM 'decoders' currently in the config.
    // TODO(dalesat): Remove these from the config or use them.
    switch (codec->id) {
      case AV_CODEC_ID_PCM_F32LE:
      case AV_CODEC_ID_PCM_S16BE:
      case AV_CODEC_ID_PCM_S16LE:
      case AV_CODEC_ID_PCM_S24BE:
      case AV_CODEC_ID_PCM_S24LE:
      case AV_CODEC_ID_PCM_S32LE:
      case AV_CODEC_ID_PCM_U8:
        continue;
      default:
        break;
    }

    auto compression_type = CompressionTypeFromCodecId(codec->id);
    if (compression_type == kUnsupportedCodecIdCompressionType) {
      FX_LOGS(WARNING) << "found " << (encoder ? "encoder" : "decoder")
                       << " with unsupported codec id " << codec->id << ", name " << codec->name
                       << ", long name " << codec->long_name;
      continue;
    }

    result.push_back(compression_type);
  }

  return result;
}

}  // namespace

fuchsia::mediastreams::PixelFormat PixelFormatFromAVPixelFormat(AVPixelFormat av_pixel_format) {
  switch (av_pixel_format) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
      return fuchsia::mediastreams::PixelFormat::I420;
    default:
      return fuchsia::mediastreams::PixelFormat::INVALID;
  }
}

AVPixelFormat AVPixelFormatFromPixelFormat(fuchsia::mediastreams::PixelFormat pixel_format) {
  switch (pixel_format) {
    case fuchsia::mediastreams::PixelFormat::I420:
      return AV_PIX_FMT_YUV420P;
    default:
      return AV_PIX_FMT_NONE;
  }
}

// static
fmlib::MediaFormat AvCodecContext::GetMediaFormat(const AVCodecContext& from) {
  switch (from.codec_type) {
    case AVMEDIA_TYPE_AUDIO:
      return fmlib::MediaFormat(AudioFormatFromCodecContext(from));
    case AVMEDIA_TYPE_VIDEO:
      return fmlib::MediaFormat(VideoFormatFromCodecContext(from));
    case AVMEDIA_TYPE_UNKNOWN:
    case AVMEDIA_TYPE_DATA:
    case AVMEDIA_TYPE_SUBTITLE:
    case AVMEDIA_TYPE_ATTACHMENT:
    case AVMEDIA_TYPE_NB:
    default:
      FX_LOGS(ERROR) << "unsupported code type " << from.codec_type;
      abort();
  }
}

// static
fmlib::MediaFormat AvCodecContext::GetMediaFormat(const AVStream& from) {
  FX_CHECK(from.codecpar);
  switch (from.codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
      return fmlib::MediaFormat(AudioFormatFromStream(from));
    case AVMEDIA_TYPE_VIDEO:
      return fmlib::MediaFormat(VideoFormatFromStream(from));
    case AVMEDIA_TYPE_UNKNOWN:
    case AVMEDIA_TYPE_DATA:
    case AVMEDIA_TYPE_SUBTITLE:
    case AVMEDIA_TYPE_ATTACHMENT:
    case AVMEDIA_TYPE_NB:
    default:
      FX_LOGS(ERROR) << "unsupported code type " << from.codecpar->codec_type;
      abort();
  }
}

// static
AvCodecContextPtr AvCodecContext::Create(const fmlib::MediaFormat& format) {
  InitFfmpeg();

  switch (format.Which()) {
    case fuchsia::mediastreams::MediaFormat::Tag::kAudio:
      return AVCodecContextFromAudioFormat(format.audio());
    case fuchsia::mediastreams::MediaFormat::Tag::kVideo:
      return AVCodecContextFromVideoFormat(format.video());
    default:
      return nullptr;
  }
}

// static
AvCodecContextPtr AvCodecContext::Create(const fmlib::AudioFormat& format) {
  InitFfmpeg();
  return AVCodecContextFromAudioFormat(format);
}

// static
AvCodecContextPtr AvCodecContext::Create(const fmlib::VideoFormat& format) {
  InitFfmpeg();
  return AVCodecContextFromVideoFormat(format);
}

// static
std::vector<std::string> AvCodecContext::GetAudioDecoderCompressionTypes() {
  return GetCompressionTypes(/* encoder */ false, AVMEDIA_TYPE_AUDIO);
}

// static
std::vector<std::string> AvCodecContext::GetVideoDecoderCompressionTypes() {
  return GetCompressionTypes(/* encoder */ false, AVMEDIA_TYPE_VIDEO);
}

// static
std::vector<std::string> AvCodecContext::GetAudioEncoderCompressionTypes() {
  return GetCompressionTypes(/* encoder */ true, AVMEDIA_TYPE_AUDIO);
}

// static
std::vector<std::string> AvCodecContext::GetVideoEncoderCompressionTypes() {
  return GetCompressionTypes(/* encoder */ true, AVMEDIA_TYPE_VIDEO);
}

}  // namespace fmlib

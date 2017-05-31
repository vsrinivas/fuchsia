// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ostream>

#include "apps/media/src/ffmpeg/ffmpeg_formatting.h"

extern "C" {
#include "third_party/ffmpeg/libavformat/avformat.h"
#include "third_party/ffmpeg/libavformat/internal.h"
#include "third_party/ffmpeg/libavutil/dict.h"
}

namespace media {

const char* safe(const char* s) {
  return s == nullptr ? "<nullptr>" : s;
}

std::ostream& operator<<(std::ostream& os,
                         const struct AVCodecTag* const* value) {
  if (value == nullptr) {
    return os << "<nullptr>\n";
  } else if (*value == nullptr) {
    return os << "&<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  os << begl << "AVCodecID id: " << (*value)->id << "\n";
  os << begl << "unsigned int tag: " << (*value)->tag << "\n";
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVInputFormat* value) {
  if (value == nullptr) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  os << begl << "const char *name: " << value->name << "\n";
  os << begl << "const char *long_name: " << value->long_name << "\n";
  os << begl << "int flags: " << AVFMTFlags(value->flags);
  os << begl << "const char *extensions: " << safe(value->extensions) << "\n";
  os << begl << "const AVCodecTag * const *codec_tag: " << value->codec_tag;
  os << begl << "const char *mime_type: " << safe(value->mime_type) << "\n";
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVOutputFormat* value) {
  if (value == nullptr) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  os << begl << "const char *name: " << safe(value->name) << "\n";
  os << begl << "const char *long_name: " << safe(value->long_name) << "\n";
  os << begl << "const char *mime_type: " << safe(value->mime_type) << "\n";
  os << begl << "const char *extensions: " << safe(value->extensions) << "\n";
  os << begl << "AVCodecID audio_codec: " << value->audio_codec;
  os << begl << "AVCodecID video_codec: " << value->video_codec;
  os << begl << "AVCodecID subtitle_codec: " << value->subtitle_codec;
  os << begl << "int flags: " << AVFMTFlags(value->flags);
  os << begl << "const AVCodecTag * const *codec_tag: " << value->codec_tag;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVIOContext* value) {
  if (value == nullptr) {
    return os << "<nullptr>\n";
  } else {
    return os << "TODO\n";
  }
}

std::ostream& operator<<(std::ostream& os, AVFMTCTXFlags value) {
  if (value.flags_ == 0) {
    return os << "<none>\n";
  }

  if (value.flags_ & AVFMTCTX_NOHEADER) {
    return os << "AVFMTCTX_NOHEADER\n";
  } else {
    return os << "<UNKNOWN AVFMTCTX_: " << value.flags_ << ">\n";
  }
}

std::ostream& operator<<(std::ostream& os, const AVRational* value) {
  if (value == nullptr) {
    return os << "<none>\n";
  } else {
    os << "\n";
  }

  os << indent;
  for (int index = 0; value->num != 0 || value->den != 0; ++value, ++index) {
    os << begl << "[" << index << "]: " << *value;
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const int* value) {
  if (value == nullptr) {
    return os << "<none>\n";
  } else {
    os << "\n";
  }

  os << indent;
  for (int index = 0; *value != 0; ++value, ++index) {
    os << begl << "[" << index << "]: " << *value << "\n";
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const uint64_t* value) {
  if (value == nullptr) {
    return os << "<none>\n";
  } else {
    os << "\n";
  }

  os << indent;
  for (int index = 0; *value != 0; ++value, ++index) {
    os << begl << "[" << index << "]: " << *value << "\n";
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVSampleFormat* value) {
  if (value == nullptr) {
    return os << "<none>\n";
  } else {
    os << "\n";
  }

  os << indent;
  for (int index = 0; int(*value) != 0; ++value, ++index) {
    os << begl << "[" << index << "]: " << *value;
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVCodec* value) {
  if (value == nullptr) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  os << begl << "const char *name: " << safe(value->name) << "\n";
  os << begl << "const char *long_name: " << safe(value->long_name) << "\n";
  os << begl << "AVMediaType type: " << value->type;
  os << begl << "AVCodecID id: " << value->id;
  os << begl << "int capabilities: " << value->capabilities << "\n";
  os << begl
     << "AVRational *supported_framerates: " << value->supported_framerates;
  os << begl
     << "const int *supported_samplerates: " << value->supported_samplerates;
  os << begl << "const AVSampleFormat *sample_fmts: " << value->sample_fmts;
  os << begl << "const uint64_t *channel_layouts: " << value->channel_layouts;

  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVCodecContext* value) {
  if (value == nullptr) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  os << begl << "AVMediaType codec_type: " << value->codec_type;
  os << begl << "const struct AVCodec *codec: " << value->codec;
  os << begl << "AVCodecID codec_id: " << value->codec_id;
  os << begl << "int bit_rate: " << value->bit_rate << "\n";
  os << begl << "int extradata_size: " << value->extradata_size << "\n";
  os << begl << "int width: " << value->width << "\n";
  os << begl << "int height: " << value->height << "\n";
  os << begl << "int coded_width: " << value->coded_width << "\n";
  os << begl << "int coded_height: " << value->coded_height << "\n";
  os << begl << "int gop_size: " << value->gop_size << "\n";
  os << begl << "int sample_rate: " << value->sample_rate << "\n";
  os << begl << "int channels: " << value->channels << "\n";
  os << begl << "AVSampleFormat sample_fmt: " << value->sample_fmt;
  os << begl << "int frame_size: " << value->frame_size << "\n";
  os << begl << "int frame_number: " << value->frame_number << "\n";
  os << begl << "int block_align: " << value->block_align << "\n";
  os << begl << "int cutoff: " << value->cutoff << "\n";
  os << begl << "uint64_t channel_layout: " << value->channel_layout << "\n";
  os << begl
     << "uint64_t request_channel_layout: " << value->request_channel_layout
     << "\n";
  os << begl
     << "AVAudioServiceType audio_service_type: " << value->audio_service_type
     << "\n";
  os << begl
     << "AVSampleFormat request_sample_fmt: " << value->request_sample_fmt;
  os << begl << "int profile: " << value->profile << "\n";
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVRational& value) {
  return os << value.num << "/" << value.den << "\n";
}

std::ostream& operator<<(std::ostream& os, const AVStream* value) {
  if (value == nullptr) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  os << begl << "int index: " << value->index << "\n";
  os << begl << "int id: " << value->id << "\n";
  os << begl << "AVRational time_base: " << value->time_base;
  os << begl << "int64_t start_time: " << value->start_time << "\n";
  os << begl << "int64_t duration: " << value->duration << "\n";
  os << begl << "int64_t nb_frames: " << value->nb_frames << "\n";
  os << begl << "int disposition: " << AV_DISPOSITIONFlags(value->disposition);
  os << begl << "AVDiscard discard: " << value->discard;
  os << begl
     << "AVRational sample_aspect_ratio: " << value->sample_aspect_ratio;
  os << begl << "AVDictionary *metadata: " << value->metadata;
  os << begl << "AVRational avg_frame_rate: " << value->avg_frame_rate;
  os << begl << "AVPacket attached_pic: " << &value->attached_pic;
  os << begl << "int nb_side_data: " << value->nb_side_data << "\n";
  os << begl << "AVPacketSideData side_data: "
     << AVPacketSideDataArray(value->side_data, value->nb_side_data);
  os << begl << "int event_flags: " << AVSTREAM_EVENTFlags(value->event_flags);
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVStreamArray& value) {
  if (value.items_ == nullptr) {
    return os << "<nullptr>\n";
  } else if (value.count_ == 0) {
    return os << "<empty>\n";
  } else {
    os << "\n";
  }

  os << indent;
  for (unsigned int i = 0; i < value.count_; i++) {
    os << begl << "[" << i << "] " << value.items_[i];
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, AVFMTFlags value) {
  if (value.flags_ == 0) {
    os << "<none>\n";
    return os;
  } else {
    os << "\n";
  }

  os << indent;
  if (value.flags_ & AVFMT_FLAG_GENPTS) {
    os << begl << "AVFMT_FLAG_GENPTS\n";
  }
  if (value.flags_ & AVFMT_FLAG_IGNIDX) {
    os << begl << "AVFMT_FLAG_IGNIDX\n";
  }
  if (value.flags_ & AVFMT_FLAG_NONBLOCK) {
    os << begl << "AVFMT_FLAG_NONBLOCK\n";
  }
  if (value.flags_ & AVFMT_FLAG_IGNDTS) {
    os << begl << "AVFMT_FLAG_IGNDTS\n";
  }
  if (value.flags_ & AVFMT_FLAG_NOFILLIN) {
    os << begl << "AVFMT_FLAG_NOFILLIN\n";
  }
  if (value.flags_ & AVFMT_FLAG_NOPARSE) {
    os << begl << "AVFMT_FLAG_NOPARSE\n";
  }
  if (value.flags_ & AVFMT_FLAG_NOBUFFER) {
    os << begl << "AVFMT_FLAG_NOBUFFER\n";
  }
  if (value.flags_ & AVFMT_FLAG_CUSTOM_IO) {
    os << begl << "AVFMT_FLAG_CUSTOM_IO\n";
  }
  if (value.flags_ & AVFMT_FLAG_DISCARD_CORRUPT) {
    os << begl << "AVFMT_FLAG_DISCARD_CORRUPT\n";
  }
  if (value.flags_ & AVFMT_FLAG_FLUSH_PACKETS) {
    os << begl << "AVFMT_FLAG_FLUSH_PACKETS\n";
  }
  if (value.flags_ & AVFMT_FLAG_BITEXACT) {
    os << begl << "AVFMT_FLAG_BITEXACT\n";
  }
  if (value.flags_ & AVFMT_FLAG_MP4A_LATM) {
    os << begl << "AVFMT_FLAG_MP4A_LATM\n";
  }
  if (value.flags_ & AVFMT_FLAG_SORT_DTS) {
    os << begl << "AVFMT_FLAG_SORT_DTS\n";
  }
  if (value.flags_ & AVFMT_FLAG_PRIV_OPT) {
    os << begl << "AVFMT_FLAG_PRIV_OPT\n";
  }
  if (value.flags_ & AVFMT_FLAG_KEEP_SIDE_DATA) {
    os << begl << "AVFMT_FLAG_KEEP_SIDE_DATA\n";
  }
  if (value.flags_ & AVFMT_FLAG_FAST_SEEK) {
    os << begl << "AVFMT_FLAG_FAST_SEEK\n";
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, AV_DISPOSITIONFlags value) {
  if (value.flags_ == 0) {
    os << "<none>\n";
    return os;
  } else {
    os << "\n";
  }

  os << indent;
  if (value.flags_ & AV_DISPOSITION_DEFAULT) {
    os << begl << "AV_DISPOSITION_DEFAULT  0x0001\n";
  }
  if (value.flags_ & AV_DISPOSITION_DUB) {
    os << begl << "AV_DISPOSITION_DUB      0x0002\n";
  }
  if (value.flags_ & AV_DISPOSITION_ORIGINAL) {
    os << begl << "AV_DISPOSITION_ORIGINAL 0x0004\n";
  }
  if (value.flags_ & AV_DISPOSITION_COMMENT) {
    os << begl << "AV_DISPOSITION_COMMENT  0x0008\n";
  }
  if (value.flags_ & AV_DISPOSITION_LYRICS) {
    os << begl << "AV_DISPOSITION_LYRICS   0x0010\n";
  }
  if (value.flags_ & AV_DISPOSITION_KARAOKE) {
    os << begl << "AV_DISPOSITION_KARAOKE  0x0020\n";
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVBufferRef* value) {
  if (value == nullptr) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  os << begl << "AVBuffer *buffer: "
     << (value->buffer == nullptr ? "<nullptr>" : "TODO") << "\n";
  os << begl
     << "uint8_t *data: " << (value->data == nullptr ? "<nullptr>" : "<opaque>")
     << "\n";
  os << begl << "int size: " << value->size << "\n";
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVFrame* value) {
  if (value == nullptr) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  os << begl << "uint8_t *data[AV_NUM_DATA_POINTERS]: ";
  {
    os << indent;
    bool any = false;
    for (int i = 0; i < AV_NUM_DATA_POINTERS; i++) {
      if (value->data[i] != nullptr) {
        if (!any) {
          any = true;
          os << "\n";
        }
        os << begl << "[" << i << "]: <opaque>\n";
      }
    }
    if (!any) {
      os << "<all nullptr>\n";
    }
    os << outdent;
  }

  os << begl << "int linesize[AV_NUM_DATA_POINTERS]: ";
  {
    os << indent;
    bool any = false;
    for (int i = 0; i < AV_NUM_DATA_POINTERS; i++) {
      if (value->linesize[i] != 0) {
        if (!any) {
          any = true;
          os << "\n";
        }
        os << begl << "[" << i << "]: " << value->linesize[i] << "\n";
      }
    }
    if (!any) {
      os << "<all zero>\n";
    }
    os << outdent;
  }

  os << begl << "uint8_t **extended_data: "
     << (value->extended_data == nullptr ? "<nullptr>" : "<opaque>") << "\n";
  os << begl << "int width: " << value->width << "\n";
  os << begl << "int height: " << value->height << "\n";
  os << begl << "int nb_samples: " << value->nb_samples << "\n";
  os << begl << "int format: " << value->format << "\n";
  os << begl << "int key_frame: " << value->key_frame << "\n";
  os << begl << "int64_t pts: " << value->pts << "\n";
  os << begl << "int64_t pkt_dts: " << value->pkt_dts << "\n";
  os << begl << "int sample_rate: " << value->sample_rate << "\n";
  os << begl << "AVBufferRef *buf[AV_NUM_DATA_POINTERS]: ";
  {
    os << indent;
    bool any = false;
    for (int i = 0; i < AV_NUM_DATA_POINTERS; i++) {
      if (value->buf[i] != nullptr) {
        if (!any) {
          any = true;
          os << "\n";
        }
        os << begl << "[" << i << "]:" << value->buf[i];
      }
    }
    if (!any) {
      os << "<all nullptr>\n";
    }
    os << outdent;
  }
  os << begl << "int channels: " << value->channels << "\n";
  os << begl << "int pkt_size: " << value->pkt_size << "\n";
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVPacket* value) {
  if (value == nullptr) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  os << begl << "AVBufferRef *buf: " << value->buf;
  os << begl << "int64_t pts: " << value->pts << "\n";
  os << begl << "int64_t dts: " << value->dts << "\n";
  os << begl
     << "uint8_t *data: " << (value->data == nullptr ? "<nullptr>" : "<opaque>")
     << "\n";
  os << begl << "int size: " << value->size << "\n";
  os << begl << "int stream_index: " << value->stream_index << "\n";
  os << begl << "int flags: " << value->flags << "\n";
  os << begl << "AVPacketSideData *side_data: " << value->side_data;
  os << begl << "int side_data_elems: " << value->side_data_elems << "\n";
  os << begl << "int duration: " << value->duration << "\n";
  os << begl << "int64_t pos: " << value->pos << "\n";
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVPacketSideData* value) {
  if (value == nullptr) {
    return os << "<nullptr>\n";
  } else {
    return os << "TODO\n";
  }
}

std::ostream& operator<<(std::ostream& os, const AVPacketSideDataArray& value) {
  if (value.items_ == nullptr) {
    return os << "<nullptr>\n";
  } else if (value.count_ == 0) {
    return os << "<empty>\n";
  } else {
    os << "\n";
  }

  os << indent;
  for (unsigned int i = 0; i < value.count_; i++) {
    os << begl << "[" << i << "] " << &value.items_[i];
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVProgram* value) {
  if (value == nullptr) {
    return os << "<nullptr>\n";
  } else {
    return os << "TODO\n";
  }
}

std::ostream& operator<<(std::ostream& os, const AVProgramArray& value) {
  if (value.items_ == nullptr) {
    return os << "<nullptr>\n";
  } else if (value.count_ == 0) {
    return os << "<empty>\n";
  } else {
    os << "\n";
  }

  os << indent;
  for (unsigned int i = 0; i < value.count_; i++) {
    os << begl << "[" << i << "]" << value.items_[i];
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVChapter* value) {
  if (value == nullptr) {
    return os << "<nullptr>\n";
  } else {
    return os << "TODO\n";
  }
}

std::ostream& operator<<(std::ostream& os, const AVChapterArray& value) {
  if (value.items_ == nullptr) {
    return os << "<nullptr>\n";
  } else if (value.count_ == 0) {
    return os << "<empty>\n";
  } else {
    os << "\n";
  }

  os << indent;
  for (unsigned int i = 0; i < value.count_; i++) {
    os << begl << "[" << i << "]" << value.items_[i];
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, AVCodecID value) {
  return os << avcodec_get_name(value) << " (" << static_cast<int>(value) << ")"
            << "\n";
}

std::ostream& operator<<(std::ostream& os, const AVDictionary* value) {
  if (value == nullptr) {
    return os << "<nullptr>\n";
  }
  AVDictionaryEntry* entry =
      av_dict_get(value, "", nullptr, AV_DICT_IGNORE_SUFFIX);
  if (entry == nullptr) {
    return os << "<empty>\n";
  }
  os << "\n";

  os << indent;
  while (entry != nullptr) {
    os << begl << safe(entry->key) << ": " << safe(entry->value) << "\n";
    entry = av_dict_get(value, "", entry, AV_DICT_IGNORE_SUFFIX);
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, AVFMT_EVENTFlags value) {
  if (value.flags_ == 0) {
    os << "<none>\n";
    return os;
  }

  if (value.flags_ & AVFMT_EVENT_FLAG_METADATA_UPDATED) {
    return os << "AVFMT_EVENT_FLAG_METADATA_UPDATED\n";
  } else {
    return os << "<UNKNOWN AVFMT_EVENT_FLAG_: " << value.flags_ << ">"
              << "\n";
  }
}

std::ostream& operator<<(std::ostream& os, AVSTREAM_EVENTFlags value) {
  if (value.flags_ == 0) {
    os << "<none>\n";
    return os;
  }

  if (value.flags_ & AVSTREAM_EVENT_FLAG_METADATA_UPDATED) {
    return os << "AVSTREAM_EVENT_FLAG_METADATA_UPDATED\n";
  } else {
    return os << "<UNKNOWN AVSTREAM_EVENT_FLAG_: " << value.flags_ << ">"
              << "\n";
  }
}

std::ostream& operator<<(std::ostream& os, AVFMT_AVOID_NEG_TSFlags value) {
  switch (value.flags_) {
    case AVFMT_AVOID_NEG_TS_AUTO:
      return os << "AVFMT_AVOID_NEG_TS_AUTO\n";
    case AVFMT_AVOID_NEG_TS_MAKE_NON_NEGATIVE:
      return os << "AVFMT_AVOID_NEG_TS_MAKE_NON_NEGATIVE\n";
    case AVFMT_AVOID_NEG_TS_MAKE_ZERO:
      return os << "AVFMT_AVOID_NEG_TS_MAKE_ZERO\n";
    default:
      return os << "<UNKNOWN AVFMT_AVOID_NEG_TS_: " << value.flags_ << ">"
                << "\n";
  }
}

std::ostream& operator<<(std::ostream& os, AVMediaType value) {
  switch (value) {
    case AVMEDIA_TYPE_UNKNOWN:
      return os << "AVMEDIA_TYPE_UNKNOWN\n";
    case AVMEDIA_TYPE_VIDEO:
      return os << "AVMEDIA_TYPE_VIDEO\n";
    case AVMEDIA_TYPE_AUDIO:
      return os << "AVMEDIA_TYPE_AUDIO\n";
    case AVMEDIA_TYPE_DATA:
      return os << "AVMEDIA_TYPE_DATA\n";
    case AVMEDIA_TYPE_SUBTITLE:
      return os << "AVMEDIA_TYPE_SUBTITLE\n";
    case AVMEDIA_TYPE_ATTACHMENT:
      return os << "AVMEDIA_TYPE_ATTACHMENT\n";
    case AVMEDIA_TYPE_NB:
      return os << "AVMEDIA_TYPE_NB\n";
    default:
      return os << "<UNKNOWN AVMediaType: " << static_cast<int>(value) << ">"
                << "\n";
  }
}

std::ostream& operator<<(std::ostream& os, AVSampleFormat value) {
  switch (value) {
    case AV_SAMPLE_FMT_NONE:
      return os << "AV_SAMPLE_FMT_NONE\n";
    case AV_SAMPLE_FMT_U8:
      return os << "AV_SAMPLE_FMT_U8\n";
    case AV_SAMPLE_FMT_S16:
      return os << "AV_SAMPLE_FMT_S16\n";
    case AV_SAMPLE_FMT_S32:
      return os << "AV_SAMPLE_FMT_S32\n";
    case AV_SAMPLE_FMT_FLT:
      return os << "AV_SAMPLE_FMT_FLT\n";
    case AV_SAMPLE_FMT_DBL:
      return os << "AV_SAMPLE_FMT_DBL\n";
    case AV_SAMPLE_FMT_U8P:
      return os << "AV_SAMPLE_FMT_U8P\n";
    case AV_SAMPLE_FMT_S16P:
      return os << "AV_SAMPLE_FMT_S16P\n";
    case AV_SAMPLE_FMT_S32P:
      return os << "AV_SAMPLE_FMT_S32P\n";
    case AV_SAMPLE_FMT_FLTP:
      return os << "AV_SAMPLE_FMT_FLTP\n";
    case AV_SAMPLE_FMT_DBLP:
      return os << "AV_SAMPLE_FMT_DBLP\n";
    case AV_SAMPLE_FMT_NB:
      return os << "AV_SAMPLE_FMT_NB\n";
    default:
      return os << "<UNKNOWN AVSampleFormat: " << static_cast<int>(value) << ">"
                << "\n";
  }
}

std::ostream& operator<<(std::ostream& os, AVColorSpace value) {
  switch (value) {
    case AVCOL_SPC_RGB:
      return os << "AVCOL_SPC_RGB";
    case AVCOL_SPC_BT709:
      return os << "AVCOL_SPC_BT709";
    case AVCOL_SPC_UNSPECIFIED:
      return os << "AVCOL_SPC_UNSPECIFIED";
    case AVCOL_SPC_RESERVED:
      return os << "AVCOL_SPC_RESERVED";
    case AVCOL_SPC_FCC:
      return os << "AVCOL_SPC_FCC";
    case AVCOL_SPC_BT470BG:
      return os << "AVCOL_SPC_BT470BG";
    case AVCOL_SPC_SMPTE170M:
      return os << "AVCOL_SPC_SMPTE170M";
    case AVCOL_SPC_SMPTE240M:
      return os << "AVCOL_SPC_SMPTE240M";
    case AVCOL_SPC_YCOCG:
      return os << "AVCOL_SPC_YCOCG";
    case AVCOL_SPC_BT2020_NCL:
      return os << "AVCOL_SPC_BT2020_NCL";
    case AVCOL_SPC_BT2020_CL:
      return os << "AVCOL_SPC_BT2020_CL";
    case AVCOL_SPC_NB:
      return os << "AVCOL_SPC_NB";
    default:
      return os << "<UNKNOWN AVColorSpace: " << static_cast<int>(value) << ">";
  }
}

std::ostream& operator<<(std::ostream& os, enum AVDiscard value) {
  switch (value) {
    case AVDISCARD_NONE:
      return os << "AVDISCARD_NONE\n";
    case AVDISCARD_DEFAULT:
      return os << "AVDISCARD_DEFAULT\n";
    case AVDISCARD_NONREF:
      return os << "AVDISCARD_NONREF\n";
    case AVDISCARD_BIDIR:
      return os << "AVDISCARD_BIDIR\n";
    case AVDISCARD_NONINTRA:
      return os << "AVDISCARD_NONINTRA\n";
    case AVDISCARD_NONKEY:
      return os << "AVDISCARD_NONKEY\n";
    case AVDISCARD_ALL:
      return os << "AVDISCARD_ALL\n";
    default:
      return os << "<UNKNOWN AVDISCARD_: " << static_cast<int>(value) << ">"
                << "\n";
  }
}

std::ostream& operator<<(std::ostream& os, AVDurationEstimationMethod value) {
  switch (value) {
    case AVFMT_DURATION_FROM_PTS:
      return os << "AVFMT_DURATION_FROM_PTS\n";
    case AVFMT_DURATION_FROM_STREAM:
      return os << "AVFMT_DURATION_FROM_STREAM\n";
    case AVFMT_DURATION_FROM_BITRATE:
      return os << "AVFMT_DURATION_FROM_BITRATE\n";
    default:
      return os << "<UNKNOWN AVDurationEstimationMethod: "
                << static_cast<int>(value) << ">\n";
  }
}

std::ostream& operator<<(std::ostream& os, const AVFormatContext* value) {
  if (value == nullptr) {
    return os << "<nullptr>\n";
  } else {
    os << "\n";
  }

  os << indent;
  os << begl << "AVInputFormat *iformat: " << value->iformat;
  os << begl << "AVOutputFormat *oformat: " << value->oformat;
  os << begl << "AVIOContext *pb: " << value->pb;
  os << begl << "int ctx_flags: " << AVFMTCTXFlags(value->ctx_flags);
  os << begl << "unsigned int nb_streams: " << value->nb_streams << "\n";
  os << begl << "AVStream **streams: "
     << AVStreamArray(value->streams, value->nb_streams);
  os << begl << "char filename[1024]: " << value->filename << "\n";
  os << begl << "int64_t start_time: " << value->start_time << "\n";
  os << begl << "int64_t duration: " << value->duration << "\n";
  os << begl << "int64_t bit_rate: " << value->bit_rate << "\n";
  os << begl << "unsigned int packet_size: " << value->packet_size << "\n";
  os << begl << "int max_delay: " << value->max_delay << "\n";
  os << begl << "int flags: " << AVFMTFlags(value->flags);
  os << begl << "int64_t probesize: " << value->probesize << "\n";
  os << begl << "unsigned int nb_programs: " << value->nb_programs << "\n";
  os << begl << "AVProgram **programs: "
     << AVProgramArray(value->programs, value->nb_programs);
  os << begl << "AVCodecID video_codec_id: " << value->video_codec_id;
  os << begl << "AVCodecID audio_codec_id: " << value->audio_codec_id;
  os << begl << "AVCodecID subtitle_codec_id: " << value->subtitle_codec_id;
  os << begl << "unsigned int max_index_size: " << value->max_index_size
     << "\n";
  os << begl << "unsigned int max_picture_buffer: " << value->max_picture_buffer
     << "\n";
  os << begl << "unsigned int nb_chapters: " << value->nb_chapters << "\n";
  os << begl << "AVChapter **chapters: "
     << AVChapterArray(value->chapters, value->nb_chapters);
  os << begl << "AVDictionary *metadata: " << value->metadata;
  os << begl << "int64_t start_time_realtime: " << value->start_time_realtime
     << "\n";
  os << begl << "int fps_probe_size: " << value->fps_probe_size << "\n";
  os << begl << "int error_recognition: " << value->error_recognition << "\n";
  os << begl << "int64_t max_interleave_delta: " << value->max_interleave_delta
     << "\n";
  os << begl << "int strict_std_compliance: " << value->strict_std_compliance
     << "\n";
  os << begl << "int event_flags: " << AVFMT_EVENTFlags(value->flags);
  os << begl << "int max_ts_probe: " << value->max_ts_probe << "\n";
  os << begl << "int avoid_negative_ts: "
     << AVFMT_AVOID_NEG_TSFlags(value->avoid_negative_ts);
  os << begl << "int ts_id: " << value->ts_id << "\n";
  os << begl << "int audio_preload: " << value->audio_preload << "\n";
  os << begl << "int max_chunk_duration: " << value->max_chunk_duration << "\n";
  os << begl << "int max_chunk_size: " << value->max_chunk_size << "\n";
  os << begl << "int use_wallclock_as_timestamps: "
     << value->use_wallclock_as_timestamps << "\n";
  os << begl << "int avio_flags: " << value->avio_flags << "\n";
  os << begl << "AVDurationEstimationMethod duration_estimation_method: "
     << value->duration_estimation_method;
  os << begl << "int64_t skip_initial_bytes: " << value->skip_initial_bytes
     << "\n";
  os << begl << "TODO(dalesat): more\n";
  return os << outdent;
}

}  // namespace media

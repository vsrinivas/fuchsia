// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ostream>

#include "garnet/bin/mediaplayer/ffmpeg/ffmpeg_formatting.h"

extern "C" {
#include "libavformat/avformat.h"
#include "libavformat/internal.h"
#include "libavutil/dict.h"
}

namespace media_player {

const char* safe(const char* s) { return s == nullptr ? "<nullptr>" : s; }

std::ostream& operator<<(std::ostream& os,
                         const struct AVCodecTag* const* value) {
  if (value == nullptr) {
    return os << "<nullptr>";
  } else if (*value == nullptr) {
    return os << "&<nullptr>";
  }

  os << fostr::Indent;
  os << fostr::NewLine << "AVCodecID id: " << (*value)->id;
  os << fostr::NewLine << "unsigned int tag: " << (*value)->tag;
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const AVInputFormat* value) {
  if (value == nullptr) {
    return os << "<nullptr>";
  }

  os << fostr::Indent;
  os << fostr::NewLine << "const char *name: " << value->name;
  os << fostr::NewLine << "const char *long_name: " << value->long_name;
  os << fostr::NewLine << "int flags: " << AVFMTFlags(value->flags);
  os << fostr::NewLine << "const char *extensions: " << safe(value->extensions);
  os << fostr::NewLine
     << "const AVCodecTag * const *codec_tag: " << value->codec_tag;
  os << fostr::NewLine << "const char *mime_type: " << safe(value->mime_type);
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const AVOutputFormat* value) {
  if (value == nullptr) {
    return os << "<nullptr>";
  }

  os << fostr::Indent;
  os << fostr::NewLine << "const char *name: " << safe(value->name);
  os << fostr::NewLine << "const char *long_name: " << safe(value->long_name);
  os << fostr::NewLine << "const char *mime_type: " << safe(value->mime_type);
  os << fostr::NewLine << "const char *extensions: " << safe(value->extensions);
  os << fostr::NewLine << "AVCodecID audio_codec: " << value->audio_codec;
  os << fostr::NewLine << "AVCodecID video_codec: " << value->video_codec;
  os << fostr::NewLine << "AVCodecID subtitle_codec: " << value->subtitle_codec;
  os << fostr::NewLine << "int flags: " << AVFMTFlags(value->flags);
  os << fostr::NewLine
     << "const AVCodecTag * const *codec_tag: " << value->codec_tag;
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const AVIOContext* value) {
  if (value == nullptr) {
    return os << "<nullptr>";
  } else {
    return os << "TODO";
  }
}

std::ostream& operator<<(std::ostream& os, AVFMTCTXFlags value) {
  if (value.flags_ == 0) {
    return os << "<none>";
  }

  if (value.flags_ & AVFMTCTX_NOHEADER) {
    return os << "AVFMTCTX_NOHEADER";
  } else {
    return os << "<UNKNOWN AVFMTCTX_: " << value.flags_ << ">";
  }
}

std::ostream& operator<<(std::ostream& os, const AVRational* value) {
  if (value == nullptr) {
    return os << "<none>";
  }

  os << fostr::Indent;
  for (int index = 0; value->num != 0 || value->den != 0; ++value, ++index) {
    os << fostr::NewLine << "[" << index << "]: " << *value;
  }
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const int* value) {
  if (value == nullptr) {
    return os << "<none>";
  }

  os << fostr::Indent;
  for (int index = 0; *value != 0; ++value, ++index) {
    os << fostr::NewLine << "[" << index << "]: " << *value;
  }
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const uint64_t* value) {
  if (value == nullptr) {
    return os << "<none>";
  }

  os << fostr::Indent;
  for (int index = 0; *value != 0; ++value, ++index) {
    os << fostr::NewLine << "[" << index << "]: " << *value;
  }
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const AVSampleFormat* value) {
  if (value == nullptr) {
    return os << "<none>";
  }

  os << fostr::Indent;
  for (int index = 0; int(*value) != 0; ++value, ++index) {
    os << fostr::NewLine << "[" << index << "]: " << *value;
  }
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const AVCodec* value) {
  if (value == nullptr) {
    return os << "<nullptr>";
  }

  os << fostr::Indent;
  os << fostr::NewLine << "const char *name: " << safe(value->name);
  os << fostr::NewLine << "const char *long_name: " << safe(value->long_name);
  os << fostr::NewLine << "AVMediaType type: " << value->type;
  os << fostr::NewLine << "AVCodecID id: " << value->id;
  os << fostr::NewLine << "int capabilities: " << value->capabilities;
  os << fostr::NewLine
     << "AVRational *supported_framerates: " << value->supported_framerates;
  os << fostr::NewLine
     << "const int *supported_samplerates: " << value->supported_samplerates;
  os << fostr::NewLine
     << "const AVSampleFormat *sample_fmts: " << value->sample_fmts;
  os << fostr::NewLine
     << "const uint64_t *channel_layouts: " << value->channel_layouts;

  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const AVCodecContext* value) {
  if (value == nullptr) {
    return os << "<nullptr>";
  }

  os << fostr::Indent;
  os << fostr::NewLine << "AVMediaType codec_type: " << value->codec_type;
  os << fostr::NewLine << "const struct AVCodec *codec: " << value->codec;
  os << fostr::NewLine << "AVCodecID codec_id: " << value->codec_id;
  os << fostr::NewLine << "int bit_rate: " << value->bit_rate;
  os << fostr::NewLine << "int extradata_size: " << value->extradata_size;
  os << fostr::NewLine << "int width: " << value->width;
  os << fostr::NewLine << "int height: " << value->height;
  os << fostr::NewLine << "int coded_width: " << value->coded_width;
  os << fostr::NewLine << "int coded_height: " << value->coded_height;
  os << fostr::NewLine << "int gop_size: " << value->gop_size;
  os << fostr::NewLine << "int sample_rate: " << value->sample_rate;
  os << fostr::NewLine << "int channels: " << value->channels;
  os << fostr::NewLine << "AVSampleFormat sample_fmt: " << value->sample_fmt;
  os << fostr::NewLine << "int frame_size: " << value->frame_size;
  os << fostr::NewLine << "int frame_number: " << value->frame_number;
  os << fostr::NewLine << "int block_align: " << value->block_align;
  os << fostr::NewLine << "int cutoff: " << value->cutoff;
  os << fostr::NewLine << "uint64_t channel_layout: " << value->channel_layout;
  os << fostr::NewLine
     << "uint64_t request_channel_layout: " << value->request_channel_layout;
  os << fostr::NewLine
     << "AVAudioServiceType audio_service_type: " << value->audio_service_type;
  os << fostr::NewLine
     << "AVSampleFormat request_sample_fmt: " << value->request_sample_fmt;
  os << fostr::NewLine << "int profile: " << value->profile;
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const AVRational& value) {
  return os << value.num << "/" << value.den;
}

std::ostream& operator<<(std::ostream& os, const AVStream* value) {
  if (value == nullptr) {
    return os << "<nullptr>";
  }

  os << fostr::Indent;
  os << fostr::NewLine << "int index: " << value->index;
  os << fostr::NewLine << "int id: " << value->id;
  os << fostr::NewLine << "AVRational time_base: " << value->time_base;
  os << fostr::NewLine << "int64_t start_time: " << value->start_time;
  os << fostr::NewLine << "int64_t duration: " << value->duration;
  os << fostr::NewLine << "int64_t nb_frames: " << value->nb_frames;
  os << fostr::NewLine
     << "int disposition: " << AV_DISPOSITIONFlags(value->disposition);
  os << fostr::NewLine << "AVDiscard discard: " << value->discard;
  os << fostr::NewLine
     << "AVRational sample_aspect_ratio: " << value->sample_aspect_ratio;
  os << fostr::NewLine << "AVDictionary *metadata: " << value->metadata;
  os << fostr::NewLine
     << "AVRational avg_frame_rate: " << value->avg_frame_rate;
  os << fostr::NewLine << "AVPacket attached_pic: " << &value->attached_pic;
  os << fostr::NewLine << "int nb_side_data: " << value->nb_side_data;
  os << fostr::NewLine << "AVPacketSideData side_data: "
     << AVPacketSideDataArray(value->side_data, value->nb_side_data);
  os << fostr::NewLine
     << "int event_flags: " << AVSTREAM_EVENTFlags(value->event_flags);
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const AVStreamArray& value) {
  if (value.items_ == nullptr) {
    return os << "<nullptr>";
  } else if (value.count_ == 0) {
    return os << "<empty>";
  }

  os << fostr::Indent;
  for (unsigned int i = 0; i < value.count_; i++) {
    os << fostr::NewLine << "[" << i << "] " << value.items_[i];
  }
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, AVFMTFlags value) {
  if (value.flags_ == 0) {
    return os << "<none>";
  }

  os << fostr::Indent;
  if (value.flags_ & AVFMT_FLAG_GENPTS) {
    os << fostr::NewLine << "AVFMT_FLAG_GENPTS";
  }
  if (value.flags_ & AVFMT_FLAG_IGNIDX) {
    os << fostr::NewLine << "AVFMT_FLAG_IGNIDX";
  }
  if (value.flags_ & AVFMT_FLAG_NONBLOCK) {
    os << fostr::NewLine << "AVFMT_FLAG_NONBLOCK";
  }
  if (value.flags_ & AVFMT_FLAG_IGNDTS) {
    os << fostr::NewLine << "AVFMT_FLAG_IGNDTS";
  }
  if (value.flags_ & AVFMT_FLAG_NOFILLIN) {
    os << fostr::NewLine << "AVFMT_FLAG_NOFILLIN";
  }
  if (value.flags_ & AVFMT_FLAG_NOPARSE) {
    os << fostr::NewLine << "AVFMT_FLAG_NOPARSE";
  }
  if (value.flags_ & AVFMT_FLAG_NOBUFFER) {
    os << fostr::NewLine << "AVFMT_FLAG_NOBUFFER";
  }
  if (value.flags_ & AVFMT_FLAG_CUSTOM_IO) {
    os << fostr::NewLine << "AVFMT_FLAG_CUSTOM_IO";
  }
  if (value.flags_ & AVFMT_FLAG_DISCARD_CORRUPT) {
    os << fostr::NewLine << "AVFMT_FLAG_DISCARD_CORRUPT";
  }
  if (value.flags_ & AVFMT_FLAG_FLUSH_PACKETS) {
    os << fostr::NewLine << "AVFMT_FLAG_FLUSH_PACKETS";
  }
  if (value.flags_ & AVFMT_FLAG_BITEXACT) {
    os << fostr::NewLine << "AVFMT_FLAG_BITEXACT";
  }
  if (value.flags_ & AVFMT_FLAG_MP4A_LATM) {
    os << fostr::NewLine << "AVFMT_FLAG_MP4A_LATM";
  }
  if (value.flags_ & AVFMT_FLAG_SORT_DTS) {
    os << fostr::NewLine << "AVFMT_FLAG_SORT_DTS";
  }
  if (value.flags_ & AVFMT_FLAG_PRIV_OPT) {
    os << fostr::NewLine << "AVFMT_FLAG_PRIV_OPT";
  }
  if (value.flags_ & AVFMT_FLAG_KEEP_SIDE_DATA) {
    os << fostr::NewLine << "AVFMT_FLAG_KEEP_SIDE_DATA";
  }
  if (value.flags_ & AVFMT_FLAG_FAST_SEEK) {
    os << fostr::NewLine << "AVFMT_FLAG_FAST_SEEK";
  }
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, AV_DISPOSITIONFlags value) {
  if (value.flags_ == 0) {
    return os << "<none>";
  }

  os << fostr::Indent;
  if (value.flags_ & AV_DISPOSITION_DEFAULT) {
    os << fostr::NewLine << "AV_DISPOSITION_DEFAULT  0x0001";
  }
  if (value.flags_ & AV_DISPOSITION_DUB) {
    os << fostr::NewLine << "AV_DISPOSITION_DUB      0x0002";
  }
  if (value.flags_ & AV_DISPOSITION_ORIGINAL) {
    os << fostr::NewLine << "AV_DISPOSITION_ORIGINAL 0x0004";
  }
  if (value.flags_ & AV_DISPOSITION_COMMENT) {
    os << fostr::NewLine << "AV_DISPOSITION_COMMENT  0x0008";
  }
  if (value.flags_ & AV_DISPOSITION_LYRICS) {
    os << fostr::NewLine << "AV_DISPOSITION_LYRICS   0x0010";
  }
  if (value.flags_ & AV_DISPOSITION_KARAOKE) {
    os << fostr::NewLine << "AV_DISPOSITION_KARAOKE  0x0020";
  }
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const AVBufferRef* value) {
  if (value == nullptr) {
    return os << "<nullptr>";
  }

  os << fostr::Indent;
  os << fostr::NewLine << "AVBuffer *buffer: "
     << (value->buffer == nullptr ? "<nullptr>" : "TODO");
  os << fostr::NewLine << "uint8_t *data: "
     << (value->data == nullptr ? "<nullptr>" : "<opaque>");
  os << fostr::NewLine << "int size: " << value->size;
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const AVFrame* value) {
  if (value == nullptr) {
    return os << "<nullptr>";
  }

  os << fostr::Indent;
  os << fostr::NewLine << "uint8_t *data[AV_NUM_DATA_POINTERS]: ";
  {
    os << fostr::Indent;
    bool any = false;
    for (int i = 0; i < AV_NUM_DATA_POINTERS; i++) {
      if (value->data[i] != nullptr) {
        if (!any) {
          any = true;
        }
        os << fostr::NewLine << "[" << i << "]: <opaque>";
      }
    }
    if (!any) {
      os << "<all nullptr>";
    }
    os << fostr::Outdent;
  }

  os << fostr::NewLine << "int linesize[AV_NUM_DATA_POINTERS]: ";
  {
    os << fostr::Indent;
    bool any = false;
    for (int i = 0; i < AV_NUM_DATA_POINTERS; i++) {
      if (value->linesize[i] != 0) {
        if (!any) {
          any = true;
        }
        os << fostr::NewLine << "[" << i << "]: " << value->linesize[i];
      }
    }
    if (!any) {
      os << "<all zero>";
    }
    os << fostr::Outdent;
  }

  os << fostr::NewLine << "uint8_t **extended_data: "
     << (value->extended_data == nullptr ? "<nullptr>" : "<opaque>");
  os << fostr::NewLine << "int width: " << value->width;
  os << fostr::NewLine << "int height: " << value->height;
  os << fostr::NewLine << "int nb_samples: " << value->nb_samples;
  os << fostr::NewLine << "int format: " << value->format;
  os << fostr::NewLine << "int key_frame: " << value->key_frame;
  os << fostr::NewLine << "int64_t pts: " << value->pts;
  os << fostr::NewLine << "int64_t pkt_dts: " << value->pkt_dts;
  os << fostr::NewLine << "int sample_rate: " << value->sample_rate;
  os << fostr::NewLine << "AVBufferRef *buf[AV_NUM_DATA_POINTERS]: ";
  {
    os << fostr::Indent;
    bool any = false;
    for (int i = 0; i < AV_NUM_DATA_POINTERS; i++) {
      if (value->buf[i] != nullptr) {
        if (!any) {
          any = true;
        }
        os << fostr::NewLine << "[" << i << "]:" << value->buf[i];
      }
    }
    if (!any) {
      os << "<all nullptr>";
    }
    os << fostr::Outdent;
  }
  os << fostr::NewLine << "int channels: " << value->channels;
  os << fostr::NewLine << "int pkt_size: " << value->pkt_size;
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const AVPacket* value) {
  if (value == nullptr) {
    return os << "<nullptr>";
  }

  os << fostr::Indent;
  os << fostr::NewLine << "AVBufferRef *buf: " << value->buf;
  os << fostr::NewLine << "int64_t pts: " << value->pts;
  os << fostr::NewLine << "int64_t dts: " << value->dts;
  os << fostr::NewLine << "uint8_t *data: "
     << (value->data == nullptr ? "<nullptr>" : "<opaque>");
  os << fostr::NewLine << "int size: " << value->size;
  os << fostr::NewLine << "int stream_index: " << value->stream_index;
  os << fostr::NewLine << "int flags: " << value->flags;
  os << fostr::NewLine << "AVPacketSideData *side_data: " << value->side_data;
  os << fostr::NewLine << "int side_data_elems: " << value->side_data_elems;
  os << fostr::NewLine << "int duration: " << value->duration;
  os << fostr::NewLine << "int64_t pos: " << value->pos;
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const AVPacketSideData* value) {
  if (value == nullptr) {
    return os << "<nullptr>";
  } else {
    return os << "TODO";
  }
}

std::ostream& operator<<(std::ostream& os, const AVPacketSideDataArray& value) {
  if (value.items_ == nullptr) {
    return os << "<nullptr>";
  } else if (value.count_ == 0) {
    return os << "<empty>";
  }

  os << fostr::Indent;
  for (unsigned int i = 0; i < value.count_; i++) {
    os << fostr::NewLine << "[" << i << "] " << &value.items_[i];
  }
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const AVProgram* value) {
  if (value == nullptr) {
    return os << "<nullptr>";
  } else {
    return os << "TODO";
  }
}

std::ostream& operator<<(std::ostream& os, const AVProgramArray& value) {
  if (value.items_ == nullptr) {
    return os << "<nullptr>";
  } else if (value.count_ == 0) {
    return os << "<empty>";
  }

  os << fostr::Indent;
  for (unsigned int i = 0; i < value.count_; i++) {
    os << fostr::NewLine << "[" << i << "]" << value.items_[i];
  }
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, const AVChapter* value) {
  if (value == nullptr) {
    return os << "<nullptr>";
  } else {
    return os << "TODO";
  }
}

std::ostream& operator<<(std::ostream& os, const AVChapterArray& value) {
  if (value.items_ == nullptr) {
    return os << "<nullptr>";
  } else if (value.count_ == 0) {
    return os << "<empty>";
  }

  os << fostr::Indent;
  for (unsigned int i = 0; i < value.count_; i++) {
    os << fostr::NewLine << "[" << i << "]" << value.items_[i];
  }
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, AVCodecID value) {
  return os << avcodec_get_name(value) << " (" << static_cast<int>(value)
            << ")";
}

std::ostream& operator<<(std::ostream& os, const AVDictionary* value) {
  if (value == nullptr) {
    return os << "<nullptr>";
  }
  AVDictionaryEntry* entry =
      av_dict_get(value, "", nullptr, AV_DICT_IGNORE_SUFFIX);
  if (entry == nullptr) {
    return os << "<empty>";
  }

  os << fostr::Indent;
  while (entry != nullptr) {
    os << fostr::NewLine << safe(entry->key) << ": " << safe(entry->value);
    entry = av_dict_get(value, "", entry, AV_DICT_IGNORE_SUFFIX);
  }
  return os << fostr::Outdent;
}

std::ostream& operator<<(std::ostream& os, AVFMT_EVENTFlags value) {
  if (value.flags_ == 0) {
    return os << "<none>";
  }

  if (value.flags_ & AVFMT_EVENT_FLAG_METADATA_UPDATED) {
    return os << "AVFMT_EVENT_FLAG_METADATA_UPDATED";
  } else {
    return os << "<UNKNOWN AVFMT_EVENT_FLAG_: " << value.flags_ << ">";
  }
}

std::ostream& operator<<(std::ostream& os, AVSTREAM_EVENTFlags value) {
  if (value.flags_ == 0) {
    return os << "<none>";
  }

  if (value.flags_ & AVSTREAM_EVENT_FLAG_METADATA_UPDATED) {
    return os << "AVSTREAM_EVENT_FLAG_METADATA_UPDATED";
  } else {
    return os << "<UNKNOWN AVSTREAM_EVENT_FLAG_: " << value.flags_ << ">";
  }
}

std::ostream& operator<<(std::ostream& os, AVFMT_AVOID_NEG_TSFlags value) {
  switch (value.flags_) {
    case AVFMT_AVOID_NEG_TS_AUTO:
      return os << "AVFMT_AVOID_NEG_TS_AUTO";
    case AVFMT_AVOID_NEG_TS_MAKE_NON_NEGATIVE:
      return os << "AVFMT_AVOID_NEG_TS_MAKE_NON_NEGATIVE";
    case AVFMT_AVOID_NEG_TS_MAKE_ZERO:
      return os << "AVFMT_AVOID_NEG_TS_MAKE_ZERO";
    default:
      return os << "<UNKNOWN AVFMT_AVOID_NEG_TS_: " << value.flags_ << ">";
  }
}

std::ostream& operator<<(std::ostream& os, AVMediaType value) {
  switch (value) {
    case AVMEDIA_TYPE_UNKNOWN:
      return os << "AVMEDIA_TYPE_UNKNOWN";
    case AVMEDIA_TYPE_VIDEO:
      return os << "AVMEDIA_TYPE_VIDEO";
    case AVMEDIA_TYPE_AUDIO:
      return os << "AVMEDIA_TYPE_AUDIO";
    case AVMEDIA_TYPE_DATA:
      return os << "AVMEDIA_TYPE_DATA";
    case AVMEDIA_TYPE_SUBTITLE:
      return os << "AVMEDIA_TYPE_SUBTITLE";
    case AVMEDIA_TYPE_ATTACHMENT:
      return os << "AVMEDIA_TYPE_ATTACHMENT";
    case AVMEDIA_TYPE_NB:
      return os << "AVMEDIA_TYPE_NB";
    default:
      return os << "<UNKNOWN AVMediaType: " << static_cast<int>(value) << ">";
  }
}

std::ostream& operator<<(std::ostream& os, AVSampleFormat value) {
  switch (value) {
    case AV_SAMPLE_FMT_NONE:
      return os << "AV_SAMPLE_FMT_NONE";
    case AV_SAMPLE_FMT_U8:
      return os << "AV_SAMPLE_FMT_U8";
    case AV_SAMPLE_FMT_S16:
      return os << "AV_SAMPLE_FMT_S16";
    case AV_SAMPLE_FMT_S32:
      return os << "AV_SAMPLE_FMT_S32";
    case AV_SAMPLE_FMT_FLT:
      return os << "AV_SAMPLE_FMT_FLT";
    case AV_SAMPLE_FMT_DBL:
      return os << "AV_SAMPLE_FMT_DBL";
    case AV_SAMPLE_FMT_U8P:
      return os << "AV_SAMPLE_FMT_U8P";
    case AV_SAMPLE_FMT_S16P:
      return os << "AV_SAMPLE_FMT_S16P";
    case AV_SAMPLE_FMT_S32P:
      return os << "AV_SAMPLE_FMT_S32P";
    case AV_SAMPLE_FMT_FLTP:
      return os << "AV_SAMPLE_FMT_FLTP";
    case AV_SAMPLE_FMT_DBLP:
      return os << "AV_SAMPLE_FMT_DBLP";
    case AV_SAMPLE_FMT_NB:
      return os << "AV_SAMPLE_FMT_NB";
    default:
      return os << "<UNKNOWN AVSampleFormat: " << static_cast<int>(value)
                << ">";
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
      return os << "AVDISCARD_NONE";
    case AVDISCARD_DEFAULT:
      return os << "AVDISCARD_DEFAULT";
    case AVDISCARD_NONREF:
      return os << "AVDISCARD_NONREF";
    case AVDISCARD_BIDIR:
      return os << "AVDISCARD_BIDIR";
    case AVDISCARD_NONINTRA:
      return os << "AVDISCARD_NONINTRA";
    case AVDISCARD_NONKEY:
      return os << "AVDISCARD_NONKEY";
    case AVDISCARD_ALL:
      return os << "AVDISCARD_ALL";
    default:
      return os << "<UNKNOWN AVDISCARD_: " << static_cast<int>(value) << ">";
  }
}

std::ostream& operator<<(std::ostream& os, AVDurationEstimationMethod value) {
  switch (value) {
    case AVFMT_DURATION_FROM_PTS:
      return os << "AVFMT_DURATION_FROM_PTS";
    case AVFMT_DURATION_FROM_STREAM:
      return os << "AVFMT_DURATION_FROM_STREAM";
    case AVFMT_DURATION_FROM_BITRATE:
      return os << "AVFMT_DURATION_FROM_BITRATE";
    default:
      return os << "<UNKNOWN AVDurationEstimationMethod: "
                << static_cast<int>(value) << ">";
  }
}

std::ostream& operator<<(std::ostream& os, const AVFormatContext* value) {
  if (value == nullptr) {
    return os << "<nullptr>";
  }

  os << fostr::Indent;
  os << fostr::NewLine << "AVInputFormat *iformat: " << value->iformat;
  os << fostr::NewLine << "AVOutputFormat *oformat: " << value->oformat;
  os << fostr::NewLine << "AVIOContext *pb: " << value->pb;
  os << fostr::NewLine << "int ctx_flags: " << AVFMTCTXFlags(value->ctx_flags);
  os << fostr::NewLine << "unsigned int nb_streams: " << value->nb_streams;
  os << fostr::NewLine << "AVStream **streams: "
     << AVStreamArray(value->streams, value->nb_streams);
  os << fostr::NewLine << "char filename[1024]: " << value->filename;
  os << fostr::NewLine << "int64_t start_time: " << value->start_time;
  os << fostr::NewLine << "int64_t duration: " << value->duration;
  os << fostr::NewLine << "int64_t bit_rate: " << value->bit_rate;
  os << fostr::NewLine << "unsigned int packet_size: " << value->packet_size;
  os << fostr::NewLine << "int max_delay: " << value->max_delay;
  os << fostr::NewLine << "int flags: " << AVFMTFlags(value->flags);
  os << fostr::NewLine << "int64_t probesize: " << value->probesize;
  os << fostr::NewLine << "unsigned int nb_programs: " << value->nb_programs;
  os << fostr::NewLine << "AVProgram **programs: "
     << AVProgramArray(value->programs, value->nb_programs);
  os << fostr::NewLine << "AVCodecID video_codec_id: " << value->video_codec_id;
  os << fostr::NewLine << "AVCodecID audio_codec_id: " << value->audio_codec_id;
  os << fostr::NewLine
     << "AVCodecID subtitle_codec_id: " << value->subtitle_codec_id;
  os << fostr::NewLine
     << "unsigned int max_index_size: " << value->max_index_size;
  os << fostr::NewLine
     << "unsigned int max_picture_buffer: " << value->max_picture_buffer;
  os << fostr::NewLine << "unsigned int nb_chapters: " << value->nb_chapters;
  os << fostr::NewLine << "AVChapter **chapters: "
     << AVChapterArray(value->chapters, value->nb_chapters);
  os << fostr::NewLine << "AVDictionary *metadata: " << value->metadata;
  os << fostr::NewLine
     << "int64_t start_time_realtime: " << value->start_time_realtime;
  os << fostr::NewLine << "int fps_probe_size: " << value->fps_probe_size;
  os << fostr::NewLine << "int error_recognition: " << value->error_recognition;
  os << fostr::NewLine
     << "int64_t max_interleave_delta: " << value->max_interleave_delta;
  os << fostr::NewLine
     << "int strict_std_compliance: " << value->strict_std_compliance;
  os << fostr::NewLine << "int event_flags: " << AVFMT_EVENTFlags(value->flags);
  os << fostr::NewLine << "int max_ts_probe: " << value->max_ts_probe;
  os << fostr::NewLine << "int avoid_negative_ts: "
     << AVFMT_AVOID_NEG_TSFlags(value->avoid_negative_ts);
  os << fostr::NewLine << "int ts_id: " << value->ts_id;
  os << fostr::NewLine << "int audio_preload: " << value->audio_preload;
  os << fostr::NewLine
     << "int max_chunk_duration: " << value->max_chunk_duration;
  os << fostr::NewLine << "int max_chunk_size: " << value->max_chunk_size;
  os << fostr::NewLine << "int use_wallclock_as_timestamps: "
     << value->use_wallclock_as_timestamps;
  os << fostr::NewLine << "int avio_flags: " << value->avio_flags;
  os << fostr::NewLine
     << "AVDurationEstimationMethod duration_estimation_method: "
     << value->duration_estimation_method;
  os << fostr::NewLine
     << "int64_t skip_initial_bytes: " << value->skip_initial_bytes;
  os << fostr::NewLine << "TODO(dalesat): more";
  return os << fostr::Outdent;
}

}  // namespace media_player

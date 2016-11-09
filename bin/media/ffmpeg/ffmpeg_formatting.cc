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
    return os << "<nullptr>" << std::endl;
  } else if (*value == nullptr) {
    return os << "&<nullptr>" << std::endl;
  } else {
    os << std::endl;
  }

  os << indent;
  os << begl << "AVCodecID id: " << (*value)->id << std::endl;
  os << begl << "unsigned int tag: " << (*value)->tag << std::endl;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVInputFormat* value) {
  if (value == nullptr) {
    return os << "<nullptr>" << std::endl;
  } else {
    os << std::endl;
  }

  os << indent;
  os << begl << "const char *name: " << value->name << std::endl;
  os << begl << "const char *long_name: " << value->long_name << std::endl;
  os << begl << "int flags: " << AVFMTFlags(value->flags);
  os << begl << "const char *extensions: " << safe(value->extensions)
     << std::endl;
  os << begl << "const AVCodecTag * const *codec_tag: " << value->codec_tag;
  os << begl << "const char *mime_type: " << safe(value->mime_type)
     << std::endl;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVOutputFormat* value) {
  if (value == nullptr) {
    return os << "<nullptr>" << std::endl;
  } else {
    os << std::endl;
  }

  os << indent;
  os << begl << "const char *name: " << safe(value->name) << std::endl;
  os << begl << "const char *long_name: " << safe(value->long_name)
     << std::endl;
  os << begl << "const char *mime_type: " << safe(value->mime_type)
     << std::endl;
  os << begl << "const char *extensions: " << safe(value->extensions)
     << std::endl;
  os << begl << "AVCodecID audio_codec: " << value->audio_codec;
  os << begl << "AVCodecID video_codec: " << value->video_codec;
  os << begl << "AVCodecID subtitle_codec: " << value->subtitle_codec;
  os << begl << "int flags: " << AVFMTFlags(value->flags);
  os << begl << "const AVCodecTag * const *codec_tag: " << value->codec_tag;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVIOContext* value) {
  if (value == nullptr) {
    return os << "<nullptr>" << std::endl;
  } else {
    return os << "TODO" << std::endl;
  }
}

std::ostream& operator<<(std::ostream& os, AVFMTCTXFlags value) {
  if (value.flags_ == 0) {
    return os << "<none>" << std::endl;
  }

  if (value.flags_ & AVFMTCTX_NOHEADER) {
    return os << "AVFMTCTX_NOHEADER" << std::endl;
  } else {
    return os << "<UNKNOWN AVFMTCTX_: " << value.flags_ << ">" << std::endl;
  }
}

std::ostream& operator<<(std::ostream& os, const AVRational* value) {
  if (value == nullptr) {
    return os << "<none>" << std::endl;
  } else {
    os << std::endl;
  }

  os << indent;
  for (int index = 0; value->num != 0 || value->den != 0; ++value, ++index) {
    os << begl << "[" << index << "]: " << *value;
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const int* value) {
  if (value == nullptr) {
    return os << "<none>" << std::endl;
  } else {
    os << std::endl;
  }

  os << indent;
  for (int index = 0; *value != 0; ++value, ++index) {
    os << begl << "[" << index << "]: " << *value << std::endl;
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const uint64_t* value) {
  if (value == nullptr) {
    return os << "<none>" << std::endl;
  } else {
    os << std::endl;
  }

  os << indent;
  for (int index = 0; *value != 0; ++value, ++index) {
    os << begl << "[" << index << "]: " << *value << std::endl;
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVSampleFormat* value) {
  if (value == nullptr) {
    return os << "<none>" << std::endl;
  } else {
    os << std::endl;
  }

  os << indent;
  for (int index = 0; int(*value) != 0; ++value, ++index) {
    os << begl << "[" << index << "]: " << *value;
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVCodec* value) {
  if (value == nullptr) {
    return os << "<nullptr>" << std::endl;
  } else {
    os << std::endl;
  }

  os << indent;
  os << begl << "const char *name: " << safe(value->name) << std::endl;
  os << begl << "const char *long_name: " << safe(value->long_name)
     << std::endl;
  os << begl << "AVMediaType type: " << value->type;
  os << begl << "AVCodecID id: " << value->id;
  os << begl << "int capabilities: " << value->capabilities << std::endl;
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
    return os << "<nullptr>" << std::endl;
  } else {
    os << std::endl;
  }

  os << indent;
  os << begl << "AVMediaType codec_type: " << value->codec_type;
  os << begl << "const struct AVCodec *codec: " << value->codec;
  os << begl << "AVCodecID codec_id: " << value->codec_id;
  os << begl << "int bit_rate: " << value->bit_rate << std::endl;
  os << begl << "int extradata_size: " << value->extradata_size << std::endl;
  os << begl << "int width: " << value->width << std::endl;
  os << begl << "int height: " << value->height << std::endl;
  os << begl << "int coded_width: " << value->coded_width << std::endl;
  os << begl << "int coded_height: " << value->coded_height << std::endl;
  os << begl << "int gop_size: " << value->gop_size << std::endl;
  os << begl << "int sample_rate: " << value->sample_rate << std::endl;
  os << begl << "int channels: " << value->channels << std::endl;
  os << begl << "AVSampleFormat sample_fmt: " << value->sample_fmt;
  os << begl << "int frame_size: " << value->frame_size << std::endl;
  os << begl << "int frame_number: " << value->frame_number << std::endl;
  os << begl << "int block_align: " << value->block_align << std::endl;
  os << begl << "int cutoff: " << value->cutoff << std::endl;
  os << begl << "uint64_t channel_layout: " << value->channel_layout
     << std::endl;
  os << begl
     << "uint64_t request_channel_layout: " << value->request_channel_layout
     << std::endl;
  os << begl
     << "AVAudioServiceType audio_service_type: " << value->audio_service_type
     << std::endl;
  os << begl
     << "AVSampleFormat request_sample_fmt: " << value->request_sample_fmt;
  os << begl << "int profile: " << value->profile << std::endl;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVRational& value) {
  return os << value.num << "/" << value.den << std::endl;
}

std::ostream& operator<<(std::ostream& os, const AVStream* value) {
  if (value == nullptr) {
    return os << "<nullptr>" << std::endl;
  } else {
    os << std::endl;
  }

  os << indent;
  os << begl << "int index: " << value->index << std::endl;
  os << begl << "int id: " << value->id << std::endl;
  os << begl << "AVCodecContext *codec: " << value->codec;
  os << begl << "AVRational time_base: " << value->time_base;
  os << begl << "int64_t start_time: " << value->start_time << std::endl;
  os << begl << "int64_t duration: " << value->duration << std::endl;
  os << begl << "int64_t nb_frames: " << value->nb_frames << std::endl;
  os << begl << "int disposition: " << AV_DISPOSITIONFlags(value->disposition);
  os << begl << "AVDiscard discard: " << value->discard;
  os << begl
     << "AVRational sample_aspect_ratio: " << value->sample_aspect_ratio;
  os << begl << "AVDictionary *metadata: " << value->metadata;
  os << begl << "AVRational avg_frame_rate: " << value->avg_frame_rate;
  os << begl << "AVPacket attached_pic: " << &value->attached_pic;
  os << begl << "int nb_side_data: " << value->nb_side_data << std::endl;
  os << begl << "AVPacketSideData side_data: "
     << AVPacketSideDataArray(value->side_data, value->nb_side_data);
  os << begl << "int event_flags: " << AVSTREAM_EVENTFlags(value->event_flags);
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVStreamArray& value) {
  if (value.items_ == nullptr) {
    return os << "<nullptr>" << std::endl;
  } else if (value.count_ == 0) {
    return os << "<empty>" << std::endl;
  } else {
    os << std::endl;
  }

  os << indent;
  for (unsigned int i = 0; i < value.count_; i++) {
    os << begl << "[" << i << "] " << value.items_[i];
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, AVFMTFlags value) {
  if (value.flags_ == 0) {
    os << "<none>" << std::endl;
    return os;
  } else {
    os << std::endl;
  }

  os << indent;
  if (value.flags_ & AVFMT_FLAG_GENPTS) {
    os << begl << "AVFMT_FLAG_GENPTS" << std::endl;
  }
  if (value.flags_ & AVFMT_FLAG_IGNIDX) {
    os << begl << "AVFMT_FLAG_IGNIDX" << std::endl;
  }
  if (value.flags_ & AVFMT_FLAG_NONBLOCK) {
    os << begl << "AVFMT_FLAG_NONBLOCK" << std::endl;
  }
  if (value.flags_ & AVFMT_FLAG_IGNDTS) {
    os << begl << "AVFMT_FLAG_IGNDTS" << std::endl;
  }
  if (value.flags_ & AVFMT_FLAG_NOFILLIN) {
    os << begl << "AVFMT_FLAG_NOFILLIN" << std::endl;
  }
  if (value.flags_ & AVFMT_FLAG_NOPARSE) {
    os << begl << "AVFMT_FLAG_NOPARSE" << std::endl;
  }
  if (value.flags_ & AVFMT_FLAG_NOBUFFER) {
    os << begl << "AVFMT_FLAG_NOBUFFER" << std::endl;
  }
  if (value.flags_ & AVFMT_FLAG_CUSTOM_IO) {
    os << begl << "AVFMT_FLAG_CUSTOM_IO" << std::endl;
  }
  if (value.flags_ & AVFMT_FLAG_DISCARD_CORRUPT) {
    os << begl << "AVFMT_FLAG_DISCARD_CORRUPT" << std::endl;
  }
  if (value.flags_ & AVFMT_FLAG_FLUSH_PACKETS) {
    os << begl << "AVFMT_FLAG_FLUSH_PACKETS" << std::endl;
  }
  if (value.flags_ & AVFMT_FLAG_BITEXACT) {
    os << begl << "AVFMT_FLAG_BITEXACT" << std::endl;
  }
  if (value.flags_ & AVFMT_FLAG_MP4A_LATM) {
    os << begl << "AVFMT_FLAG_MP4A_LATM" << std::endl;
  }
  if (value.flags_ & AVFMT_FLAG_SORT_DTS) {
    os << begl << "AVFMT_FLAG_SORT_DTS" << std::endl;
  }
  if (value.flags_ & AVFMT_FLAG_PRIV_OPT) {
    os << begl << "AVFMT_FLAG_PRIV_OPT" << std::endl;
  }
  if (value.flags_ & AVFMT_FLAG_KEEP_SIDE_DATA) {
    os << begl << "AVFMT_FLAG_KEEP_SIDE_DATA" << std::endl;
  }
  if (value.flags_ & AVFMT_FLAG_FAST_SEEK) {
    os << begl << "AVFMT_FLAG_FAST_SEEK" << std::endl;
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, AV_DISPOSITIONFlags value) {
  if (value.flags_ == 0) {
    os << "<none>" << std::endl;
    return os;
  } else {
    os << std::endl;
  }

  os << indent;
  if (value.flags_ & AV_DISPOSITION_DEFAULT) {
    os << begl << "AV_DISPOSITION_DEFAULT  0x0001" << std::endl;
  }
  if (value.flags_ & AV_DISPOSITION_DUB) {
    os << begl << "AV_DISPOSITION_DUB      0x0002" << std::endl;
  }
  if (value.flags_ & AV_DISPOSITION_ORIGINAL) {
    os << begl << "AV_DISPOSITION_ORIGINAL 0x0004" << std::endl;
  }
  if (value.flags_ & AV_DISPOSITION_COMMENT) {
    os << begl << "AV_DISPOSITION_COMMENT  0x0008" << std::endl;
  }
  if (value.flags_ & AV_DISPOSITION_LYRICS) {
    os << begl << "AV_DISPOSITION_LYRICS   0x0010" << std::endl;
  }
  if (value.flags_ & AV_DISPOSITION_KARAOKE) {
    os << begl << "AV_DISPOSITION_KARAOKE  0x0020" << std::endl;
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVBufferRef* value) {
  if (value == nullptr) {
    return os << "<nullptr>" << std::endl;
  } else {
    os << std::endl;
  }

  os << indent;
  os << begl << "AVBuffer *buffer: "
     << (value->buffer == nullptr ? "<nullptr>" : "TODO") << std::endl;
  os << begl
     << "uint8_t *data: " << (value->data == nullptr ? "<nullptr>" : "<opaque>")
     << std::endl;
  os << begl << "int size: " << value->size << std::endl;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVFrame* value) {
  if (value == nullptr) {
    return os << "<nullptr>" << std::endl;
  } else {
    os << std::endl;
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
          os << std::endl;
        }
        os << begl << "[" << i << "]: <opaque>" << std::endl;
      }
    }
    if (!any) {
      os << "<all nullptr>" << std::endl;
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
          os << std::endl;
        }
        os << begl << "[" << i << "]: " << value->linesize[i] << std::endl;
      }
    }
    if (!any) {
      os << "<all zero>" << std::endl;
    }
    os << outdent;
  }

  os << begl << "uint8_t **extended_data: "
     << (value->extended_data == nullptr ? "<nullptr>" : "<opaque>")
     << std::endl;
  os << begl << "int width: " << value->width << std::endl;
  os << begl << "int height: " << value->height << std::endl;
  os << begl << "int nb_samples: " << value->nb_samples << std::endl;
  os << begl << "int format: " << value->format << std::endl;
  os << begl << "int key_frame: " << value->key_frame << std::endl;
  os << begl << "int64_t pts: " << value->pts << std::endl;
  os << begl << "int64_t pkt_pts: " << value->pkt_pts << std::endl;
  os << begl << "int64_t pkt_dts: " << value->pkt_dts << std::endl;
  os << begl << "int sample_rate: " << value->sample_rate << std::endl;
  os << begl << "AVBufferRef *buf[AV_NUM_DATA_POINTERS]: ";
  {
    os << indent;
    bool any = false;
    for (int i = 0; i < AV_NUM_DATA_POINTERS; i++) {
      if (value->buf[i] != nullptr) {
        if (!any) {
          any = true;
          os << std::endl;
        }
        os << begl << "[" << i << "]:" << value->buf[i];
      }
    }
    if (!any) {
      os << "<all nullptr>" << std::endl;
    }
    os << outdent;
  }
  os << begl << "int channels: " << value->channels << std::endl;
  os << begl << "int pkt_size: " << value->pkt_size << std::endl;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVPacket* value) {
  if (value == nullptr) {
    return os << "<nullptr>" << std::endl;
  } else {
    os << std::endl;
  }

  os << indent;
  os << begl << "AVBufferRef *buf: " << value->buf;
  os << begl << "int64_t pts: " << value->pts << std::endl;
  os << begl << "int64_t dts: " << value->dts << std::endl;
  os << begl
     << "uint8_t *data: " << (value->data == nullptr ? "<nullptr>" : "<opaque>")
     << std::endl;
  os << begl << "int size: " << value->size << std::endl;
  os << begl << "int stream_index: " << value->stream_index << std::endl;
  os << begl << "int flags: " << value->flags << std::endl;
  os << begl << "AVPacketSideData *side_data: " << value->side_data;
  os << begl << "int side_data_elems: " << value->side_data_elems << std::endl;
  os << begl << "int duration: " << value->duration << std::endl;
  os << begl << "int64_t pos: " << value->pos << std::endl;
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVPacketSideData* value) {
  if (value == nullptr) {
    return os << "<nullptr>" << std::endl;
  } else {
    return os << "TODO" << std::endl;
  }
}

std::ostream& operator<<(std::ostream& os, const AVPacketSideDataArray& value) {
  if (value.items_ == nullptr) {
    return os << "<nullptr>" << std::endl;
  } else if (value.count_ == 0) {
    return os << "<empty>" << std::endl;
  } else {
    os << std::endl;
  }

  os << indent;
  for (unsigned int i = 0; i < value.count_; i++) {
    os << begl << "[" << i << "] " << &value.items_[i];
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVProgram* value) {
  if (value == nullptr) {
    return os << "<nullptr>" << std::endl;
  } else {
    return os << "TODO" << std::endl;
  }
}

std::ostream& operator<<(std::ostream& os, const AVProgramArray& value) {
  if (value.items_ == nullptr) {
    return os << "<nullptr>" << std::endl;
  } else if (value.count_ == 0) {
    return os << "<empty>" << std::endl;
  } else {
    os << std::endl;
  }

  os << indent;
  for (unsigned int i = 0; i < value.count_; i++) {
    os << begl << "[" << i << "]" << value.items_[i];
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, const AVChapter* value) {
  if (value == nullptr) {
    return os << "<nullptr>" << std::endl;
  } else {
    return os << "TODO" << std::endl;
  }
}

std::ostream& operator<<(std::ostream& os, const AVChapterArray& value) {
  if (value.items_ == nullptr) {
    return os << "<nullptr>" << std::endl;
  } else if (value.count_ == 0) {
    return os << "<empty>" << std::endl;
  } else {
    os << std::endl;
  }

  os << indent;
  for (unsigned int i = 0; i < value.count_; i++) {
    os << begl << "[" << i << "]" << value.items_[i];
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, AVCodecID value) {
  return os << avcodec_get_name(value) << " (" << static_cast<int>(value) << ")"
            << std::endl;
}

std::ostream& operator<<(std::ostream& os, const AVDictionary* value) {
  if (value == nullptr) {
    return os << "<nullptr>" << std::endl;
  }
  AVDictionaryEntry* entry =
      av_dict_get(value, "", nullptr, AV_DICT_IGNORE_SUFFIX);
  if (entry == nullptr) {
    return os << "<empty>" << std::endl;
  }
  os << std::endl;

  os << indent;
  while (entry != nullptr) {
    os << begl << safe(entry->key) << ": " << safe(entry->value) << std::endl;
    entry = av_dict_get(value, "", entry, AV_DICT_IGNORE_SUFFIX);
  }
  return os << outdent;
}

std::ostream& operator<<(std::ostream& os, AVFMT_EVENTFlags value) {
  if (value.flags_ == 0) {
    os << "<none>" << std::endl;
    return os;
  }

  if (value.flags_ & AVFMT_EVENT_FLAG_METADATA_UPDATED) {
    return os << "AVFMT_EVENT_FLAG_METADATA_UPDATED" << std::endl;
  } else {
    return os << "<UNKNOWN AVFMT_EVENT_FLAG_: " << value.flags_ << ">"
              << std::endl;
  }
}

std::ostream& operator<<(std::ostream& os, AVSTREAM_EVENTFlags value) {
  if (value.flags_ == 0) {
    os << "<none>" << std::endl;
    return os;
  }

  if (value.flags_ & AVSTREAM_EVENT_FLAG_METADATA_UPDATED) {
    return os << "AVSTREAM_EVENT_FLAG_METADATA_UPDATED" << std::endl;
  } else {
    return os << "<UNKNOWN AVSTREAM_EVENT_FLAG_: " << value.flags_ << ">"
              << std::endl;
  }
}

std::ostream& operator<<(std::ostream& os, AVFMT_AVOID_NEG_TSFlags value) {
  switch (value.flags_) {
    case AVFMT_AVOID_NEG_TS_AUTO:
      return os << "AVFMT_AVOID_NEG_TS_AUTO" << std::endl;
    case AVFMT_AVOID_NEG_TS_MAKE_NON_NEGATIVE:
      return os << "AVFMT_AVOID_NEG_TS_MAKE_NON_NEGATIVE" << std::endl;
    case AVFMT_AVOID_NEG_TS_MAKE_ZERO:
      return os << "AVFMT_AVOID_NEG_TS_MAKE_ZERO" << std::endl;
    default:
      return os << "<UNKNOWN AVFMT_AVOID_NEG_TS_: " << value.flags_ << ">"
                << std::endl;
  }
}

std::ostream& operator<<(std::ostream& os, AVMediaType value) {
  switch (value) {
    case AVMEDIA_TYPE_UNKNOWN:
      return os << "AVMEDIA_TYPE_UNKNOWN" << std::endl;
    case AVMEDIA_TYPE_VIDEO:
      return os << "AVMEDIA_TYPE_VIDEO" << std::endl;
    case AVMEDIA_TYPE_AUDIO:
      return os << "AVMEDIA_TYPE_AUDIO" << std::endl;
    case AVMEDIA_TYPE_DATA:
      return os << "AVMEDIA_TYPE_DATA" << std::endl;
    case AVMEDIA_TYPE_SUBTITLE:
      return os << "AVMEDIA_TYPE_SUBTITLE" << std::endl;
    case AVMEDIA_TYPE_ATTACHMENT:
      return os << "AVMEDIA_TYPE_ATTACHMENT" << std::endl;
    case AVMEDIA_TYPE_NB:
      return os << "AVMEDIA_TYPE_NB" << std::endl;
    default:
      return os << "<UNKNOWN AVMediaType: " << static_cast<int>(value) << ">"
                << std::endl;
  }
}

std::ostream& operator<<(std::ostream& os, AVSampleFormat value) {
  switch (value) {
    case AV_SAMPLE_FMT_NONE:
      return os << "AV_SAMPLE_FMT_NONE" << std::endl;
    case AV_SAMPLE_FMT_U8:
      return os << "AV_SAMPLE_FMT_U8" << std::endl;
    case AV_SAMPLE_FMT_S16:
      return os << "AV_SAMPLE_FMT_S16" << std::endl;
    case AV_SAMPLE_FMT_S32:
      return os << "AV_SAMPLE_FMT_S32" << std::endl;
    case AV_SAMPLE_FMT_FLT:
      return os << "AV_SAMPLE_FMT_FLT" << std::endl;
    case AV_SAMPLE_FMT_DBL:
      return os << "AV_SAMPLE_FMT_DBL" << std::endl;
    case AV_SAMPLE_FMT_U8P:
      return os << "AV_SAMPLE_FMT_U8P" << std::endl;
    case AV_SAMPLE_FMT_S16P:
      return os << "AV_SAMPLE_FMT_S16P" << std::endl;
    case AV_SAMPLE_FMT_S32P:
      return os << "AV_SAMPLE_FMT_S32P" << std::endl;
    case AV_SAMPLE_FMT_FLTP:
      return os << "AV_SAMPLE_FMT_FLTP" << std::endl;
    case AV_SAMPLE_FMT_DBLP:
      return os << "AV_SAMPLE_FMT_DBLP" << std::endl;
    case AV_SAMPLE_FMT_NB:
      return os << "AV_SAMPLE_FMT_NB" << std::endl;
    default:
      return os << "<UNKNOWN AVSampleFormat: " << static_cast<int>(value) << ">"
                << std::endl;
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
      return os << "AVDISCARD_NONE" << std::endl;
    case AVDISCARD_DEFAULT:
      return os << "AVDISCARD_DEFAULT" << std::endl;
    case AVDISCARD_NONREF:
      return os << "AVDISCARD_NONREF" << std::endl;
    case AVDISCARD_BIDIR:
      return os << "AVDISCARD_BIDIR" << std::endl;
    case AVDISCARD_NONINTRA:
      return os << "AVDISCARD_NONINTRA" << std::endl;
    case AVDISCARD_NONKEY:
      return os << "AVDISCARD_NONKEY" << std::endl;
    case AVDISCARD_ALL:
      return os << "AVDISCARD_ALL" << std::endl;
    default:
      return os << "<UNKNOWN AVDISCARD_: " << static_cast<int>(value) << ">"
                << std::endl;
  }
}

std::ostream& operator<<(std::ostream& os, AVDurationEstimationMethod value) {
  switch (value) {
    case AVFMT_DURATION_FROM_PTS:
      return os << "AVFMT_DURATION_FROM_PTS" << std::endl;
    case AVFMT_DURATION_FROM_STREAM:
      return os << "AVFMT_DURATION_FROM_STREAM" << std::endl;
    case AVFMT_DURATION_FROM_BITRATE:
      return os << "AVFMT_DURATION_FROM_BITRATE" << std::endl;
    default:
      return os << "<UNKNOWN AVDurationEstimationMethod: "
                << static_cast<int>(value) << ">" << std::endl;
  }
}

std::ostream& operator<<(std::ostream& os, const AVFormatContext* value) {
  if (value == nullptr) {
    return os << "<nullptr>" << std::endl;
  } else {
    os << std::endl;
  }

  os << indent;
  os << begl << "AVInputFormat *iformat: " << value->iformat;
  os << begl << "AVOutputFormat *oformat: " << value->oformat;
  os << begl << "AVIOContext *pb: " << value->pb;
  os << begl << "int ctx_flags: " << AVFMTCTXFlags(value->ctx_flags);
  os << begl << "unsigned int nb_streams: " << value->nb_streams << std::endl;
  os << begl << "AVStream **streams: " << AVStreamArray(value->streams,
                                                        value->nb_streams);
  os << begl << "char filename[1024]: " << value->filename << std::endl;
  os << begl << "int64_t start_time: " << value->start_time << std::endl;
  os << begl << "int64_t duration: " << value->duration << std::endl;
  os << begl << "int64_t bit_rate: " << value->bit_rate << std::endl;
  os << begl << "unsigned int packet_size: " << value->packet_size << std::endl;
  os << begl << "int max_delay: " << value->max_delay << std::endl;
  os << begl << "int flags: " << AVFMTFlags(value->flags);
  os << begl << "int64_t probesize: " << value->probesize << std::endl;
  os << begl << "unsigned int nb_programs: " << value->nb_programs << std::endl;
  os << begl << "AVProgram **programs: " << AVProgramArray(value->programs,
                                                           value->nb_programs);
  os << begl << "AVCodecID video_codec_id: " << value->video_codec_id;
  os << begl << "AVCodecID audio_codec_id: " << value->audio_codec_id;
  os << begl << "AVCodecID subtitle_codec_id: " << value->subtitle_codec_id;
  os << begl << "unsigned int max_index_size: " << value->max_index_size
     << std::endl;
  os << begl << "unsigned int max_picture_buffer: " << value->max_picture_buffer
     << std::endl;
  os << begl << "unsigned int nb_chapters: " << value->nb_chapters << std::endl;
  os << begl << "AVChapter **chapters: " << AVChapterArray(value->chapters,
                                                           value->nb_chapters);
  os << begl << "AVDictionary *metadata: " << value->metadata;
  os << begl << "int64_t start_time_realtime: " << value->start_time_realtime
     << std::endl;
  os << begl << "int fps_probe_size: " << value->fps_probe_size << std::endl;
  os << begl << "int error_recognition: " << value->error_recognition
     << std::endl;
  os << begl << "int64_t max_interleave_delta: " << value->max_interleave_delta
     << std::endl;
  os << begl << "int strict_std_compliance: " << value->strict_std_compliance
     << std::endl;
  os << begl << "int event_flags: " << AVFMT_EVENTFlags(value->flags);
  os << begl << "int max_ts_probe: " << value->max_ts_probe << std::endl;
  os << begl << "int avoid_negative_ts: "
     << AVFMT_AVOID_NEG_TSFlags(value->avoid_negative_ts);
  os << begl << "int ts_id: " << value->ts_id << std::endl;
  os << begl << "int audio_preload: " << value->audio_preload << std::endl;
  os << begl << "int max_chunk_duration: " << value->max_chunk_duration
     << std::endl;
  os << begl << "int max_chunk_size: " << value->max_chunk_size << std::endl;
  os << begl << "int use_wallclock_as_timestamps: "
     << value->use_wallclock_as_timestamps << std::endl;
  os << begl << "int avio_flags: " << value->avio_flags << std::endl;
  os << begl << "AVDurationEstimationMethod duration_estimation_method: "
     << value->duration_estimation_method;
  os << begl << "int64_t skip_initial_bytes: " << value->skip_initial_bytes
     << std::endl;
  os << begl << "TODO(dalesat): more" << std::endl;
  return os << outdent;
}

}  // namespace media

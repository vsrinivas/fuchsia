// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_FFMPEG_FFMPEG_FORMATTING_H_
#define GARNET_BIN_MEDIAPLAYER_FFMPEG_FFMPEG_FORMATTING_H_

#include <ostream>

#include "garnet/bin/mediaplayer/framework/formatting.h"
extern "C" {
#include "libavformat/avformat.h"
}

namespace media_player {

// See services/media/framework/ostream.h for details.

std::ostream& operator<<(std::ostream& os,
                         const struct AVCodecTag* const* value);
std::ostream& operator<<(std::ostream& os, const AVInputFormat* value);
std::ostream& operator<<(std::ostream& os, const AVOutputFormat* value);
std::ostream& operator<<(std::ostream& os, const AVIOContext* value);
std::ostream& operator<<(std::ostream& os, const AVCodecContext* value);
std::ostream& operator<<(std::ostream& os, const AVCodec* value);
std::ostream& operator<<(std::ostream& os, const AVRational& value);
std::ostream& operator<<(std::ostream& os, const AVStream* value);
std::ostream& operator<<(std::ostream& os, const AVBufferRef* value);
std::ostream& operator<<(std::ostream& os, const AVFrame* value);
std::ostream& operator<<(std::ostream& os, const AVPacket* value);
std::ostream& operator<<(std::ostream& os, const AVPacketSideData* value);
std::ostream& operator<<(std::ostream& os, const AVProgram* value);
std::ostream& operator<<(std::ostream& os, const AVChapter* value);
std::ostream& operator<<(std::ostream& os, AVCodecID value);
std::ostream& operator<<(std::ostream& os, const AVDictionary* value);
std::ostream& operator<<(std::ostream& os, enum AVDiscard value);
std::ostream& operator<<(std::ostream& os, AVDurationEstimationMethod value);
std::ostream& operator<<(std::ostream& os, const AVFormatContext* value);
std::ostream& operator<<(std::ostream& os, AVMediaType value);
std::ostream& operator<<(std::ostream& os, AVSampleFormat value);
std::ostream& operator<<(std::ostream& os, AVColorSpace value);

struct AVPacketSideDataArray {
  AVPacketSideDataArray(const AVPacketSideData* items, unsigned int count)
      : items_(items), count_(count) {}
  const AVPacketSideData* items_;
  unsigned int count_;
};
std::ostream& operator<<(std::ostream& os, const AVPacketSideDataArray& value);

struct AVProgramArray {
  AVProgramArray(AVProgram** items, unsigned int count)
      : items_(items), count_(count) {}
  AVProgram** items_;
  unsigned int count_;
};
std::ostream& operator<<(std::ostream& os, const AVProgramArray& value);

struct AVChapterArray {
  AVChapterArray(AVChapter** items, unsigned int count)
      : items_(items), count_(count) {}
  AVChapter** items_;
  unsigned int count_;
};
std::ostream& operator<<(std::ostream& os, const AVChapterArray& value);

struct AVStreamArray {
  AVStreamArray(AVStream** items, unsigned int count)
      : items_(items), count_(count) {}
  AVStream** items_;
  unsigned int count_;
};
std::ostream& operator<<(std::ostream& os, const AVStreamArray& value);

struct AVFMTFlags {
  AVFMTFlags(int flags) : flags_(flags) {}
  int flags_;
};
std::ostream& operator<<(std::ostream& os, AVFMTFlags value);

struct AVFMTCTXFlags {
  AVFMTCTXFlags(int flags) : flags_(flags) {}
  int flags_;
};
std::ostream& operator<<(std::ostream& os, AVFMTCTXFlags value);

struct AV_DISPOSITIONFlags {
  AV_DISPOSITIONFlags(int flags) : flags_(flags) {}
  int flags_;
};
std::ostream& operator<<(std::ostream& os, AV_DISPOSITIONFlags value);

struct AVFMT_EVENTFlags {
  AVFMT_EVENTFlags(int flags) : flags_(flags) {}
  int flags_;
};
std::ostream& operator<<(std::ostream& os, AVFMT_EVENTFlags value);

struct AVSTREAM_EVENTFlags {
  AVSTREAM_EVENTFlags(int flags) : flags_(flags) {}
  int flags_;
};
std::ostream& operator<<(std::ostream& os, AVSTREAM_EVENTFlags value);

struct AVFMT_AVOID_NEG_TSFlags {
  AVFMT_AVOID_NEG_TSFlags(int flags) : flags_(flags) {}
  int flags_;
};
std::ostream& operator<<(std::ostream& os, AVFMT_AVOID_NEG_TSFlags value);

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_FFMPEG_FFMPEG_FORMATTING_H_

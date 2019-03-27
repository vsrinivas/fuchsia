// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_FFMPEG_FFMPEG_DECODER_BASE_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_FFMPEG_FFMPEG_DECODER_BASE_H_

#include <limits>

#include <lib/async-loop/cpp/loop.h>

#include "src/media/playback/mediaplayer_tmp/decode/software_decoder.h"
#include "src/media/playback/mediaplayer_tmp/ffmpeg/av_codec_context.h"
#include "src/media/playback/mediaplayer_tmp/ffmpeg/av_frame.h"
#include "src/media/playback/mediaplayer_tmp/ffmpeg/av_packet.h"
extern "C" {
#include "libavcodec/avcodec.h"
}

namespace media_player {

// Abstract base class for ffmpeg-based decoders.
class FfmpegDecoderBase : public SoftwareDecoder {
 public:
  FfmpegDecoderBase(AvCodecContextPtr av_codec_context);

  ~FfmpegDecoderBase() override;

  // Decoder implementation.
  std::unique_ptr<StreamType> output_stream_type() const override;

  // Node implementation.
  void Dump(std::ostream& os) const override;

 protected:
  // SoftwareDecoder overrides.
  void Flush() override;

  bool TransformPacket(const PacketPtr& input, bool new_input,
                       PacketPtr* output) override;

  // Called when a new input packet is about to be processed. The default
  // implementation does nothing.
  virtual void OnNewInputPacket(const PacketPtr& packet);

  // Fills in |av_frame|, probably using an |AVBuffer| allocated via
  // CreateAVBuffer. |av_codec_context| may be distinct from context() and
  // should be used when a codec context is required.
  virtual int BuildAVFrame(const AVCodecContext& av_codec_context,
                           AVFrame* av_frame) = 0;

  // Creates a Packet from av_frame.
  virtual PacketPtr CreateOutputPacket(
      const AVFrame& av_frame, fbl::RefPtr<PayloadBuffer> payload_buffer) = 0;

  // The ffmpeg codec context.
  const AvCodecContextPtr& context() { return av_codec_context_; }

  // Gets the current 'next PTS' value.
  int64_t next_pts() { return next_pts_; }

  // Sets the next PTS value. This is used by this class to create an
  // end-of-stream packet. Subclasses may also use it as needed.
  void set_next_pts(int64_t value) { next_pts_ = value; }

  // Gets the current PTS rate value.
  media::TimelineRate pts_rate() { return pts_rate_; }

  // Gets the PTS rate value.
  void set_pts_rate(media::TimelineRate value) { pts_rate_ = value; }

  // Creates an AVBuffer from a |PayloadBuffer|. The |AVBuffer| referenced by
  // the returned |AVBufferRef| references the |payload_buffer|, so the
  // |AVBuffer| won't outlive the |PayloadBuffer|.
  AVBufferRef* CreateAVBuffer(fbl::RefPtr<PayloadBuffer> payload_buffer);

 private:
  // Callback used by the ffmpeg decoder to acquire a buffer.
  static int AllocateBufferForAvFrame(AVCodecContext* av_codec_context,
                                      AVFrame* av_frame, int flags);

  // Callback used by the ffmpeg decoder to release a buffer.
  static void ReleaseBufferForAvFrame(void* opaque, uint8_t* buffer);

  // Sends |input| to the ffmpeg decoder and returns the result of
  // |avcodec_send_packet|. A return value of 0 indicates success.
  int SendPacket(const PacketPtr& input);

  // Creates an end-of-stream packet.
  PacketPtr CreateEndOfStreamPacket();

  AvCodecContextPtr av_codec_context_;
  ffmpeg::AvFramePtr av_frame_ptr_;
  int64_t next_pts_ = Packet::kNoPts;
  media::TimelineRate pts_rate_;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_FFMPEG_FFMPEG_DECODER_BASE_H_

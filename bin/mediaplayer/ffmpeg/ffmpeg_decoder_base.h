// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_FFMPEG_FFMPEG_DECODER_BASE_H_
#define GARNET_BIN_MEDIAPLAYER_FFMPEG_FFMPEG_DECODER_BASE_H_

#include <limits>

#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/mediaplayer/decode/software_decoder.h"
#include "garnet/bin/mediaplayer/ffmpeg/av_codec_context.h"
#include "garnet/bin/mediaplayer/ffmpeg/av_frame.h"
#include "garnet/bin/mediaplayer/ffmpeg/av_packet.h"
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

  // AsyncNode implementation.
  void Dump(std::ostream& os) const override;

 protected:
  class DecoderPacket : public Packet {
   public:
    static PacketPtr Create(int64_t pts, media::TimelineRate pts_rate,
                            bool keyframe, AVBufferRef* av_buffer_ref,
                            FfmpegDecoderBase* owner) {
      return std::make_shared<DecoderPacket>(pts, pts_rate, keyframe,
                                             av_buffer_ref, owner);
    }

    ~DecoderPacket() override;

    DecoderPacket(int64_t pts, media::TimelineRate pts_rate, bool keyframe,
                  AVBufferRef* av_buffer_ref, FfmpegDecoderBase* owner)
        : Packet(pts, pts_rate, keyframe, false,
                 static_cast<size_t>(av_buffer_ref->size), av_buffer_ref->data),
          av_buffer_ref_(av_buffer_ref),
          owner_(owner) {
      FXL_DCHECK(av_buffer_ref->size >= 0);
    }

   private:
    AVBufferRef* av_buffer_ref_;
    FfmpegDecoderBase* owner_;
  };

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
  virtual int BuildAVFrame(
      const AVCodecContext& av_codec_context, AVFrame* av_frame,
      const std::shared_ptr<PayloadAllocator>& allocator) = 0;

  // Creates a Packet from av_frame.
  virtual PacketPtr CreateOutputPacket(
      const AVFrame& av_frame,
      const std::shared_ptr<PayloadAllocator>& allocator) = 0;

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

  // Creates an AVBuffer.
  AVBufferRef* CreateAVBuffer(
      uint8_t* payload_buffer, size_t payload_buffer_size,
      const std::shared_ptr<PayloadAllocator>& allocator) {
    FXL_DCHECK(payload_buffer_size <=
               static_cast<size_t>(std::numeric_limits<int>::max()));
    return av_buffer_create(
        payload_buffer, static_cast<int>(payload_buffer_size),
        ReleaseBufferForAvFrame, allocator.get(), /* flags */ 0);
  }

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
  int64_t next_pts_ = Packet::kUnknownPts;
  media::TimelineRate pts_rate_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_FFMPEG_FFMPEG_DECODER_BASE_H_

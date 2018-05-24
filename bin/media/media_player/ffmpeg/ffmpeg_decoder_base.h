// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_FFMPEG_FFMPEG_DECODER_BASE_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_FFMPEG_FFMPEG_DECODER_BASE_H_

#include <atomic>
#include <limits>

#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/media/media_player/decode/decoder.h"
#include "garnet/bin/media/media_player/ffmpeg/av_codec_context.h"
#include "garnet/bin/media/media_player/ffmpeg/av_frame.h"
#include "garnet/bin/media/media_player/ffmpeg/av_packet.h"
#include "garnet/bin/media/media_player/metrics/value_tracker.h"
extern "C" {
#include "third_party/ffmpeg/libavcodec/avcodec.h"
}

namespace media_player {

// Abstract base class for ffmpeg-based decoders.
class FfmpegDecoderBase : public Decoder {
 public:
  FfmpegDecoderBase(AvCodecContextPtr av_codec_context);

  ~FfmpegDecoderBase() override;

  // Decoder implementation.
  std::unique_ptr<StreamType> output_stream_type() const override;

  // AsyncNode implementation.
  void Dump(std::ostream& os) const override;

  void GetConfiguration(size_t* input_count, size_t* output_count) override;

  void FlushInput(bool hold_frame, size_t input_index,
                  fxl::Closure callback) override;

  void FlushOutput(size_t output_index, fxl::Closure callback) override;

  std::shared_ptr<PayloadAllocator> allocator_for_input(
      size_t input_index) override;

  void PutInputPacket(PacketPtr packet, size_t input_index) override;

  bool can_accept_allocator_for_output(size_t output_index) const override;

  void SetAllocatorForOutput(std::shared_ptr<PayloadAllocator> allocator,
                             size_t output_index) override;

  void RequestOutputPacket() override;

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

  // Called when a new input packet is about to be processed. The default
  // implementation does nothing.
  virtual void OnNewInputPacket(const PacketPtr& packet);

  // Fills in |av_frame|, probably using an |AVBuffer| allocated via
  // CreateAVBuffer. |av_codec_context| may be distinct from context() and
  // should be used when a codec context is required.
  virtual int BuildAVFrame(const AVCodecContext& av_codec_context,
                           AVFrame* av_frame, PayloadAllocator* allocator) = 0;

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
  AVBufferRef* CreateAVBuffer(uint8_t* payload_buffer,
                              size_t payload_buffer_size,
                              PayloadAllocator* allocator) {
    FXL_DCHECK(payload_buffer_size <=
               static_cast<size_t>(std::numeric_limits<int>::max()));
    return av_buffer_create(payload_buffer,
                            static_cast<int>(payload_buffer_size),
                            ReleaseBufferForAvFrame, allocator, /* flags */ 0);
  }

 private:
  enum class State { kIdle, kOutputPacketRequested, kEndOfStream };

  // Callback used by the ffmpeg decoder to acquire a buffer.
  static int AllocateBufferForAvFrame(AVCodecContext* av_codec_context,
                                      AVFrame* av_frame, int flags);

  // Callback used by the ffmpeg decoder to release a buffer.
  static void ReleaseBufferForAvFrame(void* opaque, uint8_t* buffer);

  // Transforms an input packet. Called on the worker thread.
  void TransformPacket(PacketPtr packet);

  // Creates an end-of-stream packet.
  PacketPtr CreateEndOfStreamPacket();

  AvCodecContextPtr av_codec_context_;
  async::Loop worker_loop_;
  ffmpeg::AvFramePtr av_frame_ptr_;
  int64_t next_pts_ = Packet::kUnknownPts;
  media::TimelineRate pts_rate_;
  std::atomic<State> state_;
  bool flushing_ = false;

  // The allocator used by avcodec_send_packet and avcodec_receive_frame to
  // provide context for AllocateBufferForAvFrame.
  std::shared_ptr<PayloadAllocator> allocator_;

  ValueTracker<int64_t> decode_duration_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_FFMPEG_FFMPEG_DECODER_BASE_H_

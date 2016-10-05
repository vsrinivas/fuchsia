// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_FRAMEWORK_FFMPEG_FFMPEG_DECODER_BASE_H_
#define APPS_MEDIA_SERVICES_FRAMEWORK_FFMPEG_FFMPEG_DECODER_BASE_H_

#include "apps/media/services/framework/parts/decoder.h"
#include "apps/media/services/framework_ffmpeg/av_codec_context.h"
#include "apps/media/services/framework_ffmpeg/av_frame.h"
#include "apps/media/services/framework_ffmpeg/av_packet.h"
extern "C" {
#include "third_party/ffmpeg/libavcodec/avcodec.h"
}

namespace mojo {
namespace media {

// Abstract base class for ffmpeg-based decoders.
class FfmpegDecoderBase : public Decoder {
 public:
  FfmpegDecoderBase(AvCodecContextPtr av_codec_context);

  ~FfmpegDecoderBase() override;

  // Decoder implementation.
  std::unique_ptr<StreamType> output_stream_type() override;

  // Transform implementation.
  void Flush() override;

  bool TransformPacket(const PacketPtr& input,
                       bool new_input,
                       PayloadAllocator* allocator,
                       PacketPtr* output) override;

 protected:
  class DecoderPacket : public Packet {
   public:
    static PacketPtr Create(int64_t pts,
                            TimelineRate pts_rate,
                            AVBufferRef* av_buffer_ref) {
      return PacketPtr(new DecoderPacket(pts, pts_rate, av_buffer_ref));
    }

   protected:
    ~DecoderPacket() override;

    void Release() override;

   private:
    DecoderPacket(int64_t pts,
                  TimelineRate pts_rate,
                  AVBufferRef* av_buffer_ref)
        : Packet(pts,
                 pts_rate,
                 false,
                 static_cast<size_t>(av_buffer_ref->size),
                 av_buffer_ref->data),
          av_buffer_ref_(av_buffer_ref) {
      FTL_DCHECK(av_buffer_ref->size >= 0);
    }

    AVBufferRef* av_buffer_ref_;
  };

  // Decodes from av_packet into av_frame_ptr. The result indicates how many
  // bytes were consumed from av_packet_. *frame_decoded_out indicates whether
  // av_frame_ptr contains a complete frame.
  virtual int Decode(const AVPacket& av_packet,
                     const ffmpeg::AvFramePtr& av_frame_ptr,
                     PayloadAllocator* allocator,
                     bool* frame_decoded_out) = 0;

  // Creates a Packet from av_frame.
  virtual PacketPtr CreateOutputPacket(const AVFrame& av_frame,
                                       PayloadAllocator* allocator) = 0;

  // Creates an end-of-stream packet with no payload.
  virtual PacketPtr CreateOutputEndOfStreamPacket() = 0;

  // The ffmpeg codec context.
  const AvCodecContextPtr& context() { return av_codec_context_; }

 private:
  // Prepares to process a new input packet.
  void PrepareInputPacket(const PacketPtr& input);

  // Finishes up after processing of an input packet has completed, possibly
  // producing a zero-size end-of-stream packet. Returns true to indicate that
  // a new input packet is required.
  bool UnprepareInputPacket(const PacketPtr& input, PacketPtr* output);

  AvCodecContextPtr av_codec_context_;
  AVPacket av_packet_;
  ffmpeg::AvFramePtr av_frame_ptr_;
};

}  // namespace media
}  // namespace mojo

#endif  // APPS_MEDIA_SERVICES_FRAMEWORK_FFMPEG_FFMPEG_DECODER_BASE_H_

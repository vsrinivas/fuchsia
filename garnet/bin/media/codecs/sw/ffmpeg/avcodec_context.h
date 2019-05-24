// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_AVCODEC_CONTEXT_H_
#define GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_AVCODEC_CONTEXT_H_

#include <optional>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/imgutils.h"
}

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/media/codec_impl/codec_packet.h>
#include <src/lib/fxl/macros.h>

// Wraps AVCodecContext type from ffmpeg.
class AvCodecContext {
 public:
  // Describes the requirements of a buffer which can back a frame.
  struct FrameBufferRequest {
    fuchsia::media::VideoUncompressedFormat format;
    size_t buffer_bytes_needed;
  };

  using GetBufferCallback = ::fit::function<int(
      const FrameBufferRequest& frame_buffer_request,
      AVCodecContext* avcodec_context, AVFrame* frame, int flags)>;

  using AVFramePtr = ::std::unique_ptr<AVFrame, fit::function<void(AVFrame*)>>;

  // Creates a decoder context. The decoder context can be used to decode an
  // elementary stream with successive calls to SendPacket() and
  // ReceiveFrame() in a loop.
  //
  // In the case of error, std::nullopt is returned.
  //
  // A decoder can decode one stream at most. A new decoder context should
  // be created for new streams.
  //
  // The get_buffer_callback must provide buffers for each frame. To claim
  // buffers back when the decoder is done referencing them, set up a free
  // callback in the AVBufferRef provided to each frame.
  //
  // Calls to get_buffer_callback may also be redirected to
  // avcodec_default_get_buffer2 if you have an error state and just want ffmpeg
  // to gracefully conclude its work.
  //
  // See ffmpeg's get_buffer2 and av_buffer_create for more details.
  static std::optional<std::unique_ptr<AvCodecContext>> CreateDecoder(
      const fuchsia::media::FormatDetails& format_details,
      GetBufferCallback get_buffer_callback);

  // Sends a compressed packet to the decoder. The semantics of SendPacket and
  // ReceiveFrame mirror those of avcodec_send_packet and avcodec_receive_frame
  // of ffmpeg. Returns an ffmpeg return code.
  int SendPacket(const CodecPacket* codec_packet);

  // Receives a frame from ffmpeg decoder, paired with its ffmpeg return code.
  std::pair<int, AVFramePtr> ReceiveFrame();

  // No further packets may be sent to the decoder after this call. Input data
  // is not discarded and should still be received with calls to ReceiveFrame
  // until it is all received. Returns an ffmpeg return code.
  int EndStream();

 private:
  // Returns info on the decoded output so it can be displayed and buffers can
  // be allocated for it.
  FrameBufferRequest frame_buffer_request(AVFrame* frame) const;

  static int GetBufferCallbackRouter(AVCodecContext* avcodec_context,
                                     AVFrame* frame, int flag);

  int GetBufferHandler(AVCodecContext* avcodec_context, AVFrame* frame,
                       int flag);

  // Takes ownership of ffmpeg's AVCodecContext type (note uppercase V).
  AvCodecContext(
      std::unique_ptr<AVCodecContext, fit::function<void(AVCodecContext*)>>
          avcodec_context,
      GetBufferCallback get_buffer_callback);

  // ffmpeg's AVCodecContext (note uppercase V).
  std::unique_ptr<AVCodecContext, fit::function<void(AVCodecContext*)>>
      avcodec_context_;

  // callback to get buffers for decoding.
  GetBufferCallback get_buffer_callback_;

  AvCodecContext() = delete;
  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(AvCodecContext);
};

#endif  // GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_AVCODEC_CONTEXT_H_

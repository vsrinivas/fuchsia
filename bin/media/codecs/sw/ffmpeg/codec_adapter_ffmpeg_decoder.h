// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_CODEC_ADAPTER_FFMPEG_DECODER_H_
#define GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_CODEC_ADAPTER_FFMPEG_DECODER_H_

#include <threads.h>
#include <optional>
#include <queue>

#include <lib/async-loop/cpp/loop.h>
#include <lib/fxl/synchronization/thread_annotations.h>
#include <lib/media/codec_impl/codec_adapter.h>
#include <lib/media/codec_impl/codec_input_item.h>

#include "avcodec_context.h"
#include "mpsc_queue.h"

class CodecAdapterFfmpegDecoder : public CodecAdapter {
 public:
  CodecAdapterFfmpegDecoder(std::mutex& lock,
                            CodecAdapterEvents* codec_adapter_events);
  ~CodecAdapterFfmpegDecoder();

  bool IsCoreCodecRequiringOutputConfigForFormatDetection() override;
  void CoreCodecInit(const fuchsia::media::FormatDetails&
                         initial_input_format_details) override;
  void CoreCodecStartStream() override;
  void CoreCodecQueueInputFormatDetails(
      const fuchsia::media::FormatDetails& per_stream_override_format_details)
      override;
  void CoreCodecQueueInputPacket(CodecPacket* packet) override;
  void CoreCodecQueueInputEndOfStream() override;
  void CoreCodecStopStream() override;
  void CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) override;
  void CoreCodecConfigureBuffers(
      CodecPort port,
      const std::vector<std::unique_ptr<CodecPacket>>& packets) override;
  void CoreCodecRecycleOutputPacket(CodecPacket* packet) override;
  void CoreCodecEnsureBuffersNotConfigured(CodecPort port) override;
  std::unique_ptr<const fuchsia::media::StreamOutputConfig>
  CoreCodecBuildNewOutputConfig(
      uint64_t stream_lifetime_ordinal,
      uint64_t new_output_buffer_constraints_version_ordinal,
      uint64_t new_output_format_details_version_ordinal,
      bool buffer_constraints_action_required) override;
  void CoreCodecMidStreamOutputBufferReConfigPrepare() override;
  void CoreCodecMidStreamOutputBufferReConfigFinish() override;

 private:
  struct BufferAllocation {
    const CodecBuffer* buffer;
    size_t bytes_used;
  };

  // Reads the opaque pointer from our free callback and routes it to our
  // instance. The opaque pointer is provided when we set up a free callback
  // when providing buffers to the decoder in GetBuffer.
  static void BufferFreeCallbackRouter(void* opaque, uint8_t* data);

  // A callback handler for when buffers are freed by the decoder, which returns
  // them to our pool. The opaque pointer is provided when we set up a free
  // callback when providing buffers to the decoder in GetBuffer.
  void BufferFreeHandler(uint8_t* data);

  // Processes input in a loop. Should only execute on input_processing_thread_.
  // Loops for the lifetime of a stream.
  void ProcessInputLoop();

  // Allocates buffer for a frame for ffmpeg.
  int GetBuffer(const AvCodecContext::DecodedOutputInfo& decoded_output_info,
                AVCodecContext* avcodec_context, AVFrame* frame, int flags);

  // Decodes frames until the decoder is empty.
  void DecodeFrames();

  void WaitForInputProcessingLoopToEnd();

  BlockingMpscQueue<CodecInputItem> input_queue_;
  BlockingMpscQueue<const CodecBuffer*> free_output_buffers_;
  BlockingMpscQueue<CodecPacket*> free_output_packets_;
  std::optional<AvCodecContext::DecodedOutputInfo> decoded_output_info_
      FXL_GUARDED_BY(lock_);

  // When no references exist to our buffers in the decoder's refcounting
  // anymore, the decoder will execute our BufferFreeHandler that looks up our
  // buffer here and adds it to our free list.
  //
  // We also look here when frames come out of the decoder, to associate an
  // output packet with the the buffer.
  std::map<uint8_t*, BufferAllocation> in_use_by_decoder_ FXL_GUARDED_BY(lock_);
  // This keeps buffers alive via the decoder's refcount until the client is
  // done with them.
  std::map<CodecPacket*, AvCodecContext::AVFramePtr> in_use_by_client_
      FXL_GUARDED_BY(lock_);

  uint64_t input_format_details_version_ordinal_;

  async::Loop input_processing_loop_;
  thrd_t input_processing_thread_;
  std::unique_ptr<AvCodecContext> avcodec_context_;
};

#endif  // GARNET_BIN_MEDIA_CODECS_SW_FFMPEG_CODEC_ADAPTER_FFMPEG_DECODER_H_

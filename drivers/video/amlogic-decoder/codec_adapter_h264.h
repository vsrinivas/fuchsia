// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADAPTER_H264_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADAPTER_H264_H_

#include "codec_adapter.h"

#include <lib/async-loop/cpp/loop.h>

class AmlogicVideo;
class DeviceCtx;
struct VideoFrame;
class CodecAdapterH264 : public CodecAdapter {
 public:
  explicit CodecAdapterH264(std::mutex& lock,
                            CodecAdapterEvents* codec_adapter_events,
                            DeviceCtx* device);
  ~CodecAdapterH264();

  bool IsCoreCodecRequiringOutputConfigForFormatDetection() override;
  void CoreCodecInit(const fuchsia::mediacodec::CodecFormatDetails&
                         initial_input_format_details) override;
  void CoreCodecStartStream(std::unique_lock<std::mutex>& lock) override;
  void CoreCodecQueueInputFormatDetails(
      const fuchsia::mediacodec::CodecFormatDetails&
          per_stream_override_format_details) override;
  void CoreCodecQueueInputPacket(const CodecPacket* packet) override;
  void CoreCodecQueueInputEndOfStream() override;
  void CoreCodecStopStream(std::unique_lock<std::mutex>& lock) override;
  void CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) override;
  void CoreCodecConfigureBuffers(CodecPort port) override;
  void CoreCodecRecycleOutputPacketLocked(CodecPacket* packet) override;
  void CoreCodecEnsureBuffersNotConfiguredLocked(CodecPort port) override;
  std::unique_ptr<const fuchsia::mediacodec::CodecOutputConfig>
  CoreCodecBuildNewOutputConfig(
      uint64_t stream_lifetime_ordinal,
      uint64_t new_output_buffer_constraints_version_ordinal,
      uint64_t new_output_format_details_version_ordinal,
      bool buffer_constraints_action_required) override;
  void CoreCodecMidStreamOutputBufferReConfigPrepare(
      std::unique_lock<std::mutex>& lock) override;
  void CoreCodecMidStreamOutputBufferReConfigFinish(
      std::unique_lock<std::mutex>& lock) override;

 private:
  void PostSerial(async_dispatcher_t* dispatcher, fit::closure to_run);
  void PostToInputProcessingThread(fit::closure to_run);
  void PostToOutputProcessingThread(fit::closure to_run);
  void QueueInputItem(CodecInputItem input_item);
  CodecInputItem DequeueInputItem();
  void ProcessInput();
  void ProcessOutput();

  DeviceCtx* device_ = nullptr;
  AmlogicVideo* video_ = nullptr;

  fuchsia::mediacodec::CodecFormatDetails initial_input_format_details_;

  // Currently, AmlogicVideo::ParseVideo() can indirectly block on availability
  // of output buffers to make space in the ring buffer the parser is outputting
  // into, so avoid calling ParseVideo() on shared_fidl_thread() since the
  // shared_fidl_thread() is needed for output buffers to become available.  We
  // use processing_loop_ (aka processing_thread_) to call ParseVideo().
  //
  // Only StreamControl ever adds anything to input_queue_.  Only
  // processing_thread_ ever removes anything from input_queue_, including when
  // stopping.
  async::Loop input_processing_loop_;
  thrd_t input_processing_thread_ = 0;
  bool is_process_input_queued_ = false;

  // TODO(dustingreen): The sole purpose of the output_processing_thread_ is to
  // copy ouput frame data, so once we're no longer doing that, we can remove
  // these.
  async::Loop output_processing_loop_;
  thrd_t output_processing_thread_ = 0;
  bool is_process_output_queued_ = false;

  // Skip any further processing in ProcessInput().
  bool is_cancelling_input_processing_ = false;

  // Skip any further processing in ProcessOutput().
  bool is_cancelling_output_processing_ = false;

  // When there are no free_output_packets_, this list can build up some.
  //
  // TODO(dustingreen): This list shouldn't be needed once we're no longer
  // copying output frame data.  For the moment, copying of data from
  // ready_output_frames_ to free_output_packets_ occurs.  This copying is
  // performed on the separate output_processing_thread_, because because the
  // input_processing_thread_ needs output to flow to ensure that
  // video_->ParseVideo() doesn't time out just because no output room was
  // available.
  std::list<std::shared_ptr<VideoFrame>> ready_output_frames_;

  // Output packets that are free.
  std::list<CodecPacket*> free_output_packets_;

  FXL_DISALLOW_IMPLICIT_CONSTRUCTORS(CodecAdapterH264);
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADAPTER_H264_H_

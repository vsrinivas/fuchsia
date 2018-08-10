// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADAPTER_H264_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADAPTER_H264_H_

#include "codec_adapter.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/bti.h>

class AmlogicVideo;
struct CodecFrame;
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
  void CoreCodecStartStream() override;
  void CoreCodecQueueInputFormatDetails(
      const fuchsia::mediacodec::CodecFormatDetails&
          per_stream_override_format_details) override;
  void CoreCodecQueueInputPacket(const CodecPacket* packet) override;
  void CoreCodecQueueInputEndOfStream() override;
  void CoreCodecStopStream() override;
  void CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) override;
  void CoreCodecConfigureBuffers(
      CodecPort port,
      const std::vector<std::unique_ptr<CodecPacket>>& packets) override;
  void CoreCodecRecycleOutputPacket(CodecPacket* packet) override;
  void CoreCodecEnsureBuffersNotConfigured(CodecPort port) override;
  std::unique_ptr<const fuchsia::mediacodec::CodecOutputConfig>
  CoreCodecBuildNewOutputConfig(
      uint64_t stream_lifetime_ordinal,
      uint64_t new_output_buffer_constraints_version_ordinal,
      uint64_t new_output_format_details_version_ordinal,
      bool buffer_constraints_action_required) override;
  void CoreCodecMidStreamOutputBufferReConfigPrepare() override;
  void CoreCodecMidStreamOutputBufferReConfigFinish() override;

 private:
  void PostSerial(async_dispatcher_t* dispatcher, fit::closure to_run);
  void PostToInputProcessingThread(fit::closure to_run);
  void QueueInputItem(CodecInputItem input_item);
  CodecInputItem DequeueInputItem();
  void ProcessInput();
  zx_status_t InitializeFramesHandler(::zx::bti bti, uint32_t frame_count,
                                      uint32_t width, uint32_t height,
                                      uint32_t stride, uint32_t display_width,
                                      uint32_t display_height,
                                      std::vector<CodecFrame>* frames_out);

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

  // Skip any further processing in ProcessInput().
  bool is_cancelling_input_processing_ = false;

  std::vector<const CodecBuffer*> all_output_buffers_;
  std::vector<CodecPacket*> all_output_packets_;

  // True while CoreCodecStopStream() needs InitializeFramesHandler() to cancel
  // and return.
  bool is_stopping_ = false;
  // The value of this bool is only meaningful while InitializeFramesHandler()
  // is running.
  bool is_mid_stream_output_config_change_done_ = false;
  std::condition_variable wake_initialize_frames_handler_;

  uint32_t packet_count_total_ = 0;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint32_t stride_ = 0;
  uint32_t display_width_ = 0;
  uint32_t display_height_ = 0;

  // Output frames get a PTS based on looking up the output frame's input stream
  // offset via the PtsManager.  For that to work we have to feed the input PTSs
  // into the PtsManager by their input stream offset.  This member tracks the
  // cumulative input stream offset. This is implicitly the same count of bytes
  // so far that the amlogic firmware will accumulate and stamp on output
  // frames.  This counts all bytes delivered to the amlogic firmware, including
  // start code bytes.
  uint64_t parsed_video_size_ = 0;
  bool is_input_end_of_stream_queued_ = false;

  FXL_DISALLOW_IMPLICIT_CONSTRUCTORS(CodecAdapterH264);
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADAPTER_H264_H_

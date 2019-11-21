// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADAPTER_VP9_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADAPTER_VP9_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/media/codec_impl/codec_adapter.h>
#include <lib/zx/bti.h>

#include <random>

#include <fbl/macros.h>

#include "vp9_decoder.h"

class AmlogicVideo;
struct CodecFrame;
class DeviceCtx;
struct VideoFrame;

class CodecAdapterVp9 : public CodecAdapter, public Vp9Decoder::FrameDataProvider {
 public:
  explicit CodecAdapterVp9(std::mutex& lock, CodecAdapterEvents* codec_adapter_events,
                           DeviceCtx* device);
  ~CodecAdapterVp9();

  bool IsCoreCodecRequiringOutputConfigForFormatDetection() override;
  bool IsCoreCodecMappedBufferUseful(CodecPort port) override;
  bool IsCoreCodecHwBased() override;

  void CoreCodecInit(const fuchsia::media::FormatDetails& initial_input_format_details) override;
  void CoreCodecSetSecureMemoryMode(
      CodecPort port, fuchsia::mediacodec::SecureMemoryMode secure_memory_mode) override;
  fuchsia::sysmem::BufferCollectionConstraints CoreCodecGetBufferCollectionConstraints(
      CodecPort port, const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
      const fuchsia::media::StreamBufferPartialSettings& partial_settings) override;
  void CoreCodecSetBufferCollectionInfo(
      CodecPort port,
      const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) override;
  void CoreCodecStartStream() override;
  void CoreCodecQueueInputFormatDetails(
      const fuchsia::media::FormatDetails& per_stream_override_format_details) override;
  void CoreCodecQueueInputPacket(CodecPacket* packet) override;
  void CoreCodecQueueInputEndOfStream() override;
  void CoreCodecStopStream() override;
  void CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) override;
  void CoreCodecConfigureBuffers(CodecPort port,
                                 const std::vector<std::unique_ptr<CodecPacket>>& packets) override;
  void CoreCodecRecycleOutputPacket(CodecPacket* packet) override;
  void CoreCodecEnsureBuffersNotConfigured(CodecPort port) override;
  std::unique_ptr<const fuchsia::media::StreamOutputConstraints> CoreCodecBuildNewOutputConstraints(
      uint64_t stream_lifetime_ordinal, uint64_t new_output_buffer_constraints_version_ordinal,
      bool buffer_constraints_action_required) override;
  fuchsia::media::StreamOutputFormat CoreCodecGetOutputFormat(
      uint64_t stream_lifetime_ordinal,
      uint64_t new_output_format_details_version_ordinal) override;
  void CoreCodecMidStreamOutputBufferReConfigPrepare() override;
  void CoreCodecMidStreamOutputBufferReConfigFinish() override;

  void ReadMoreInputData(Vp9Decoder* decoder) override;
  void ReadMoreInputDataFromReschedule(Vp9Decoder* decoder) override;
  void FrameWasOutput() override;
  bool HasMoreInputData() override;

 private:
  friend class CodecAdapterVp9Test;

  void PostSerial(async_dispatcher_t* dispatcher, fit::closure to_run);
  void PostToInputProcessingThread(fit::closure to_run);
  void QueueInputItem(CodecInputItem input_item);
  CodecInputItem DequeueInputItem();
  void ProcessInput();
  bool IsCurrentOutputBufferCollectionUsable(uint32_t frame_count, uint32_t coded_width,
                                             uint32_t coded_height, uint32_t stride,
                                             uint32_t display_width, uint32_t display_height);
  zx_status_t InitializeFramesHandler(::zx::bti bti, uint32_t frame_count, uint32_t width,
                                      uint32_t height, uint32_t stride, uint32_t display_width,
                                      uint32_t display_height, bool has_sar, uint32_t sar_width,
                                      uint32_t sar_height);

  void OnCoreCodecEos();
  void OnCoreCodecFailStream(fuchsia::media::StreamError error);
  CodecPacket* GetFreePacket();
  bool IsPortSecureRequired(CodecPort port);
  bool IsPortSecurePermitted(CodecPort port);
  bool IsPortSecure(CodecPort port);
  bool IsOutputSecure();
  void SubmitDataToStreamBuffer(const std::vector<uint8_t>& data);

  DeviceCtx* device_ = nullptr;
  AmlogicVideo* video_ = nullptr;
  // TODO(fxb/35200): Enable for secure input.
  bool use_parser_ = false;

  fuchsia::media::FormatDetails initial_input_format_details_;

  fuchsia::mediacodec::SecureMemoryMode secure_memory_mode_[kPortCount] = {};
  std::optional<fuchsia::sysmem::SingleBufferSettings> buffer_settings_[kPortCount];

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

  std::optional<fuchsia::sysmem::BufferCollectionInfo_2> output_buffer_collection_info_;

  std::vector<const CodecBuffer*> all_output_buffers_;
  std::vector<CodecPacket*> all_output_packets_;
  std::vector<uint32_t> free_output_packets_;

  // >= output_buffer_collection_info_.buffer_count
  uint32_t packet_count_total_ = 0;
  // These don't actually change, for VP9, since the SAR is at webm layer and
  // the VP9 decoder never actually sees SAR.
  bool has_sar_ = false;
  uint32_t sar_width_ = 0;
  uint32_t sar_height_ = 0;
  // These change on the fly as frames are decoded:
  uint32_t coded_width_ = 0;
  uint32_t coded_height_ = 0;
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

  bool is_stream_failed_ = false;

  // Guarded by decoder lock.
  // This is a list of frame (not superframe) sizes for frames already in the
  // ringbuffer. It can hold at most 9 frames (the maximum for a superframe),
  // but will typically have 2 or less.
  std::vector<uint32_t> queued_frame_sizes_;

  Vp9Decoder* decoder_ = nullptr;

  CodecAdapterVp9() = delete;
  DISALLOW_COPY_ASSIGN_AND_MOVE(CodecAdapterVp9);
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADAPTER_VP9_H_

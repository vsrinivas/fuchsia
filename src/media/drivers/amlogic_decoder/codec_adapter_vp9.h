// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_CODEC_ADAPTER_VP9_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_CODEC_ADAPTER_VP9_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/closure-queue/closure_queue.h>
#include <lib/media/codec_impl/codec_adapter.h>
#include <lib/media/codec_impl/codec_diagnostics.h>
#include <lib/zx/bti.h>

#include <optional>
#include <random>

#include <fbl/macros.h>

#include "amlogic_codec_adapter.h"
#include "video_decoder.h"
#include "vp9_decoder.h"

// From codec_impl
struct VideoFrame;

namespace amlogic_decoder {

// Used for friend declarations below
namespace test {
class CodecAdapterVp9Test;
}  // namespace test

class AmlogicVideo;
class DeviceCtx;

class CodecAdapterVp9 : public AmlogicCodecAdapter, public Vp9Decoder::FrameDataProvider {
 public:
  explicit CodecAdapterVp9(std::mutex& lock, CodecAdapterEvents* codec_adapter_events,
                           DeviceCtx* device);
  ~CodecAdapterVp9();

  void SetCodecDiagnostics(CodecDiagnostics* codec_diagnostics) override;
  std::optional<media_metrics::StreamProcessorEvents2MigratedMetricDimensionImplementation>
  CoreCodecMetricsImplementation() override;
  bool IsCoreCodecRequiringOutputConfigForFormatDetection() override;
  bool IsCoreCodecMappedBufferUseful(CodecPort port) override;
  bool IsCoreCodecHwBased(CodecPort port) override;

  void CoreCodecInit(const fuchsia::media::FormatDetails& initial_input_format_details) override;
  zx::unowned_bti CoreCodecBti() override;
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
  void CoreCodecResetStreamAfterCurrentFrame() override;
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
  std::string CoreCodecGetName() override { return "AmlVp9"; }
  void CoreCodecSetStreamControlProfile(zx::unowned_thread stream_control_thread) override;

  void ReadMoreInputData(Vp9Decoder* decoder) override;
  void ReadMoreInputDataFromReschedule(Vp9Decoder* decoder) override;
  bool HasMoreInputData() override;
  void AsyncResetStreamAfterCurrentFrame() override;

  // |VideoDecoder::Client| implementation.
  void OnError() override;
  void OnEos() override;
  bool IsOutputReady() override;
  void OnFrameReady(std::shared_ptr<VideoFrame> frame) override;
  zx_status_t InitializeFrames(uint32_t min_frame_count, uint32_t max_frame_count, uint32_t width,
                               uint32_t height, uint32_t stride, uint32_t display_width,
                               uint32_t display_height, bool has_sar, uint32_t sar_width,
                               uint32_t sar_height) override;
  bool IsCurrentOutputBufferCollectionUsable(uint32_t min_frame_count, uint32_t max_frame_count,
                                             uint32_t coded_width, uint32_t coded_height,
                                             uint32_t stride, uint32_t display_width,
                                             uint32_t display_height) override;

 private:
  friend class test::CodecAdapterVp9Test;

  void PostSerial(async_dispatcher_t* dispatcher, fit::closure to_run);
  void PostToInputProcessingThread(fit::closure to_run);
  void QueueInputItem(CodecInputItem input_item);
  CodecInputItem DequeueInputItem();
  void ProcessInput();

  void OnCoreCodecEos();
  void OnCoreCodecFailStream(fuchsia::media::StreamError error);
  CodecPacket* GetFreePacket(const CodecBuffer* buffer);
  bool IsPortSecureRequired(CodecPort port);
  bool IsPortSecurePermitted(CodecPort port);
  bool IsPortSecure(CodecPort port);
  bool IsOutputSecure();
  // If paddr_size != 0, paddr_base is valid and is used to submit data directly to the HW by
  // physical address.  Else vaddr_base and vaddr_size are valid and are used to submit data to the
  // HW.
  void SubmitDataToStreamBuffer(zx_paddr_t paddr_base, uint32_t paddr_size, uint8_t* vaddr_base,
                                uint32_t vaddr_size);

  std::list<CodecInputItem> CoreCodecStopStreamInternal();

  void MidStreamOutputBufferConfigInternal(bool did_reallocate_buffers);

  DeviceCtx* device_ = nullptr;
  AmlogicVideo* video_ = nullptr;
  // We always use the parser, because we must when output is protected, and we get more efficient
  // test coverage if we always run that way.
  bool use_parser_ = true;

  fuchsia::media::FormatDetails initial_input_format_details_;

  fuchsia::mediacodec::SecureMemoryMode secure_memory_mode_[kPortCount] = {};
  bool secure_memory_mode_set_[kPortCount] = {};
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

  std::optional<ClosureQueue> shared_fidl_thread_closure_queue_;

  std::optional<DriverCodecDiagnostics> codec_diagnostics_;

  // Skip any further processing in ProcessInput().
  bool is_cancelling_input_processing_ = false;

  std::optional<fuchsia::sysmem::BufferCollectionInfo_2> output_buffer_collection_info_;

  std::vector<const CodecBuffer*> all_output_buffers_;
  std::vector<CodecPacket*> all_output_packets_;
  std::vector<uint32_t> free_output_packets_;

  uint32_t min_buffer_count_[kPortCount] = {};
  uint32_t max_buffer_count_[kPortCount] = {};
  // These change on the fly as frames are decoded:
  uint32_t coded_width_ = 0;
  uint32_t coded_height_ = 0;
  uint32_t stride_ = 0;
  uint32_t display_width_ = 0;
  uint32_t display_height_ = 0;

  uint32_t output_coded_width_ = 0;
  uint32_t output_coded_height_ = 0;
  uint32_t output_stride_ = 0;
  uint32_t output_display_width_ = 0;
  uint32_t output_display_height_ = 0;

  // Output frames get a PTS based on looking up the output frame's input stream
  // offset via the PtsManager.  For that to work we have to feed the input PTSs
  // into the PtsManager by their input stream offset.  This member tracks the
  // cumulative input stream offset. This is implicitly the same count of bytes
  // so far that the amlogic firmware will accumulate and stamp on output
  // frames.  This counts all bytes delivered to the amlogic firmware, including
  // start code bytes.
  //
  // The SW keeps uint64_t on input, but the HW has only 32 bits available.
  uint64_t parsed_video_size_ = 0;
  bool is_input_end_of_stream_queued_to_core_ = false;
  // For now, this is only ever true for non-DRM streams.  For now, for DRM streams, this stays
  // false but we deliver all frames to Vp9Decoder.  In turn, Vp9Decoder will trigger skipping
  // frames before the first keyframe using a much slower skip involving (repeated) use of
  // AsyncResetStreamAfterCurrentFrame().
  bool has_input_keyframe_ = false;

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

}  // namespace amlogic_decoder

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_CODEC_ADAPTER_VP9_H_

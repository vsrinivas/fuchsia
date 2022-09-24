// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_CODEC_ADAPTER_H264_MULTI_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_CODEC_ADAPTER_H264_MULTI_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/closure-queue/closure_queue.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/zx/bti.h>

#include <fbl/macros.h>

#include "amlogic_codec_adapter.h"
#include "h264_multi_decoder.h"
#include "video_decoder.h"

// From codec_impl
struct VideoFrame;

namespace amlogic_decoder {

class AmlogicVideo;
class DeviceCtx;
class CodecAdapterH264Multi : public AmlogicCodecAdapter,
                              public H264MultiDecoder::FrameDataProvider {
 public:
  explicit CodecAdapterH264Multi(std::mutex& lock, CodecAdapterEvents* codec_adapter_events,
                                 DeviceCtx* device);
  ~CodecAdapterH264Multi();

  void SetCodecDiagnostics(CodecDiagnostics* codec_diagnostics) override;
  std::optional<media_metrics::StreamProcessorEvents2MigratedMetricDimensionImplementation>
  CoreCodecMetricsImplementation() override;

  bool IsCoreCodecRequiringOutputConfigForFormatDetection() override;
  bool IsCoreCodecMappedBufferUseful(CodecPort port) override;
  bool IsCoreCodecHwBased(CodecPort port) override;
  zx::unowned_bti CoreCodecBti() override;
  void CoreCodecInit(const fuchsia::media::FormatDetails& initial_input_format_details) override;
  void CoreCodecSetSecureMemoryMode(
      CodecPort port, fuchsia::mediacodec::SecureMemoryMode secure_memory_mode) override;
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
  fuchsia::sysmem::BufferCollectionConstraints CoreCodecGetBufferCollectionConstraints(
      CodecPort port, const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
      const fuchsia::media::StreamBufferPartialSettings& partial_settings) override;
  void CoreCodecSetBufferCollectionInfo(
      CodecPort port,
      const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) override;
  fuchsia::media::StreamOutputFormat CoreCodecGetOutputFormat(
      uint64_t stream_lifetime_ordinal,
      uint64_t new_output_format_details_version_ordinal) override;
  void CoreCodecMidStreamOutputBufferReConfigPrepare() override;
  void CoreCodecMidStreamOutputBufferReConfigFinish() override;
  void CoreCodecResetStreamAfterCurrentFrame() override;
  std::string CoreCodecGetName() override { return "AmlH264Multi"; }
  void CoreCodecSetStreamControlProfile(zx::unowned_thread stream_control_thread) override;

  // VideoDecoder::Client implementation;
  void OnError() override;
  void OnEos() override;
  bool IsOutputReady() override { return true; }
  void OnFrameReady(std::shared_ptr<VideoFrame> frame) override;
  zx_status_t InitializeFrames(uint32_t min_frame_count, uint32_t max_frame_count, uint32_t width,
                               uint32_t height, uint32_t stride, uint32_t display_width,
                               uint32_t display_height, bool has_sar, uint32_t sar_width,
                               uint32_t sar_height) override;
  bool IsCurrentOutputBufferCollectionUsable(uint32_t min_frame_count, uint32_t max_frame_count,
                                             uint32_t coded_width, uint32_t coded_height,
                                             uint32_t stride, uint32_t display_width,
                                             uint32_t display_height) override;

  // H264MultiDecoder::FrameDataProvider implementation.
  std::optional<H264MultiDecoder::DataInput> ReadMoreInputData() override;
  bool HasMoreInputData() override;
  void AsyncPumpDecoder() override;
  void AsyncResetStreamAfterCurrentFrame() override;

 private:
  void PostAndBlockResourceTask(fit::closure task_function);
  void QueueInputItem(CodecInputItem input_item, bool at_front = false);
  CodecInputItem DequeueInputItem();
  std::vector<uint8_t> ParseCodecOobBytes();
  // If parsing something whose format depends on is_avcc_, use this method.
  //
  // The buffer pointer can be nullptr unless the VMO is a secure VMO.
  std::optional<H264MultiDecoder::DataInput> ParseVideo(const CodecBuffer* buffer,
                                                        fit::deferred_callback* return_input_packet,
                                                        const uint8_t* data, uint32_t length);
  // If parsing something that's known to be in AVCC format, such as a bunch of
  // 0x00 without start codes or emulation prevention bytes, use this method.
  //
  // This does not support secure buffers, as this requires a CPU re-pack which at least for now is
  // only implemented in the REE (rich execution environment), so the re-pack can't happen if the
  // buffer can't be read by the CPU from the REE.
  std::optional<H264MultiDecoder::DataInput> ParseVideoAvcc(const uint8_t* data, uint32_t length);
  // If parsing something that's known to be in AnnexB format, such as the
  // end-of-stream marker data, use this method.
  //
  // The buffer pointer can be nullptr unless the VMO is a secure VMO.
  std::optional<H264MultiDecoder::DataInput> ParseVideoAnnexB(
      const CodecBuffer* buffer, fit::deferred_callback* return_input_packet, const uint8_t* data,
      uint32_t length);

  void OnCoreCodecFailStream(fuchsia::media::StreamError error);
  // The passed-in buffer will be set on the returned packet until the packet is no longer in
  // flight.
  CodecPacket* GetFreePacket(const CodecBuffer* buffer);
  std::list<CodecInputItem> CoreCodecStopStreamInternal();

  bool IsPortSecureRequired(CodecPort port);
  bool IsPortSecurePermitted(CodecPort port);
  bool IsPortSecure(CodecPort port);
  bool IsOutputSecure();

  void MidStreamOutputBufferConfigInternal(bool did_reallocate_buffers);

  DeviceCtx* device_ = nullptr;
  AmlogicVideo* video_ = nullptr;
  // Should only be accessed under the video decoder lock.
  H264MultiDecoder* decoder_ = nullptr;

  // CodecImpl requires some calls (mainly related to returning input data) to not be on the
  // StreamControl or FIDL threads, so this thread should be used for calls into the
  // H264MultiDecoder that may trigger PumpDecoder.
  async::Loop core_loop_;

  // Use to initialize/destroy resources. Since the computational complexity of initialization or
  // destruction will fall out of line of the stream_control deadline parameters this allow us to
  // temporarily ignore those parameters in order to get of the critical path as fast a possible.
  // Even though it is an async::Loop tasks should be posted and then the stream_control_thread_
  // should then block on the task's completion. The reason for this is that stream control
  // operation are assumed to be synchronous.
  async::Loop resource_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};

  std::optional<ClosureQueue> shared_fidl_thread_closure_queue_;

  bool have_queued_trigger_decoder_ = false;

  fuchsia::mediacodec::SecureMemoryMode secure_memory_mode_[kPortCount] = {};

  fuchsia::media::FormatDetails initial_input_format_details_;
  fuchsia::media::FormatDetails latest_input_format_details_;

  std::optional<fuchsia::sysmem::BufferCollectionInfo_2> output_buffer_collection_info_;
  std::optional<fuchsia::sysmem::SingleBufferSettings> buffer_settings_[kPortCount];

  std::vector<const CodecBuffer*> all_output_buffers_;
  std::vector<CodecPacket*> all_output_packets_;
  std::vector<uint32_t> free_output_packets_;

  std::optional<DriverCodecDiagnostics> codec_diagnostics_;

  uint32_t min_buffer_count_[kPortCount] = {};
  uint32_t max_buffer_count_[kPortCount] = {};
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint32_t min_stride_ = 0;
  uint32_t output_stride_ = 0;
  uint32_t display_width_ = 0;
  uint32_t display_height_ = 0;
  bool has_sar_ = false;
  uint32_t sar_width_ = 0;
  uint32_t sar_height_ = 0;

  // If true, the core codec will need the oob_bytes info, if any.  The
  // core codec in this case wants the info in annex B form in-band, not
  // AVCC/avcC form out-of-band.
  bool is_input_format_details_pending_ = false;

  // For any new stream, remains false until proven otherwise.  If this is true
  // we have to add start code emulation prevention bytes, and replace AVCC
  // nal_length fields (themselves usually 4 bytes long but not always) with
  // start codes (out-of-place conversion).
  bool is_avcc_ = false;
  // This is the length in bytes of the pseudo_nal_length field, which in turn
  // has the length of a pseudo_nal in bytes.  Feel free to suggest a better
  // name for this field, but I want to strongly emphasize that it's the length
  // of a length field, not itself directly the length...
  //
  // Typically 4 if is_avcc_, but not always.
  uint32_t pseudo_nal_length_field_bytes_ = 0;

  bool is_input_end_of_stream_queued_ = false;

  bool is_stream_failed_ = false;

  bool is_input_end_of_stream_queued_to_core_ = false;

  CodecAdapterH264Multi() = delete;
  DISALLOW_COPY_ASSIGN_AND_MOVE(CodecAdapterH264Multi);
};

}  // namespace amlogic_decoder

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_CODEC_ADAPTER_H264_MULTI_H_

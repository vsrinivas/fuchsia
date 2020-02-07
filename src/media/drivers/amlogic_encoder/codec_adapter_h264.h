// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_ENCODER_CODEC_ADAPTER_H264_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_ENCODER_CODEC_ADAPTER_H264_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/media/codec_impl/codec_adapter.h>
#include <lib/zx/bti.h>

#include <fbl/macros.h>

struct CodecFrame;
class DeviceCtx;
struct VideoFrame;

class CodecAdapterH264 : public CodecAdapter {
 public:
  explicit CodecAdapterH264(std::mutex& lock, CodecAdapterEvents* codec_adapter_events,
                            DeviceCtx* device);
  ~CodecAdapterH264() override;

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

 private:
  void PostSerial(async_dispatcher_t* dispatcher, fit::closure to_run);
  void PostToInputProcessingThread(fit::closure to_run);
  void QueueInputItem(CodecInputItem input_item);
  CodecInputItem DequeueInputItem();
  void ProcessInput();
  bool ParseAndDeliverCodecOobBytes();

  void OnCoreCodecFailStream(fuchsia::media::StreamError error);
  CodecPacket* GetFreePacket();

  DeviceCtx* device_ = nullptr;

  fuchsia::media::FormatDetails initial_input_format_details_;
  fuchsia::media::FormatDetails latest_input_format_details_;

  std::optional<fuchsia::sysmem::SingleBufferSettings> buffer_settings_[kPortCount];

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
  std::vector<uint32_t> free_output_packets_;

  uint32_t min_buffer_count_[kPortCount] = {};
  uint32_t max_buffer_count_[kPortCount] = {};
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint32_t min_stride_ = 0;
  uint32_t display_width_ = 0;
  uint32_t display_height_ = 0;

  bool is_input_end_of_stream_queued_ = false;

  bool is_stream_failed_ = false;

  CodecAdapterH264() = delete;
  CodecAdapterH264(const CodecAdapterH264&) = delete;
  CodecAdapterH264(CodecAdapterH264&&) = delete;
  CodecAdapterH264& operator=(const CodecAdapterH264&) = delete;
  CodecAdapterH264& operator=(CodecAdapterH264&&) = delete;
};

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_ENCODER_CODEC_ADAPTER_H264_H_

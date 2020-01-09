// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADAPTER_H264_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADAPTER_H264_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/media/codec_impl/codec_adapter.h>
#include <lib/zx/bti.h>

#include <fbl/macros.h>

#include "video_decoder.h"

class AmlogicVideo;
struct CodecFrame;
class DeviceCtx;
struct VideoFrame;
class CodecAdapterH264 : public CodecAdapter, public VideoDecoder::Client {
 public:
  explicit CodecAdapterH264(std::mutex& lock, CodecAdapterEvents* codec_adapter_events,
                            DeviceCtx* device);
  ~CodecAdapterH264();

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

  // VideoDecoder::Client implementation;
  void OnError() override;
  void OnEos() override {}
  bool IsOutputReady() override { return true; }
  void OnFrameReady(std::shared_ptr<VideoFrame> frame) override;
  zx_status_t InitializeFrames(zx::bti, uint32_t min_frame_count, uint32_t max_frame_count,
                               uint32_t width, uint32_t height, uint32_t stride,
                               uint32_t display_width, uint32_t display_height, bool has_sar,
                               uint32_t sar_width, uint32_t sar_height) override;
  bool IsCurrentOutputBufferCollectionUsable(uint32_t min_frame_count, uint32_t max_frame_count,
                                             uint32_t coded_width, uint32_t coded_height,
                                             uint32_t stride, uint32_t display_width,
                                             uint32_t display_height) override {
    return true;
  }

 private:
  void PostSerial(async_dispatcher_t* dispatcher, fit::closure to_run);
  void PostToInputProcessingThread(fit::closure to_run);
  void QueueInputItem(CodecInputItem input_item);
  CodecInputItem DequeueInputItem();
  void ProcessInput();
  bool ParseAndDeliverCodecOobBytes();
  // If parsing something whose format depends on is_avcc_, use this method.
  //
  // The buffer pointer can be nullptr unless the VMO is a secure VMO.
  bool ParseVideo(const CodecBuffer* buffer, const uint8_t* data, uint32_t length);
  // If parsing something that's known to be in AVCC format, such as a bunch of
  // 0x00 without start codes or emulation prevention bytes, use this method.
  //
  // This does not support secure buffers, as this requires a CPU re-pack which at least for now is
  // only implemented in the REE (rich execution environment), so the re-pack can't happen if the
  // buffer can't be read by the CPU from the REE.
  bool ParseVideoAvcc(const uint8_t* data, uint32_t length);
  // If parsing something that's known to be in AnnexB format, such as the
  // end-of-stream marker data, use this method.
  //
  // The buffer pointer can be nullptr unless the VMO is a secure VMO.
  bool ParseVideoAnnexB(const CodecBuffer* buffer, const uint8_t* data, uint32_t length);

  void OnCoreCodecFailStream(fuchsia::media::StreamError error);
  CodecPacket* GetFreePacket();

  bool IsPortSecureRequired(CodecPort port);
  bool IsPortSecurePermitted(CodecPort port);
  bool IsPortSecure(CodecPort port);
  bool IsOutputSecure();

  DeviceCtx* device_ = nullptr;
  AmlogicVideo* video_ = nullptr;

  fuchsia::mediacodec::SecureMemoryMode secure_memory_mode_[kPortCount] = {};

  fuchsia::media::FormatDetails initial_input_format_details_;
  fuchsia::media::FormatDetails latest_input_format_details_;

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
  bool has_sar_ = false;
  uint32_t sar_width_ = 0;
  uint32_t sar_height_ = 0;

  // Output frames get a PTS based on looking up the output frame's input stream
  // offset via the PtsManager.  For that to work we have to feed the input PTSs
  // into the PtsManager by their input stream offset.  This member tracks the
  // cumulative input stream offset. This is implicitly the same count of bytes
  // so far that the amlogic firmware will accumulate and stamp on output
  // frames.  This counts all bytes delivered to the amlogic firmware, including
  // start code bytes.
  uint64_t parsed_video_size_ = 0;
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

  CodecAdapterH264() = delete;
  DISALLOW_COPY_ASSIGN_AND_MOVE(CodecAdapterH264);
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADAPTER_H264_H_

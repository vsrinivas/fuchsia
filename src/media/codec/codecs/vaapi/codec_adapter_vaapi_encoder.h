// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_CODECS_VAAPI_CODEC_ADAPTER_VAAPI_ENCODER_H_
#define SRC_MEDIA_CODEC_CODECS_VAAPI_CODEC_ADAPTER_VAAPI_ENCODER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/media/codec_impl/codec_adapter.h>
#include <lib/media/codec_impl/codec_buffer.h>
#include <lib/media/codec_impl/codec_input_item.h>
#include <lib/media/codec_impl/codec_packet.h>
#include <lib/trace/event.h>
#include <threads.h>

#include <optional>
#include <queue>

#include <fbl/algorithm.h>
#include <va/va.h>

#include "buffer_pool.h"
#include "media/gpu/accelerated_video_decoder.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/lib/mpsc_queue/mpsc_queue.h"
#include "src/media/third_party/chromium_media/media/gpu/gpu_video_encode_accelerator_helpers.h"
#include "vaapi_utils.h"

class CodecAdapterVaApiEncoder;

namespace media {
class VaapiVideoEncoderDelegate;
class VaapiWrapper;
}  // namespace media

class VaApiEncoderOutput {
 public:
  VaApiEncoderOutput() = default;
  VaApiEncoderOutput(uint8_t* base_address, CodecAdapterVaApiEncoder* adapter)
      : base_address_(base_address), adapter_(adapter) {}
  VaApiEncoderOutput(const VaApiEncoderOutput&) = delete;
  VaApiEncoderOutput(VaApiEncoderOutput&& other) noexcept;

  ~VaApiEncoderOutput();

  VaApiEncoderOutput& operator=(VaApiEncoderOutput&& other) noexcept;
  VaApiEncoderOutput& operator=(VaApiEncoderOutput& other) noexcept = delete;

 private:
  uint8_t* base_address_ = nullptr;
  CodecAdapterVaApiEncoder* adapter_ = nullptr;
};

class CodecAdapterVaApiEncoder : public CodecAdapter {
 public:
  CodecAdapterVaApiEncoder(std::mutex& lock, CodecAdapterEvents* codec_adapter_events);

  ~CodecAdapterVaApiEncoder() override;

  bool IsCoreCodecRequiringOutputConfigForFormatDetection() override { return false; }

  bool IsCoreCodecMappedBufferUseful(CodecPort port) override { return true; }

  bool IsCoreCodecHwBased(CodecPort port) override { return false; }

  void CoreCodecInit(const fuchsia::media::FormatDetails& initial_input_format_details) override;

  void CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) override {
    if (port != kOutputPort) {
      return;
    }

    staged_output_buffers_.push_back(buffer);
  }

  void CoreCodecConfigureBuffers(
      CodecPort port, const std::vector<std::unique_ptr<CodecPacket>>& packets) override {
    if (port != kOutputPort) {
      return;
    }
    std::vector<CodecPacket*> all_packets;
    for (auto& packet : packets) {
      all_packets.push_back(packet.get());
    }
    std::shuffle(all_packets.begin(), all_packets.end(), not_for_security_prng_);
    for (CodecPacket* packet : all_packets) {
      free_output_packets_.Push(packet);
    }
  }

  void CoreCodecStartStream() override {
    // It's ok for RecycleInputPacket to make a packet free anywhere in this
    // sequence. Nothing else ought to be happening during CoreCodecStartStream
    // (in this or any other thread).
    input_queue_.Reset();
    free_output_packets_.Reset(/*keep_data=*/true);
    output_buffer_pool_.Reset(/*keep_data=*/true);
    LoadStagedOutputBuffers();

    zx_status_t post_result =
        async::PostTask(input_processing_loop_.dispatcher(), [this] { ProcessInputLoop(); });
    ZX_ASSERT_MSG(post_result == ZX_OK,
                  "async::PostTask() failed to post input processing loop - result: %d\n",
                  post_result);

    TRACE_INSTANT("codec_runner", "Media:Start", TRACE_SCOPE_THREAD);
  }

  void CoreCodecQueueInputFormatDetails(
      const fuchsia::media::FormatDetails& per_stream_override_format_details) override {
    input_queue_.Push(CodecInputItem::FormatDetails(per_stream_override_format_details));
  }

  void CoreCodecQueueInputPacket(CodecPacket* packet) override {
    TRACE_INSTANT("codec_runner", "Media:PacketReceived", TRACE_SCOPE_THREAD);
    input_queue_.Push(CodecInputItem::Packet(packet));
  }

  void CoreCodecQueueInputEndOfStream() override {
    input_queue_.Push(CodecInputItem::EndOfStream());
  }

  void CoreCodecStopStream() override {
    input_queue_.StopAllWaits();
    free_output_packets_.StopAllWaits();
    output_buffer_pool_.StopAllWaits();

    WaitForInputProcessingLoopToEnd();
    CleanUpAfterStream();

    auto queued_input_items = BlockingMpscQueue<CodecInputItem>::Extract(std::move(input_queue_));
    while (!queued_input_items.empty()) {
      CodecInputItem input_item = std::move(queued_input_items.front());
      queued_input_items.pop();
      if (input_item.is_packet()) {
        events_->onCoreCodecInputPacketDone(input_item.packet());
      }
    }

    TRACE_INSTANT("codec_runner", "Media:Stop", TRACE_SCOPE_THREAD);
  }

  void CoreCodecRecycleOutputPacket(CodecPacket* packet) override {
    if (packet->is_new()) {
      // CoreCodecConfigureBuffers() took care of initially populating
      // free_output_packets_ (in shuffled order), so ignore new packets.
      ZX_DEBUG_ASSERT(!packet->buffer());
      packet->SetIsNew(false);
      return;
    }
    if (packet->buffer()) {
      VaApiEncoderOutput local_output;
      {
        std::lock_guard<std::mutex> lock(lock_);
        ZX_DEBUG_ASSERT(in_use_by_client_.find(packet) != in_use_by_client_.end());
        local_output = std::move(in_use_by_client_[packet]);
        in_use_by_client_.erase(packet);
      }

      // ~ local_output, which may trigger a buffer free callback.
    }
    free_output_packets_.Push(std::move(packet));
  }

  void CoreCodecEnsureBuffersNotConfigured(CodecPort port) override {
    buffer_settings_[port] = std::nullopt;
    if (port != kOutputPort) {
      // We don't do anything with input buffers.
      return;
    }

    {  // scope to_drop
      std::map<CodecPacket*, VaApiEncoderOutput> to_drop;
      {
        std::lock_guard<std::mutex> lock(lock_);
        std::swap(to_drop, in_use_by_client_);
      }
      // ~to_drop
    }

    // The ~to_drop returns all buffers to the output_buffer_pool_.
    ZX_DEBUG_ASSERT(!output_buffer_pool_.has_buffers_in_use());

    // VMO handles for the old output buffers may still exist, but the
    // decoder doesn't know about those, and buffer_lifetime_ordinal will
    // prevent us calling output_buffer_pool_.FreeBuffer() for any of the old
    // buffers.  So forget about the old buffers here.
    output_buffer_pool_.Reset();
    staged_output_buffers_.clear();

    free_output_packets_.Reset();
  }

  void CoreCodecMidStreamOutputBufferReConfigPrepare() override {
    // Nothing to do here.
  }

  void CoreCodecMidStreamOutputBufferReConfigFinish() override { LoadStagedOutputBuffers(); }

  std::unique_ptr<const fuchsia::media::StreamOutputConstraints> CoreCodecBuildNewOutputConstraints(
      uint64_t stream_lifetime_ordinal, uint64_t new_output_buffer_constraints_version_ordinal,
      bool buffer_constraints_action_required) override {
    auto config = std::make_unique<fuchsia::media::StreamOutputConstraints>();

    config->set_stream_lifetime_ordinal(stream_lifetime_ordinal);

    // For the moment, there will be only one StreamOutputConstraints, and it'll
    // need output buffers configured for it.
    ZX_DEBUG_ASSERT(buffer_constraints_action_required);
    config->set_buffer_constraints_action_required(buffer_constraints_action_required);
    auto* constraints = config->mutable_buffer_constraints();
    constraints->set_buffer_constraints_version_ordinal(
        new_output_buffer_constraints_version_ordinal);

    return config;
  }

  fuchsia::media::StreamOutputFormat CoreCodecGetOutputFormat(
      uint64_t stream_lifetime_ordinal,
      uint64_t new_output_format_details_version_ordinal) override {
    fuchsia::media::StreamOutputFormat result;
    result.set_stream_lifetime_ordinal(stream_lifetime_ordinal);
    result.mutable_format_details()->set_format_details_version_ordinal(
        new_output_format_details_version_ordinal);

    result.mutable_format_details()->set_mime_type("video/h264");

    fuchsia::media::VideoFormat video_format;

    auto compressed_format = fuchsia::media::VideoCompressedFormat();
    compressed_format.set_temp_field_todo_remove(0);
    video_format.set_compressed(std::move(compressed_format));

    result.mutable_format_details()->mutable_domain()->set_video(std::move(video_format));
    return result;
  }

  fuchsia::sysmem::BufferCollectionConstraints CoreCodecGetBufferCollectionConstraints(
      CodecPort port, const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
      const fuchsia::media::StreamBufferPartialSettings& partial_settings) override {
    if (port == kOutputPort) {
      fuchsia::sysmem::BufferCollectionConstraints constraints;
      constraints.min_buffer_count_for_camping = 1;
      constraints.has_buffer_memory_constraints = true;
      // The Intel GPU supports CPU domain buffer collections, so we don't really need to support
      // RAM domain.
      constraints.buffer_memory_constraints.cpu_domain_supported = true;
      ZX_ASSERT(display_size_.width() > 0);
      ZX_ASSERT(display_size_.height() > 0);
      // The encoder doesn't support splitting output across buffers.
      constraints.buffer_memory_constraints.min_size_bytes =
          static_cast<uint32_t>(media::GetEncodeBitstreamBufferSize(coded_size_));
      return constraints;
    } else if (port == kInputPort) {
      fuchsia::sysmem::BufferCollectionConstraints constraints;
      constraints.min_buffer_count_for_camping = 1;
      constraints.has_buffer_memory_constraints = true;
      constraints.buffer_memory_constraints.cpu_domain_supported = true;
      constraints.image_format_constraints_count = 1;
      fuchsia::sysmem::ImageFormatConstraints& image_constraints =
          constraints.image_format_constraints[0];
      image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
      // TODO(fxbug.dev/100642): Add support for more colorspaces.
      image_constraints.color_spaces_count = 1;
      image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::REC709;

      // The non-"required_" fields indicate the encoder's ability to accept
      // input frames at various dimensions. The input frames need to be within
      // these bounds.
      image_constraints.min_coded_width = 16;
      image_constraints.max_coded_width = 3840;
      image_constraints.min_coded_height = 16;
      // This intentionally isn't the height of a 4k frame.  See
      // max_coded_width_times_coded_height.  We intentionally constrain the max
      // dimension in width or height to the width of a 4k frame.  While the HW
      // might be able to go bigger than that as long as the other dimension is
      // smaller to compensate, we don't really need to enable any larger than
      // 4k's width in either dimension, so we don't.
      image_constraints.max_coded_height = 3840;
      image_constraints.min_bytes_per_row = 16;
      // no hard-coded max stride, at least for now
      image_constraints.max_bytes_per_row = 0xFFFFFFFF;
      image_constraints.max_coded_width_times_coded_height = 3840 * 2160;
      image_constraints.layers = 1;
      image_constraints.coded_width_divisor = 2;
      image_constraints.coded_height_divisor = 2;
      image_constraints.bytes_per_row_divisor = 2;
      image_constraints.start_offset_divisor = 1;
      // Odd display dimensions are permitted, but these don't imply odd YV12
      // dimensions - those are constrainted by coded_width_divisor and
      // coded_height_divisor which are both 2.
      image_constraints.display_width_divisor = 1;
      image_constraints.display_height_divisor = 1;

      // The required sizes aren't initialized, since
      // CoreCodecGetBufferCollectionConstraints won't be re-triggered when the
      // input format is changed.
      return constraints;
    }
    return fuchsia::sysmem::BufferCollectionConstraints{};
  }

  void CoreCodecSetBufferCollectionInfo(
      CodecPort port,
      const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) override {
    buffer_settings_[port] = buffer_collection_info.settings;
  }

  VAContextID context_id() { return context_id_->id(); }

  scoped_refptr<VASurface> GetVASurface();

 private:
  friend class VaApiEncoderOutput;
  void WaitForInputProcessingLoopToEnd() {
    ZX_DEBUG_ASSERT(thrd_current() != input_processing_thread_);

    std::condition_variable stream_stopped_condition;
    bool stream_stopped = false;
    zx_status_t post_result = async::PostTask(input_processing_loop_.dispatcher(),
                                              [this, &stream_stopped, &stream_stopped_condition] {
                                                {
                                                  std::lock_guard<std::mutex> lock(lock_);
                                                  stream_stopped = true;
                                                  // Under lock since
                                                  // WaitForInputProcessingLoopToEnd()
                                                  // may otherwise return too soon deleting
                                                  // stream_stopped_condition too soon.
                                                  stream_stopped_condition.notify_all();
                                                }
                                              });
    ZX_ASSERT_MSG(post_result == ZX_OK,
                  "async::PostTask() failed to post input processing loop - result: %d\n",
                  post_result);

    std::unique_lock<std::mutex> lock(lock_);
    stream_stopped_condition.wait(lock, [&stream_stopped] { return stream_stopped; });
  }
  bool HandleInputFormatChange(const fuchsia::media::FormatDetails& input_format_details,
                               bool initial);

  // We don't give the codec any buffers in its output pool until
  // configuration is finished or a stream starts. Until finishing
  // configuration we stage all the buffers. Here we load all the staged
  // buffers so the codec can make output.
  void LoadStagedOutputBuffers() {
    std::vector<const CodecBuffer*> to_add = std::move(staged_output_buffers_);
    for (auto buffer : to_add) {
      output_buffer_pool_.AddBuffer(buffer);
    }
  }

  // Processes input in a loop. Should only execute on input_processing_thread_.
  // Loops for the lifetime of a stream.
  void ProcessInputLoop();

  bool ProcessPacket(CodecPacket* packet);
  // Releases any resources from the just-ended stream.
  void CleanUpAfterStream();

  BlockingMpscQueue<CodecInputItem> input_queue_{};
  BlockingMpscQueue<CodecPacket*> free_output_packets_{};

  VAProfile va_profile_ = VAProfileH264High;
  // VAEntrypointEncSlice should also work, but LP is supported on Intel and more efficient.
  VAEntrypoint va_entrypoint_ = VAEntrypointEncSliceLP;
  std::optional<ScopedConfigID> config_;

  // The order of output_buffer_pool_ and in_use_by_client_ matters, so that
  // destruction of in_use_by_client_ happens first, because those destructing
  // will return buffers to output_buffer_pool_.
  BufferPool output_buffer_pool_;
  std::map<CodecPacket*, VaApiEncoderOutput> in_use_by_client_ FXL_GUARDED_BY(lock_);

  // Buffers the client has added but that we cannot use until configuration is
  // complete.
  std::vector<const CodecBuffer*> staged_output_buffers_;

  uint64_t input_format_details_version_ordinal_;
  media::VideoEncodeAccelerator::Config accelerator_config_;

  std::optional<fuchsia::sysmem::SingleBufferSettings> buffer_settings_[kPortCount];

  // DPB surfaces.
  std::mutex surfaces_lock_;
  // Incremented whenever new surfaces are allocated and old surfaces should be released.
  uint64_t surface_generation_ FXL_GUARDED_BY(surfaces_lock_) = {};
  gfx::Size surface_size_ FXL_GUARDED_BY(surfaces_lock_);
  // These surfaces are used to hold reference frames.
  std::vector<ScopedSurfaceID> surfaces_ FXL_GUARDED_BY(surfaces_lock_);

  // The input frame is uploaded into this surface, which is used only while encoding.
  std::optional<ScopedSurfaceID> input_surface_;

  std::optional<ScopedContextID> context_id_;

  std::shared_ptr<media::VaapiWrapper> vaapi_wrapper_;
  std::unique_ptr<media::VaapiVideoEncoderDelegate> encoder_;

  gfx::Size display_size_;
  gfx::Size coded_size_;
  bool next_frame_keyframe_ = false;

  async::Loop input_processing_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  thrd_t input_processing_thread_;
};

#endif  // SRC_MEDIA_CODEC_CODECS_VAAPI_CODEC_ADAPTER_VAAPI_ENCODER_H_

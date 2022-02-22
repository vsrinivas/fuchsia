// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_CODECS_VAAPI_CODEC_ADAPTER_VAAPI_DECODER_H_
#define SRC_MEDIA_CODEC_CODECS_VAAPI_CODEC_ADAPTER_VAAPI_DECODER_H_

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
#include "vaapi_utils.h"

class CodecAdapterVaApiDecoder;

// VA-API outputs are distinct from the DPB and are stored in a regular
// BufferPool, since the hardware doesn't necessarily support decoding to a
// linear format like downstream consumers might need.
class VaApiOutput {
 public:
  VaApiOutput() = default;
  VaApiOutput(uint8_t* base_address, CodecAdapterVaApiDecoder* adapter)
      : base_address_(base_address), adapter_(adapter) {}
  VaApiOutput(const VaApiOutput&) = delete;
  VaApiOutput(VaApiOutput&& other) noexcept;

  ~VaApiOutput();

  VaApiOutput& operator=(VaApiOutput&& other) noexcept;

 private:
  uint8_t* base_address_ = nullptr;
  CodecAdapterVaApiDecoder* adapter_ = nullptr;
};

static inline constexpr uint32_t make_fourcc(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return (static_cast<uint32_t>(d) << 24) | (static_cast<uint32_t>(c) << 16) |
         (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(a);
}
class CodecAdapterVaApiDecoder : public CodecAdapter {
 public:
  CodecAdapterVaApiDecoder(std::mutex& lock, CodecAdapterEvents* codec_adapter_events)
      : CodecAdapter(lock, codec_adapter_events),
        input_queue_(),
        free_output_packets_(),
        input_processing_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    ZX_DEBUG_ASSERT(events_);
  }

  ~CodecAdapterVaApiDecoder() { input_processing_loop_.Shutdown(); }

  bool IsCoreCodecRequiringOutputConfigForFormatDetection() override { return false; }

  bool IsCoreCodecMappedBufferUseful(CodecPort port) override { return true; }

  bool IsCoreCodecHwBased(CodecPort port) override { return true; }

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
    // TODO(turnage): Accept midstream and interstream input format changes.
    // For now these should always be 0, so assert to notice if anything
    // changes.
    ZX_ASSERT(per_stream_override_format_details.has_format_details_version_ordinal() &&
              per_stream_override_format_details.format_details_version_ordinal() ==
                  input_format_details_version_ordinal_);
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
      VaApiOutput local_output;
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
      std::map<CodecPacket*, VaApiOutput> to_drop;
      {
        std::lock_guard<std::mutex> lock(lock_);
        std::swap(to_drop, in_use_by_client_);
      }
      // ~to_drop
    }

    // The ~to_drop returns all buffers to the output_buffer_pool_.
    ZX_DEBUG_ASSERT(!output_buffer_pool_.has_buffers_in_use());

    // VMO handles for the old output buffers may still exist, but the SW
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
    std::lock_guard<std::mutex> lock(lock_);
    fuchsia::media::StreamOutputFormat result;
    fuchsia::sysmem::ImageFormat_2 image_format;
    // This is temporary until the media decoder is added.
    gfx::Size pic_size(0, 0);
    gfx::Rect visible_rect(0, 0);
    image_format.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
    image_format.coded_width = pic_size.width();
    image_format.coded_height = pic_size.height();
    image_format.bytes_per_row = GetOutputStride();
    image_format.display_width = visible_rect.width();
    image_format.display_height = visible_rect.height();
    image_format.layers = 1;
    image_format.color_space.type = fuchsia::sysmem::ColorSpaceType::REC709;
    image_format.has_pixel_aspect_ratio = false;

    fuchsia::media::FormatDetails format_details;

    format_details.set_mime_type("video/raw");

    fuchsia::media::VideoFormat video_format;
    video_format.set_uncompressed(GetUncompressedFormat(image_format));

    format_details.mutable_domain()->set_video(std::move(video_format));

    result.set_stream_lifetime_ordinal(stream_lifetime_ordinal);
    result.set_format_details(std::move(format_details));
    result.mutable_format_details()->set_format_details_version_ordinal(
        new_output_format_details_version_ordinal);
    return result;
  }

  fuchsia::sysmem::BufferCollectionConstraints CoreCodecGetBufferCollectionConstraints(
      CodecPort port, const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
      const fuchsia::media::StreamBufferPartialSettings& partial_settings) override {
    if (port == kInputPort) {
      fuchsia::sysmem::BufferCollectionConstraints constraints;
      constraints.min_buffer_count_for_camping = 1;
      constraints.has_buffer_memory_constraints = true;
      constraints.buffer_memory_constraints.cpu_domain_supported = true;
      // Must be big enough to hold an entire NAL unit, since the H264Decoder doesn't support split
      // NAL units.
      constraints.buffer_memory_constraints.min_size_bytes = 8192 * 512;
      return constraints;
    } else if (port == kOutputPort) {
      fuchsia::sysmem::BufferCollectionConstraints constraints;
      constraints.min_buffer_count_for_camping = 1;
      constraints.has_buffer_memory_constraints = true;
      // TODO(fxbug.dev/94140): Add RAM domain support.
      constraints.buffer_memory_constraints.cpu_domain_supported = true;
      constraints.image_format_constraints_count = 1;
      fuchsia::sysmem::ImageFormatConstraints& image_constraints =
          constraints.image_format_constraints[0];
      image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
      // TODO(fix)
      image_constraints.color_spaces_count = 1;
      image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::REC709;

      // The non-"required_" fields indicate the decoder's ability to potentially
      // output frames at various dimensions as coded in the stream.  Aside from
      // the current stream being somewhere in these bounds, these have nothing to
      // do with the current stream in particular.
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
      image_constraints.coded_width_divisor = 16;
      image_constraints.coded_height_divisor = 16;
      image_constraints.bytes_per_row_divisor = 16;
      image_constraints.start_offset_divisor = 1;
      // Odd display dimensions are permitted, but these don't imply odd YV12
      // dimensions - those are constrainted by coded_width_divisor and
      // coded_height_divisor which are both 16.
      image_constraints.display_width_divisor = 1;
      image_constraints.display_height_divisor = 1;

      // The decoder is producing frames and the decoder has no choice but to
      // produce frames at their coded size.  The decoder wants to potentially be
      // able to support a stream with dynamic resolution, potentially including
      // dimensions both less than and greater than the dimensions that led to the
      // current need to allocate a BufferCollection.  For this reason, the
      // required_ fields are set to the exact current dimensions, and the
      // permitted (non-required_) fields is set to the full potential range that
      // the decoder could potentially output.  If an initiator wants to require a
      // larger range of dimensions that includes the required range indicated
      // here (via a-priori knowledge of the potential stream dimensions), an
      // initiator is free to do so.

      // This is temporary until the media decoder is added.
      gfx::Size pic_size(0, 0);
      image_constraints.required_min_coded_width = pic_size.width();
      image_constraints.required_max_coded_width = pic_size.width();
      image_constraints.required_min_coded_height = pic_size.height();
      image_constraints.required_max_coded_height = pic_size.height();
      return constraints;
    }
    return fuchsia::sysmem::BufferCollectionConstraints{};
  }

  void CoreCodecSetBufferCollectionInfo(
      CodecPort port,
      const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) override {
    buffer_settings_[port] = buffer_collection_info.settings;
  }

 protected:
  friend class VaApiOutput;
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

  // Releases any resources from the just-ended stream.
  void CleanUpAfterStream();

  uint32_t GetOutputStride() { return 0; }

  static fuchsia::media::VideoUncompressedFormat GetUncompressedFormat(
      const fuchsia::sysmem::ImageFormat_2& image_format) {
    ZX_DEBUG_ASSERT(image_format.pixel_format.type == fuchsia::sysmem::PixelFormatType::NV12);
    fuchsia::media::VideoUncompressedFormat video_uncompressed;
    video_uncompressed.image_format = image_format;
    video_uncompressed.fourcc = make_fourcc('N', 'V', '1', '2');
    video_uncompressed.primary_width_pixels = image_format.coded_width;
    video_uncompressed.primary_height_pixels = image_format.coded_height;
    video_uncompressed.secondary_width_pixels = image_format.coded_width / 2;
    video_uncompressed.secondary_height_pixels = image_format.coded_height / 2;
    video_uncompressed.primary_display_width_pixels = image_format.display_width;
    video_uncompressed.primary_display_height_pixels = image_format.display_height;

    video_uncompressed.planar = true;
    ZX_DEBUG_ASSERT(!image_format.pixel_format.has_format_modifier);
    video_uncompressed.swizzled = false;
    video_uncompressed.primary_line_stride_bytes = image_format.bytes_per_row;
    video_uncompressed.secondary_line_stride_bytes = image_format.bytes_per_row;
    video_uncompressed.primary_start_offset = 0;
    video_uncompressed.secondary_start_offset =
        image_format.bytes_per_row * image_format.coded_height;
    video_uncompressed.tertiary_start_offset = video_uncompressed.secondary_start_offset + 1;
    video_uncompressed.primary_pixel_stride = 1;
    video_uncompressed.secondary_pixel_stride = 2;
    video_uncompressed.has_pixel_aspect_ratio = image_format.has_pixel_aspect_ratio;
    video_uncompressed.pixel_aspect_ratio_height = image_format.pixel_aspect_ratio_height;
    video_uncompressed.pixel_aspect_ratio_width = image_format.pixel_aspect_ratio_width;
    return video_uncompressed;
  }

  BlockingMpscQueue<CodecInputItem> input_queue_;
  BlockingMpscQueue<CodecPacket*> free_output_packets_;

  // The order of output_buffer_pool_ and in_use_by_client_ matters, so that
  // destruction of in_use_by_client_ happens first, because those destructing
  // will return buffers to output_buffer_pool_.
  BufferPool output_buffer_pool_;
  std::map<CodecPacket*, VaApiOutput> in_use_by_client_ FXL_GUARDED_BY(lock_);

  // Buffers the client has added but that we cannot use until configuration is
  // complete.
  std::vector<const CodecBuffer*> staged_output_buffers_;

  uint64_t input_format_details_version_ordinal_;

  std::optional<fuchsia::sysmem::SingleBufferSettings> buffer_settings_[kPortCount];

  std::deque<std::pair<int32_t, uint64_t>> stream_to_pts_map_;
  int32_t next_stream_id_{};

  async::Loop input_processing_loop_;
  thrd_t input_processing_thread_;
};

#endif  // SRC_MEDIA_CODEC_CODECS_VAAPI_CODEC_ADAPTER_VAAPI_DECODER_H_

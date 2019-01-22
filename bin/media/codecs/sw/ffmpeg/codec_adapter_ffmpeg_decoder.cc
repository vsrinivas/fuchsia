// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_adapter_ffmpeg_decoder.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/defer.h>
#include <lib/media/codec_impl/codec_buffer.h>

namespace {

// TODO(turnage): Allow a range of packet count for the client instead of
// forcing a particular number.
static constexpr uint32_t kPacketCountForClientForced = 5;
static constexpr uint32_t kDefaultPacketCountForClient =
    kPacketCountForClientForced;

// We want at least 16 packets codec side because that's the worst case scenario
// for h264 keeping frames around (if the media has set its reference frame
// option to 16).
//
// TODO(turnage): Dynamically detect how many reference frames are needed by a
// given stream, to allow fewer buffers to be allocated.
static constexpr uint32_t kPacketCount = kPacketCountForClientForced + 16;

}  // namespace

CodecAdapterFfmpegDecoder::CodecAdapterFfmpegDecoder(
    std::mutex& lock, CodecAdapterEvents* codec_adapter_events)
    : CodecAdapter(lock, codec_adapter_events),
      input_queue_(),
      free_output_buffers_(),
      free_output_packets_(),
      input_processing_loop_(&kAsyncLoopConfigNoAttachToThread) {
  ZX_DEBUG_ASSERT(codec_adapter_events);
}

CodecAdapterFfmpegDecoder::~CodecAdapterFfmpegDecoder() = default;

bool CodecAdapterFfmpegDecoder::
    IsCoreCodecRequiringOutputConfigForFormatDetection() {
  return false;
}

void CodecAdapterFfmpegDecoder::CoreCodecInit(
    const fuchsia::media::FormatDetails& initial_input_format_details) {
  // Will always be 0 for now.
  input_format_details_version_ordinal_ =
      initial_input_format_details.format_details_version_ordinal;
  zx_status_t result = input_processing_loop_.StartThread(
      "input_processing_thread_", &input_processing_thread_);
  if (result != ZX_OK) {
    events_->onCoreCodecFailCodec(
        "CodecCodecInit(): Failed to start input processing thread with "
        "zx_status_t: %d",
        result);
    return;
  }
}

void CodecAdapterFfmpegDecoder::CoreCodecStartStream() {
  ZX_DEBUG_ASSERT(avcodec_context_ == nullptr);
  // It's ok for RecycleInputPacket to make a packet free anywhere in this
  // sequence. Nothing else ought to be happening during CoreCodecStartStream
  // (in this or any other thread).
  input_queue_.Reset();
  free_output_buffers_.Reset(/*keep_data=*/true);
  free_output_packets_.Reset(/*keep_data=*/true);

  zx_status_t post_result = async::PostTask(input_processing_loop_.dispatcher(),
                                            [this] { ProcessInputLoop(); });
  ZX_ASSERT_MSG(
      post_result == ZX_OK,
      "async::PostTask() failed to post input processing loop - result: %d\n",
      post_result);
}

void CodecAdapterFfmpegDecoder::CoreCodecQueueInputFormatDetails(
    const fuchsia::media::FormatDetails& per_stream_override_format_details) {
  // TODO(turnage): Accept midstream and interstream input format changes.
  // For now these should always be 0, so assert to notice if anything changes.
  ZX_ASSERT(per_stream_override_format_details.format_details_version_ordinal ==
            input_format_details_version_ordinal_);
  input_queue_.Push(
      CodecInputItem::FormatDetails(per_stream_override_format_details));
}

void CodecAdapterFfmpegDecoder::CoreCodecQueueInputPacket(CodecPacket* packet) {
  input_queue_.Push(CodecInputItem::Packet(packet));
}

void CodecAdapterFfmpegDecoder::CoreCodecQueueInputEndOfStream() {
  input_queue_.Push(CodecInputItem::EndOfStream());
}

void CodecAdapterFfmpegDecoder::CoreCodecStopStream() {
  input_queue_.StopAllWaits();
  free_output_buffers_.StopAllWaits();
  free_output_packets_.StopAllWaits();

  WaitForInputProcessingLoopToEnd();
  avcodec_context_ = nullptr;

  auto queued_input_items =
      BlockingMpscQueue<CodecInputItem>::Extract(std::move(input_queue_));
  while (!queued_input_items.empty()) {
    CodecInputItem input_item = std::move(queued_input_items.front());
    queued_input_items.pop();
    if (input_item.is_packet()) {
      events_->onCoreCodecInputPacketDone(input_item.packet());
    }
  }
}

void CodecAdapterFfmpegDecoder::CoreCodecAddBuffer(CodecPort port,
                                                   const CodecBuffer* buffer) {
  if (port != kOutputPort) {
    return;
  }
  free_output_buffers_.Push(std::move(buffer));
}

void CodecAdapterFfmpegDecoder::CoreCodecConfigureBuffers(
    CodecPort port, const std::vector<std::unique_ptr<CodecPacket>>& packets) {
  // Nothing to do here.
}

void CodecAdapterFfmpegDecoder::CoreCodecRecycleOutputPacket(
    CodecPacket* packet) {
  if (packet->buffer()) {
    AvCodecContext::AVFramePtr frame;
    {
      std::lock_guard<std::mutex> lock(lock_);
      frame = std::move(in_use_by_client_[packet]);
      in_use_by_client_.erase(packet);
    }

    // ~ frame, which may trigger our buffer free callback.
  }

  free_output_packets_.Push(std::move(packet));
}

void CodecAdapterFfmpegDecoder::CoreCodecEnsureBuffersNotConfigured(
    CodecPort port) {
  if (port != kOutputPort) {
    // We don't do anything with input buffers.
    return;
  }
  free_output_buffers_.Reset();
  free_output_packets_.Reset();

  {
    std::map<CodecPacket*, AvCodecContext::AVFramePtr> to_drop;
    {
      std::lock_guard<std::mutex> lock(lock_);
      std::swap(to_drop, in_use_by_client_);
    }
    // ~ to_drop
  }

  {
    std::lock_guard<std::mutex> lock(lock_);
    // Given that we currently fail the codec on mid-stream output format
    // change (elsewhere), the decoder won't have frames referenced here.
    ZX_DEBUG_ASSERT(in_use_by_decoder_.empty());
  }
}

std::unique_ptr<const fuchsia::media::StreamOutputConfig>
CodecAdapterFfmpegDecoder::CoreCodecBuildNewOutputConfig(
    uint64_t stream_lifetime_ordinal,
    uint64_t new_output_buffer_constraints_version_ordinal,
    uint64_t new_output_format_details_version_ordinal,
    bool buffer_constraints_action_required) {
  std::lock_guard<std::mutex> lock(lock_);
  ZX_ASSERT(decoded_output_info_.has_value());

  auto& [uncompressed_format, per_packet_buffer_bytes] =
      decoded_output_info_.value();

  auto config = std::make_unique<fuchsia::media::StreamOutputConfig>();

  config->stream_lifetime_ordinal = stream_lifetime_ordinal;
  // For the moment, there will be only one StreamOutputConfig, and it'll need
  // output buffers configured for it.
  ZX_DEBUG_ASSERT(buffer_constraints_action_required);
  config->buffer_constraints_action_required =
      buffer_constraints_action_required;
  config->buffer_constraints.buffer_constraints_version_ordinal =
      new_output_buffer_constraints_version_ordinal;

  // 0 is intentionally invalid - the client must fill out this field.
  config->buffer_constraints.default_settings.buffer_lifetime_ordinal = 0;
  config->buffer_constraints.default_settings
      .buffer_constraints_version_ordinal =
      new_output_buffer_constraints_version_ordinal;
  config->buffer_constraints.default_settings.packet_count_for_server =
      kPacketCount - kPacketCountForClientForced;
  config->buffer_constraints.default_settings.packet_count_for_client =
      kDefaultPacketCountForClient;
  config->buffer_constraints.default_settings.per_packet_buffer_bytes =
      per_packet_buffer_bytes;
  config->buffer_constraints.default_settings.single_buffer_mode = false;

  // For the moment, let's just force the client to allocate this exact size.
  config->buffer_constraints.per_packet_buffer_bytes_min =
      per_packet_buffer_bytes;
  config->buffer_constraints.per_packet_buffer_bytes_recommended =
      per_packet_buffer_bytes;
  config->buffer_constraints.per_packet_buffer_bytes_max =
      per_packet_buffer_bytes;

  // For the moment, let's just force the client to set this exact number of
  // frames for the codec.
  config->buffer_constraints.packet_count_for_server_min =
      kPacketCount - kPacketCountForClientForced;
  config->buffer_constraints.packet_count_for_server_recommended =
      kPacketCount - kPacketCountForClientForced;
  config->buffer_constraints.packet_count_for_server_recommended_max =
      kPacketCount - kPacketCountForClientForced;
  config->buffer_constraints.packet_count_for_server_max =
      kPacketCount - kPacketCountForClientForced;

  config->buffer_constraints.packet_count_for_client_min =
      kPacketCountForClientForced;
  config->buffer_constraints.packet_count_for_client_max =
      kPacketCountForClientForced;

  config->buffer_constraints.single_buffer_mode_allowed = false;
  config->buffer_constraints.is_physically_contiguous_required = false;

  config->format_details.format_details_version_ordinal =
      new_output_format_details_version_ordinal;
  config->format_details.mime_type = "video/raw";

  fuchsia::media::VideoFormat video_format;
  video_format.set_uncompressed(std::move(uncompressed_format));

  config->format_details.domain =
      std::make_unique<fuchsia::media::DomainFormat>();
  config->format_details.domain->set_video(std::move(video_format));

  return config;
}

void CodecAdapterFfmpegDecoder::
    CoreCodecMidStreamOutputBufferReConfigPrepare() {
  // Nothing to do here for now.
}

void CodecAdapterFfmpegDecoder::CoreCodecMidStreamOutputBufferReConfigFinish() {
  // Nothing to do here for now.
}

// static
void CodecAdapterFfmpegDecoder::BufferFreeCallbackRouter(void* opaque,
                                                         uint8_t* data) {
  auto decoder = reinterpret_cast<CodecAdapterFfmpegDecoder*>(opaque);
  decoder->BufferFreeHandler(data);
}

void CodecAdapterFfmpegDecoder::BufferFreeHandler(uint8_t* data) {
  const CodecBuffer* buffer;
  {
    std::lock_guard<std::mutex> lock(lock_);
    auto buffer_iter = in_use_by_decoder_.find(data);
    ZX_DEBUG_ASSERT(buffer_iter != in_use_by_decoder_.end());
    in_use_by_decoder_.erase(data);
    buffer = buffer_iter->second.buffer;
  }
  free_output_buffers_.Push(std::move(buffer));
}

void CodecAdapterFfmpegDecoder::ProcessInputLoop() {
  std::optional<CodecInputItem> maybe_input_item;
  while ((maybe_input_item = input_queue_.WaitForElement())) {
    CodecInputItem input_item = std::move(maybe_input_item.value());
    if (input_item.is_format_details()) {
      if (avcodec_context_) {
        events_->onCoreCodecFailCodec(
            "Midstream input  format change is not supported.");
        return;
      }
      auto maybe_avcodec_context = AvCodecContext::CreateDecoder(
          input_item.format_details(),
          [this](const AvCodecContext::DecodedOutputInfo& decoded_output_info,
                 AVCodecContext* avcodec_context, AVFrame* frame, int flags) {
            return GetBuffer(decoded_output_info, avcodec_context, frame,
                             flags);
          });
      if (!maybe_avcodec_context) {
        events_->onCoreCodecFailCodec("Failed to create ffmpeg decoder.");
        return;
      }
      avcodec_context_ = std::move(maybe_avcodec_context.value());
    } else if (input_item.is_end_of_stream()) {
      ZX_ASSERT(avcodec_context_);
      avcodec_context_->EndStream();
      DecodeFrames();
    } else if (input_item.is_packet()) {
      ZX_DEBUG_ASSERT(avcodec_context_);
      int result = avcodec_context_->SendPacket(input_item.packet());
      if (result < 0) {
        events_->onCoreCodecFailCodec(
            "Failed to decode input packet with ffmpeg error: %s",
            av_err2str(result));
        return;
      }

      events_->onCoreCodecInputPacketDone(input_item.packet());

      DecodeFrames();
    }
  }
}

int CodecAdapterFfmpegDecoder::GetBuffer(
    const AvCodecContext::DecodedOutputInfo& decoded_output_info,
    AVCodecContext* avcodec_context, AVFrame* frame, int flags) {
  size_t buffer_size;
  bool should_config_output = false;
  bool output_increased_in_size = false;
  bool need_new_buffers = false;
  {
    std::lock_guard<std::mutex> lock(lock_);
    need_new_buffers = !decoded_output_info_;
    if (!decoded_output_info_ ||
        (*decoded_output_info_).format != decoded_output_info.format) {
      output_increased_in_size =
          decoded_output_info_.has_value() &&
          decoded_output_info.buffer_bytes_needed >
              (*decoded_output_info_).buffer_bytes_needed;
      decoded_output_info_ = {
          .format = fidl::Clone(decoded_output_info.format),
          .buffer_bytes_needed = decoded_output_info.buffer_bytes_needed};
      buffer_size = (*decoded_output_info_).buffer_bytes_needed;
      should_config_output = true;
    }
  }

  if (output_increased_in_size) {
    events_->onCoreCodecFailCodec(
        "Midstream output config change to larger format is not supported.");
    return avcodec_default_get_buffer2(avcodec_context, frame, flags);
  }

  if (should_config_output) {
    events_->onCoreCodecMidStreamOutputConfigChange(
        /*output_re_config_required=*/need_new_buffers);
  }

  std::optional<const CodecBuffer*> maybe_buffer =
      free_output_buffers_.WaitForElement();
  if (!maybe_buffer) {
    // This should only happen if the stream is stopped. In this case we let
    // ffmpeg allocate some memory just so it can conclude gracefully.
    return avcodec_default_get_buffer2(avcodec_context, frame, flags);
  }
  ZX_ASSERT(*maybe_buffer);
  const CodecBuffer* buffer = *maybe_buffer;

  AVBufferRef* buffer_ref = av_buffer_create(
      buffer->buffer_base(), static_cast<int>(buffer_size),
      BufferFreeCallbackRouter, reinterpret_cast<void*>(this), flags);

  int frame_bytes_or_error =
      av_image_fill_arrays(frame->data, frame->linesize, buffer_ref->data,
                           static_cast<AVPixelFormat>(frame->format),
                           frame->width, frame->height, 1);

  // IYUV is not YV12. Ffmpeg only decodes into IYUV. The difference between
  // YV12 and IYUV is the order of the U and V planes. Here we trick Ffmpeg
  // into writing them in YV12 order relative to one another.
  std::swap(frame->data[1], frame->data[2]);

  if (frame_bytes_or_error < 0) {
    return frame_bytes_or_error;
  }
  frame->buf[0] = buffer_ref;
  // ffmpeg says to set extended_data to data if we're not using extended_data
  frame->extended_data = frame->data;

  ZX_DEBUG_ASSERT(buffer->buffer_base() == frame->data[0]);
  {
    std::lock_guard<std::mutex> lock(lock_);
    in_use_by_decoder_.emplace(
        buffer->buffer_base(),
        BufferAllocation{
            .buffer = buffer,
            .bytes_used = static_cast<size_t>(frame_bytes_or_error)});
  }

  return 0;
}

void CodecAdapterFfmpegDecoder::DecodeFrames() {
  ZX_DEBUG_ASSERT(thrd_current() == input_processing_thread_);
  ZX_DEBUG_ASSERT(avcodec_context_);

  while (true) {
    auto [error, frame] = avcodec_context_->ReceiveFrame();
    if (error == AVERROR(EAGAIN)) {
      return;
    } else if (error == AVERROR_EOF) {
      events_->onCoreCodecOutputEndOfStream(/*error_detected_before=*/false);
      return;
    } else if (error < 0) {
      events_->onCoreCodecFailCodec(
          "DecodeFrames(): Failed to decode frame: %s", av_err2str(error));
      return;
    }

    std::optional<CodecPacket*> maybe_output_packet =
        free_output_packets_.WaitForElement();
    if (!maybe_output_packet) {
      return;
    }
    auto output_packet = *maybe_output_packet;

    BufferAllocation buffer_alloc;
    {
      std::lock_guard<std::mutex> lock(lock_);
      auto codec_buffer_iter = in_use_by_decoder_.find(frame->data[0]);
      ZX_ASSERT(codec_buffer_iter != in_use_by_decoder_.end());
      buffer_alloc = codec_buffer_iter->second;
    }

    output_packet->SetBuffer(buffer_alloc.buffer);
    output_packet->SetStartOffset(0);
    output_packet->SetValidLengthBytes(buffer_alloc.bytes_used);
    output_packet->SetTimstampIsh(frame->pts);

    {
      std::lock_guard<std::mutex> lock(lock_);
      ZX_DEBUG_ASSERT(in_use_by_client_.find(output_packet) ==
                      in_use_by_client_.end());
      in_use_by_client_.emplace(output_packet, std::move(frame));
    }

    events_->onCoreCodecOutputPacket(output_packet,
                                     /*error_detected_before=*/false,
                                     /*error_detected_during=*/false);
  }
}

void CodecAdapterFfmpegDecoder::WaitForInputProcessingLoopToEnd() {
  ZX_DEBUG_ASSERT(thrd_current() != input_processing_thread_);

  std::condition_variable stream_stopped_condition;
  bool stream_stopped = false;
  zx_status_t post_result =
      async::PostTask(input_processing_loop_.dispatcher(),
                      [this, &stream_stopped, &stream_stopped_condition] {
                        {
                          std::lock_guard<std::mutex> lock(lock_);
                          stream_stopped = true;
                        }
                        stream_stopped_condition.notify_all();
                      });
  ZX_ASSERT_MSG(
      post_result == ZX_OK,
      "async::PostTask() failed to post input processing loop - result: %d\n",
      post_result);

  std::unique_lock<std::mutex> lock(lock_);
  stream_stopped_condition.wait(lock,
                                [&stream_stopped] { return stream_stopped; });
}

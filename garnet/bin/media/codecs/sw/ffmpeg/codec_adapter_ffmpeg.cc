// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_adapter_ffmpeg.h"

#include <lib/async/cpp/task.h>
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

CodecAdapterFfmpeg::CodecAdapterFfmpeg(std::mutex& lock,
                                       CodecAdapterEvents* codec_adapter_events)
    : CodecAdapter(lock, codec_adapter_events),
      input_queue_(),
      free_output_packets_(),
      input_processing_loop_(&kAsyncLoopConfigNoAttachToThread) {
  ZX_DEBUG_ASSERT(codec_adapter_events);
}

CodecAdapterFfmpeg::~CodecAdapterFfmpeg() = default;

bool CodecAdapterFfmpeg::IsCoreCodecRequiringOutputConfigForFormatDetection() {
  return false;
}

void CodecAdapterFfmpeg::CoreCodecInit(
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

void CodecAdapterFfmpeg::CoreCodecStartStream() {
  ZX_DEBUG_ASSERT(avcodec_context_ == nullptr);
  // It's ok for RecycleInputPacket to make a packet free anywhere in this
  // sequence. Nothing else ought to be happening during CoreCodecStartStream
  // (in this or any other thread).
  input_queue_.Reset();
  output_buffer_pool_.Reset(/*keep_data=*/true);
  free_output_packets_.Reset(/*keep_data=*/true);

  zx_status_t post_result = async::PostTask(input_processing_loop_.dispatcher(),
                                            [this] { ProcessInputLoop(); });
  ZX_ASSERT_MSG(
      post_result == ZX_OK,
      "async::PostTask() failed to post input processing loop - result: %d\n",
      post_result);
}

void CodecAdapterFfmpeg::CoreCodecQueueInputFormatDetails(
    const fuchsia::media::FormatDetails& per_stream_override_format_details) {
  // TODO(turnage): Accept midstream and interstream input format changes.
  // For now these should always be 0, so assert to notice if anything changes.
  ZX_ASSERT(per_stream_override_format_details.format_details_version_ordinal ==
            input_format_details_version_ordinal_);
  input_queue_.Push(
      CodecInputItem::FormatDetails(per_stream_override_format_details));
}

void CodecAdapterFfmpeg::CoreCodecQueueInputPacket(CodecPacket* packet) {
  input_queue_.Push(CodecInputItem::Packet(packet));
}

void CodecAdapterFfmpeg::CoreCodecQueueInputEndOfStream() {
  input_queue_.Push(CodecInputItem::EndOfStream());
}

void CodecAdapterFfmpeg::CoreCodecStopStream() {
  input_queue_.StopAllWaits();
  output_buffer_pool_.StopAllWaits();
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

void CodecAdapterFfmpeg::CoreCodecAddBuffer(CodecPort port,
                                            const CodecBuffer* buffer) {
  if (port != kOutputPort) {
    return;
  }
  output_buffer_pool_.AddBuffer(buffer);
}

void CodecAdapterFfmpeg::CoreCodecConfigureBuffers(
    CodecPort port, const std::vector<std::unique_ptr<CodecPacket>>& packets) {
  // Nothing to do here.
}

void CodecAdapterFfmpeg::CoreCodecRecycleOutputPacket(CodecPacket* packet) {
  UnreferenceOutputPacket(packet);
  free_output_packets_.Push(std::move(packet));
}

void CodecAdapterFfmpeg::CoreCodecEnsureBuffersNotConfigured(CodecPort port) {
  if (port != kOutputPort) {
    // We don't do anything with input buffers.
    return;
  }

  output_buffer_pool_.Reset();
  UnreferenceClientBuffers();

  // Given that we currently fail the codec on mid-stream output format
  // change (elsewhere), the decoder won't have frames referenced here.
  ZX_DEBUG_ASSERT(!output_buffer_pool_.has_buffers_in_use());

  free_output_packets_.Reset();
}

void CodecAdapterFfmpeg::CoreCodecMidStreamOutputBufferReConfigPrepare() {
  // Nothing to do here for now.
}

void CodecAdapterFfmpeg::CoreCodecMidStreamOutputBufferReConfigFinish() {
  // Nothing to do here for now.
}

std::unique_ptr<const fuchsia::media::StreamOutputConfig>
CodecAdapterFfmpeg::CoreCodecBuildNewOutputConfig(
    uint64_t stream_lifetime_ordinal,
    uint64_t new_output_buffer_constraints_version_ordinal,
    uint64_t new_output_format_details_version_ordinal,
    bool buffer_constraints_action_required) {
  auto [format_details, per_packet_buffer_bytes] = OutputFormatDetails();

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

  config->format_details = std::move(format_details);
  config->format_details.format_details_version_ordinal =
      new_output_format_details_version_ordinal;

  return config;
}

void CodecAdapterFfmpeg::WaitForInputProcessingLoopToEnd() {
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

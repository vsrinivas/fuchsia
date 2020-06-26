// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/drivers/amlogic_encoder/codec_adapter_h264.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/media/codec_impl/codec_frame.h>
#include <lib/trace/event.h>
#include <lib/zx/bti.h>

#include "src/media/drivers/amlogic_encoder/device_ctx.h"
#include "src/media/drivers/amlogic_encoder/macros.h"

constexpr uint32_t kOutputBufferMinSizeBytes = 500 * 1024;
constexpr uint32_t kOutputBufferMaxSizeBytes = 0;  // unbounded
constexpr uint32_t kOutputMinBufferCountForCamping = 2;
constexpr uint32_t kOutputMaxBufferCount = 0;  // unbounded
constexpr uint32_t kInputMinBufferCountForCamping = 2;
constexpr uint32_t kInputMaxBufferCount = 0;  // unbounded

constexpr uint64_t kInputBufferConstraintsVersionOrdinal = 1;
constexpr uint64_t kInputDefaultBufferConstraintsVersionOrdinal =
    kInputBufferConstraintsVersionOrdinal;

constexpr uint32_t kInputPacketCountForServerMin = 2;
constexpr uint32_t kInputPacketCountForServerRecommended = 3;
constexpr uint32_t kInputPacketCountForServerRecommendedMax = 3;
constexpr uint32_t kInputPacketCountForServerMax = 3;
constexpr uint32_t kInputDefaultPacketCountForServer = kInputPacketCountForServerRecommended;

constexpr uint32_t kInputPacketCountForClientMin = 2;
constexpr uint32_t kInputPacketCountForClientMax = std::numeric_limits<uint32_t>::max();
constexpr uint32_t kInputDefaultPacketCountForClient = 3;

constexpr uint32_t kInputPerPacketBufferBytesMin = 8 * 1024;
constexpr uint32_t kInputPerPacketBufferBytesRecommended = 1920 * 1080 * 3 / 2;
constexpr uint32_t kInputPerPacketBufferBytesMax = 1920 * 1080 * 3 / 2;
constexpr uint32_t kInputDefaultPerPacketBufferBytes = kInputPerPacketBufferBytesRecommended;

constexpr bool kInputSingleBufferModeAllowed = false;
constexpr bool kInputDefaultSingleBufferMode = false;

CodecAdapterH264::CodecAdapterH264(std::mutex& lock, CodecAdapterEvents* codec_adapter_events,
                                   DeviceCtx* device)
    : CodecAdapter(lock, codec_adapter_events),
      device_(device),
      input_processing_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
  ZX_DEBUG_ASSERT(device_);
}

CodecAdapterH264::~CodecAdapterH264() {
  // nothing else to do here, at least not until we aren't calling PowerOff() in
  // CoreCodecStopStream().
}

bool CodecAdapterH264::IsCoreCodecRequiringOutputConfigForFormatDetection() { return false; }

bool CodecAdapterH264::IsCoreCodecMappedBufferUseful(CodecPort port) {
  if (port == kInputPort) {
    return false;
  } else {
    // we likely want to be able to copy the encoded output to support sending a NAL across multiple
    // output packets, with each new NAL starting a fresh packet so it gets a place to put its PTS
    return true;
  }
}

bool CodecAdapterH264::IsCoreCodecHwBased(CodecPort port) { return true; }

zx::unowned_bti CodecAdapterH264::CoreCodecBti() { return zx::unowned_bti(device_->bti()); }

void CodecAdapterH264::CoreCodecInit(
    const fuchsia::media::FormatDetails& initial_input_format_details) {
  ZX_DEBUG_ASSERT(!output_sink_);
  zx_status_t result = input_processing_loop_.StartThread(
      "CodecAdapterH264::input_processing_thread_", &input_processing_thread_);
  if (result != ZX_OK) {
    events_->onCoreCodecFailCodec(
        "In CodecAdapterH264::CoreCodecInit(), StartThread() failed (input)");
    return;
  }

  initial_input_format_details_ = fidl::Clone(initial_input_format_details);
  latest_input_format_details_ = fidl::Clone(initial_input_format_details);

  if (!latest_input_format_details_.has_domain() ||
      !latest_input_format_details_.domain().is_video() ||
      !latest_input_format_details_.domain().video().is_uncompressed()) {
    events_->onCoreCodecFailCodec(
        "In CodecAdapterH264::CoreCodecInit(), StartThread() failed (input)");
    return;
  }

  width_ = latest_input_format_details_.domain().video().uncompressed().image_format.display_width;
  height_ =
      latest_input_format_details_.domain().video().uncompressed().image_format.display_height;
  min_stride_ = width_;

  output_sink_.emplace(/*sender=*/
                       [this](CodecPacket* output_packet) {
                         TRACE_DURATION("media", "Media:PacketSent");
                         events_->onCoreCodecOutputPacket(output_packet,
                                                          /*error_detected_before=*/false,
                                                          /*error_detected_during=*/false);
                         return OutputSink::kSuccess;
                       },
                       /*writer_thread=*/input_processing_thread_);

  TRACE_DURATION("media", "Media:Start");
  result = device_->EncoderInit(initial_input_format_details_);
  if (result != ZX_OK) {
    events_->onCoreCodecFailCodec("In CodecAdapterH264::CoreCodecInit(), EncoderInit failed");
    return;
  }
}

void CodecAdapterH264::CoreCodecStartStream() {
  ZX_DEBUG_ASSERT(!output_sink_->HasPendingPacket());
  // The keep_data true keeps any free buffers and free packets.
  output_sink_->Reset(/*keep_data=*/true);
}

void CodecAdapterH264::CoreCodecQueueInputFormatDetails(
    const fuchsia::media::FormatDetails& per_stream_override_format_details) {
  // TODO(dustingreen): Consider letting the client specify profile/level info
  // in the FormatDetails at least optionally, and possibly sizing input
  // buffer constraints and/or other buffers based on that.

  QueueInputItem(CodecInputItem::FormatDetails(per_stream_override_format_details));
}

void CodecAdapterH264::CoreCodecQueueInputPacket(CodecPacket* packet) {
  QueueInputItem(CodecInputItem::Packet(packet));
}

void CodecAdapterH264::CoreCodecQueueInputEndOfStream() {
  // This queues a marker, but doesn't force the HW to necessarily decode all
  // the way up to the marker, depending on whether the client closes the stream
  // or switches to a different stream first - in those cases it's fine for the
  // marker to never show up as output EndOfStream.

  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    is_input_end_of_stream_queued_ = true;
  }  // ~lock

  QueueInputItem(CodecInputItem::EndOfStream());
}

// TODO(dustingreen): See comment on CoreCodecStartStream() re. not deleting
// creating as much stuff for each stream.
void CodecAdapterH264::CoreCodecStopStream() {
  TRACE_DURATION("media", "Media:Stop");
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);

    // This helps any previously-queued ProcessInput() calls return faster, and
    // is checked before calling WaitForParsingCompleted() in case
    // TryStartCancelParsing() does nothing.
    is_cancelling_input_processing_ = true;
  }

  output_sink_->StopAllWaits();

  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    std::condition_variable stop_input_processing_condition;
    // We know there won't be any new queuing of input, so once this posted work
    // runs, we know all previously-queued ProcessInput() calls have returned.
    PostToInputProcessingThread([this, &stop_input_processing_condition] {
      std::list<CodecInputItem> leftover_input_items;
      {  // scope lock
        std::lock_guard<std::mutex> lock(lock_);
        ZX_DEBUG_ASSERT(is_cancelling_input_processing_);
        leftover_input_items = std::move(input_queue_);
        is_cancelling_input_processing_ = false;
      }  // ~lock
      for (auto& input_item : leftover_input_items) {
        if (input_item.is_packet()) {
          events_->onCoreCodecInputPacketDone(input_item.packet());
        }
      }
      stop_input_processing_condition.notify_all();
    });
    while (is_cancelling_input_processing_) {
      stop_input_processing_condition.wait(lock);
    }
    ZX_DEBUG_ASSERT(!is_cancelling_input_processing_);
  }  // ~lock

  // Stop processing queued frames.
  device_->StopEncoder();
}

void CodecAdapterH264::CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) {
  if (port == kInputPort) {
    const char* kInputBufferName = "H264InputBuffer";
    buffer->vmo().set_property(ZX_PROP_NAME, kInputBufferName, strlen(kInputBufferName));
  } else if (port == kOutputPort) {
    const char* kOutputBufferName = "H264OutputBuffer";
    buffer->vmo().set_property(ZX_PROP_NAME, kOutputBufferName, strlen(kOutputBufferName));
    staged_buffers_.Push(buffer);
  }
}

void CodecAdapterH264::CoreCodecConfigureBuffers(
    CodecPort port, const std::vector<std::unique_ptr<CodecPacket>>& packets) {}

void CodecAdapterH264::CoreCodecRecycleOutputPacket(CodecPacket* packet) {
  output_sink_->AddOutputPacket(packet);
}

void CodecAdapterH264::CoreCodecEnsureBuffersNotConfigured(CodecPort port) {
  std::lock_guard<std::mutex> lock(lock_);

  // This adapter should ensure that zero old CodecPacket* or CodecBuffer*
  // remain in this adapter (or below).

  if (port == kInputPort) {
    // There shouldn't be any queued input at this point, but if there is any,
    // fail here even in a release build.
    ZX_ASSERT(input_queue_.empty());
  } else {
    ZX_DEBUG_ASSERT(port == kOutputPort);
    output_sink_->Reset();
  }
}

std::unique_ptr<const fuchsia::media::StreamBufferConstraints>
CodecAdapterH264::CoreCodecBuildNewInputConstraints() {
  auto constraints = std::make_unique<fuchsia::media::StreamBufferConstraints>();

  constraints->set_buffer_constraints_version_ordinal(kInputBufferConstraintsVersionOrdinal);

  constraints->mutable_default_settings()
      ->set_buffer_lifetime_ordinal(0)
      .set_buffer_constraints_version_ordinal(kInputDefaultBufferConstraintsVersionOrdinal)
      .set_packet_count_for_server(kInputDefaultPacketCountForServer)
      .set_packet_count_for_client(kInputDefaultPacketCountForClient)
      .set_per_packet_buffer_bytes(kInputDefaultPerPacketBufferBytes)
      .set_single_buffer_mode(kInputDefaultSingleBufferMode);

  constraints->set_per_packet_buffer_bytes_min(kInputPerPacketBufferBytesMin)
      .set_per_packet_buffer_bytes_recommended(kInputPerPacketBufferBytesRecommended)
      .set_per_packet_buffer_bytes_max(kInputPerPacketBufferBytesMax)
      .set_packet_count_for_server_min(kInputPacketCountForServerMin)
      .set_packet_count_for_server_recommended(kInputPacketCountForServerRecommended)
      .set_packet_count_for_server_recommended_max(kInputPacketCountForServerRecommendedMax)
      .set_packet_count_for_server_max(kInputPacketCountForServerMax)
      .set_packet_count_for_client_min(kInputPacketCountForClientMin)
      .set_packet_count_for_client_max(kInputPacketCountForClientMax)
      .set_single_buffer_mode_allowed(kInputSingleBufferModeAllowed)
      .set_is_physically_contiguous_required(true);

  return constraints;
}

std::unique_ptr<const fuchsia::media::StreamOutputConstraints>
CodecAdapterH264::CoreCodecBuildNewOutputConstraints(
    uint64_t stream_lifetime_ordinal, uint64_t new_output_buffer_constraints_version_ordinal,
    bool buffer_constraints_action_required) {
  // sysmem constraints take priority over StreamOutputConstraints so just use some defaults that
  // pass checks here
  constexpr uint32_t kDefaultPacketCountForClient = 1;
  constexpr uint32_t kDefaultPacketCountForServer = 1;
  constexpr uint32_t kRecommendedMaxPacketCount = std::numeric_limits<uint32_t>::max();
  constexpr uint32_t kServerMaxPacketCount = std::numeric_limits<uint32_t>::max();
  constexpr uint32_t kClientMaxPacketCount = std::numeric_limits<uint32_t>::max();

  std::unique_ptr<fuchsia::media::StreamOutputConstraints> config =
      std::make_unique<fuchsia::media::StreamOutputConstraints>();

  config->set_stream_lifetime_ordinal(stream_lifetime_ordinal);

  auto* constraints = config->mutable_buffer_constraints();
  auto* default_settings = constraints->mutable_default_settings();

  // For the moment, there will be only one StreamOutputConstraints, and it'll
  // need output buffers configured for it.
  ZX_DEBUG_ASSERT(buffer_constraints_action_required);
  config->set_buffer_constraints_action_required(buffer_constraints_action_required);
  constraints->set_buffer_constraints_version_ordinal(
      new_output_buffer_constraints_version_ordinal);

  // 0 is intentionally invalid - the client must fill out this field.
  default_settings->set_buffer_lifetime_ordinal(0)
      .set_buffer_constraints_version_ordinal(new_output_buffer_constraints_version_ordinal)
      .set_packet_count_for_server(kDefaultPacketCountForServer)
      .set_packet_count_for_client(kDefaultPacketCountForClient)
      .set_per_packet_buffer_bytes(kOutputBufferMinSizeBytes)
      .set_single_buffer_mode(false);

  // For the moment, let's tell the client to allocate this exact size.
  constraints->set_per_packet_buffer_bytes_min(kOutputBufferMinSizeBytes)
      .set_per_packet_buffer_bytes_recommended(kOutputBufferMinSizeBytes)
      .set_per_packet_buffer_bytes_max(kOutputBufferMaxSizeBytes)
      .set_packet_count_for_server_min(kDefaultPacketCountForServer)
      .set_packet_count_for_server_recommended(kDefaultPacketCountForServer)
      .set_packet_count_for_server_recommended_max(kRecommendedMaxPacketCount)
      .set_packet_count_for_server_max(kServerMaxPacketCount)
      .set_packet_count_for_client_min(0)
      .set_packet_count_for_client_max(kClientMaxPacketCount);

  // False because it's not required and not encouraged for a video encoder
  // output to allow single buffer mode.
  constraints->set_single_buffer_mode_allowed(false);

  constraints->set_is_physically_contiguous_required(true);

  return config;
}

fuchsia::sysmem::BufferCollectionConstraints
CodecAdapterH264::CoreCodecGetBufferCollectionConstraints(
    CodecPort port, const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
    const fuchsia::media::StreamBufferPartialSettings& partial_settings) {
  fuchsia::sysmem::BufferCollectionConstraints result;

  // For now, we didn't report support for single_buffer_mode, and CodecImpl
  // will have failed the codec already by this point if the client tried to
  // use single_buffer_mode.
  //
  // TODO(dustingreen): Support single_buffer_mode on input (only).
  ZX_DEBUG_ASSERT(!partial_settings.has_single_buffer_mode() ||
                  !partial_settings.single_buffer_mode());
  // The CodecImpl won't hand us the sysmem token, so we shouldn't expect to
  // have the token here.
  ZX_DEBUG_ASSERT(!partial_settings.has_sysmem_token());

  if (port == kInputPort) {
    result.min_buffer_count_for_camping = kInputMinBufferCountForCamping;
    result.max_buffer_count = kInputMaxBufferCount;
  } else {
    result.min_buffer_count_for_camping = kOutputMinBufferCountForCamping;
    result.max_buffer_count = kOutputMaxBufferCount;
  }

  uint32_t per_packet_buffer_bytes_min;
  uint32_t per_packet_buffer_bytes_max;
  if (port == kOutputPort) {
    per_packet_buffer_bytes_min = kOutputBufferMinSizeBytes;
    per_packet_buffer_bytes_max = kOutputBufferMaxSizeBytes;
  } else {
    ZX_DEBUG_ASSERT(port == kInputPort);
    // NV12, based on min stride.
    per_packet_buffer_bytes_min = min_stride_ * height_ * 3 / 2;
    per_packet_buffer_bytes_max = 0xFFFFFFFF;
  }

  result.has_buffer_memory_constraints = true;
  result.buffer_memory_constraints.min_size_bytes = per_packet_buffer_bytes_min;
  result.buffer_memory_constraints.max_size_bytes = per_packet_buffer_bytes_max;
  // amlogic requires physically contiguous on both input and output
  result.buffer_memory_constraints.physically_contiguous_required = true;
  result.buffer_memory_constraints.secure_required = false;
  result.buffer_memory_constraints.cpu_domain_supported = true;
  result.buffer_memory_constraints.ram_domain_supported = true;
  result.buffer_memory_constraints
      .heap_permitted[result.buffer_memory_constraints.heap_permitted_count++] =
      fuchsia::sysmem::HeapType::SYSTEM_RAM;

  if (port == kInputPort) {
    result.image_format_constraints_count = 1;
    fuchsia::sysmem::ImageFormatConstraints& image_constraints = result.image_format_constraints[0];
    image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
    image_constraints.pixel_format.has_format_modifier = true;
    image_constraints.pixel_format.format_modifier.value = fuchsia::sysmem::FORMAT_MODIFIER_LINEAR;

    image_constraints.color_spaces_count = 3;
    image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::REC709;
    // Some sources (camera) currently claim to produce color_space REC601_* so also allow that.
    image_constraints.color_space[1].type = fuchsia::sysmem::ColorSpaceType::REC601_NTSC;
    image_constraints.color_space[2].type = fuchsia::sysmem::ColorSpaceType::REC601_PAL;

    // The non-"required_" fields indicate the decoder's ability to potentially
    // output frames at various dimensions as coded in the stream.  Aside from
    // the current stream being somewhere in these bounds, these have nothing to
    // do with the current stream in particular.
    image_constraints.min_coded_width = 16;
    image_constraints.max_coded_width = 4096;
    image_constraints.min_coded_height = 16;
    // This intentionally isn't the _height_ of a 4096x2176 frame, it's
    // intentionally the _width_ of a 4096x2176 frame assigned to
    // max_coded_height.
    //
    // See max_coded_width_times_coded_height.  We intentionally constrain the
    // max dimension in width or height to the width of a 4096x2176 frame.
    // While the HW might be able to go bigger than that as long as the other
    // dimension is smaller to compensate, we don't really need to enable any
    // larger than 4096x2176's width in either dimension, so we don't.
    image_constraints.max_coded_height = 4096;
    image_constraints.min_bytes_per_row = 16;
    // no hard-coded max stride, at least for now
    image_constraints.max_bytes_per_row = 0xFFFFFFFF;
    image_constraints.max_coded_width_times_coded_height = 4096 * 2176;
    image_constraints.layers = 1;
    image_constraints.coded_width_divisor = 16;
    image_constraints.coded_height_divisor = 16;
    image_constraints.bytes_per_row_divisor = 16;
    // TODO(dustingreen): Since this is a producer that will always produce at
    // offset 0 of a physical page, we don't really care if this field is
    // consistent with any constraints re. what the HW can do.
    image_constraints.start_offset_divisor = 1;
    // Odd display dimensions are permitted, but these don't imply odd NV12
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
    image_constraints.required_min_coded_width = width_;
    image_constraints.required_max_coded_width = width_;
    image_constraints.required_min_coded_height = height_;
    image_constraints.required_max_coded_height = height_;
  } else {
    ZX_DEBUG_ASSERT(result.image_format_constraints_count == 0);
  }

  // We don't have to fill out usage - CodecImpl takes care of that.
  ZX_DEBUG_ASSERT(!result.usage.cpu);
  ZX_DEBUG_ASSERT(!result.usage.display);
  ZX_DEBUG_ASSERT(!result.usage.vulkan);
  ZX_DEBUG_ASSERT(!result.usage.video);

  return result;
}

void CodecAdapterH264::CoreCodecSetBufferCollectionInfo(
    CodecPort port, const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) {
  ZX_DEBUG_ASSERT(buffer_collection_info.settings.buffer_settings.is_physically_contiguous);
  if (port == kInputPort) {
    ZX_DEBUG_ASSERT(buffer_collection_info.settings.has_image_format_constraints);
    ZX_DEBUG_ASSERT(buffer_collection_info.settings.image_format_constraints.pixel_format.type ==
                    fuchsia::sysmem::PixelFormatType::NV12);
  }
}

fuchsia::media::StreamOutputFormat CodecAdapterH264::CoreCodecGetOutputFormat(
    uint64_t stream_lifetime_ordinal, uint64_t new_output_format_details_version_ordinal) {
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

void CodecAdapterH264::CoreCodecMidStreamOutputBufferReConfigPrepare() {}

void CodecAdapterH264::CoreCodecMidStreamOutputBufferReConfigFinish() {
  std::optional<const CodecBuffer*> staged_buffer;
  while ((staged_buffer = staged_buffers_.Pop())) {
    output_sink_->AddOutputBuffer(*staged_buffer);
  }
}

void CodecAdapterH264::CoreCodecSetSecureMemoryMode(
    CodecPort port, fuchsia::mediacodec::SecureMemoryMode secure_memory_mode) {}

void CodecAdapterH264::PostSerial(async_dispatcher_t* dispatcher, fit::closure to_run) {
  zx_status_t post_result = async::PostTask(dispatcher, std::move(to_run));
  ZX_ASSERT_MSG(post_result == ZX_OK, "async::PostTask() failed - result: %d\n", post_result);
}

void CodecAdapterH264::PostToInputProcessingThread(fit::closure to_run) {
  PostSerial(input_processing_loop_.dispatcher(), std::move(to_run));
}

void CodecAdapterH264::QueueInputItem(CodecInputItem input_item) {
  bool is_trigger_needed = false;
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    // For now we don't worry about avoiding a trigger if we happen to queue
    // when ProcessInput() has removed the last item but ProcessInput() is still
    // running.
    if (!is_process_input_queued_) {
      is_trigger_needed = input_queue_.empty();
      is_process_input_queued_ = is_trigger_needed;
    }
    input_queue_.emplace_back(std::move(input_item));
  }  // ~lock
  if (is_trigger_needed) {
    PostToInputProcessingThread(fit::bind_member(this, &CodecAdapterH264::ProcessInput));
  }
}

CodecInputItem CodecAdapterH264::DequeueInputItem() {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    if (is_stream_failed_ || is_cancelling_input_processing_ || input_queue_.empty()) {
      return CodecInputItem::Invalid();
    }
    CodecInputItem to_ret = std::move(input_queue_.front());
    input_queue_.pop_front();
    return to_ret;
  }  // ~lock
}

void CodecAdapterH264::ProcessInput() {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    is_process_input_queued_ = false;
  }  // ~lock

  auto status = device_->EnsureFwLoaded();
  if (status != ZX_OK) {
    events_->onCoreCodecFailCodec("failed to init hw");
    return;
  }

  while (true) {
    CodecInputItem item = DequeueInputItem();

    if (!item.is_valid()) {
      // input loop should exit
      return;
    }

    if (item.is_format_details()) {
      status = device_->UpdateEncoderSettings(item.format_details());
      if (status != ZX_OK) {
        events_->onCoreCodecFailCodec("failed update encoder settings");
        return;
      }

      if (output_sink_->OutputBufferCount() < kOutputMinBufferCountForCamping) {
        // Currently, this will only run if output buffers aren't presently configured.
        // TODO(afoxley) In future, new encode params may imply a need to re-configure
        // output buffers that are already configured but too small or too few in number
        // for higher output bitrate.
        events_->onCoreCodecMidStreamOutputConstraintsChange(
            /*output_re_config_required=*/true);
      }
      continue;
    }

    if (item.is_end_of_stream()) {
      // TODO(afoxley) possibly redundant with Flush at end of function
      output_sink_->Flush();
      events_->onCoreCodecOutputEndOfStream(/*error_detected_before=*/false);
      continue;
    }

    TRACE_DURATION("media", "Media:PacketReceived");

    auto return_packet =
        fit::defer([this, &item] { events_->onCoreCodecInputPacketDone(item.packet()); });

    // errors are logged and handled closer to source in callback
    //
    // TODO(afoxley) We may need to have the hardware encode into a large-ish output buffer then
    // break it up into smaller sub-NAL chunks for sending to output. This means adjusting the
    // output chunk size passed in here.
    (void)output_sink_->NextOutputBlock(
        kOutputBufferMinSizeBytes, std::nullopt,
        [this, &item](OutputSink::OutputBlock output_block) -> OutputSink::OutputResult {
          if (device_->SetInputBuffer(item.packet()->buffer()) != ZX_OK) {
            events_->onCoreCodecFailCodec("Failed to setup input buffer");
            return {.len = 0, .status = OutputSink::kError};
          }

          // input frames are expected to start at buffer offset 0
          if (item.packet()->start_offset()) {
            events_->onCoreCodecFailCodec("Input has offset");
            return {.len = 0, .status = OutputSink::kError};
          }

          device_->SetOutputBuffer(output_block.buffer);
          uint32_t output_len = 0;
          auto status = device_->EncodeFrame(&output_len);

          if (status != ZX_OK) {
            ENCODE_ERROR("Encoding failed: %d", status);
            events_->onCoreCodecFailCodec("Encoding failed: %d", status);
            // TODO(afoxley) soft reset?
            return {.len = 0, .status = OutputSink::kError};
          }

          return {.len = output_len, .status = OutputSink::kSuccess};
        });

    // force one packet per buffer
    output_sink_->Flush();
  }
}

void CodecAdapterH264::OnCoreCodecFailStream(fuchsia::media::StreamError error) {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    is_stream_failed_ = true;
  }
  events_->onCoreCodecFailStream(error);
}

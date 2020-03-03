// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/drivers/amlogic_encoder/codec_adapter_h264.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/media/codec_impl/codec_frame.h>
#include <lib/zx/bti.h>

#include "src/media/drivers/amlogic_encoder/device_ctx.h"
#include "src/media/drivers/amlogic_encoder/macros.h"

constexpr uint32_t kOutputPerPacketBufferBytesMin = 512 * 1024;
// This is an arbitrary cap for now.
constexpr uint32_t kOutputPerPacketBufferBytesMax = 4 * 1024 * 1024;

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
    return true;
  } else {
    ZX_DEBUG_ASSERT(port == kOutputPort);
    return false;
  }
}

bool CodecAdapterH264::IsCoreCodecHwBased(CodecPort port) { return true; }

zx::unowned_bti CodecAdapterH264::CoreCodecBti() { return zx::unowned_bti(device_->bti()); }

void CodecAdapterH264::CoreCodecInit(
    const fuchsia::media::FormatDetails& initial_input_format_details) {
  zx_status_t result = input_processing_loop_.StartThread(
      "CodecAdapterH264::input_processing_thread_", &input_processing_thread_);
  if (result != ZX_OK) {
    events_->onCoreCodecFailCodec(
        "In CodecAdapterH264::CoreCodecInit(), StartThread() failed (input)");
    return;
  }

  initial_input_format_details_ = fidl::Clone(initial_input_format_details);
  latest_input_format_details_ = fidl::Clone(initial_input_format_details);

  result = device_->EncoderInit(initial_input_format_details_);
  if (result != ZX_OK) {
    events_->onCoreCodecFailCodec("In CodecAdapterH264::CoreCodecInit(), EncoderInit failed");
    return;
  }
}

void CodecAdapterH264::CoreCodecStartStream() {
  zx_status_t status;

  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    status = device_->StartEncoder();
    if (status != ZX_OK) {
      events_->onCoreCodecFailCodec("StartStream() failed");
      return;
    }
  }  // ~lock
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
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);

    // This helps any previously-queued ProcessInput() calls return faster, and
    // is checked before calling WaitForParsingCompleted() in case
    // TryStartCancelParsing() does nothing.
    is_cancelling_input_processing_ = true;
  }

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
          events_->onCoreCodecInputPacketDone(std::move(input_item.packet()));
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
  device_->WaitForIdle();
}

void CodecAdapterH264::CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) {
  if (port == kInputPort) {
    const char* kInputBufferName = "H264InputBuffer";
    buffer->vmo().set_property(ZX_PROP_NAME, kInputBufferName, strlen(kInputBufferName));
  } else if (port == kOutputPort) {
    const char* kOutputBufferName = "H264OutputBuffer";
    buffer->vmo().set_property(ZX_PROP_NAME, kOutputBufferName, strlen(kOutputBufferName));
  }
  if (port != kOutputPort) {
    return;
  }
  ZX_DEBUG_ASSERT(port == kOutputPort);
  all_output_buffers_.push_back(buffer);
}

void CodecAdapterH264::CoreCodecConfigureBuffers(
    CodecPort port, const std::vector<std::unique_ptr<CodecPacket>>& packets) {
  if (port != kOutputPort) {
    return;
  }
  ZX_DEBUG_ASSERT(port == kOutputPort);
  // output

  ZX_DEBUG_ASSERT(all_output_packets_.empty());
  ZX_DEBUG_ASSERT(free_output_packets_.empty());
  ZX_DEBUG_ASSERT(!all_output_buffers_.empty());
  ZX_DEBUG_ASSERT(all_output_buffers_.size() == packets.size());
  for (auto& packet : packets) {
    all_output_packets_.push_back(packet.get());
    free_output_packets_.push_back(packet.get()->packet_index());
  }
}

void CodecAdapterH264::CoreCodecRecycleOutputPacket(CodecPacket* packet) {
  if (packet->is_new()) {
    packet->SetIsNew(false);
    return;
  }
  ZX_DEBUG_ASSERT(!packet->is_new());

  // A recycled packet will have a buffer set because the packet is in-flight
  // until put on the free list, and has a buffer associated while in-flight.
  const CodecBuffer* buffer = packet->buffer();
  ZX_DEBUG_ASSERT(buffer);

  // Getting the buffer is all we needed the packet for.  The packet won't get
  // re-used until it goes back on the free list below.
  packet->SetBuffer(nullptr);

  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    free_output_packets_.push_back(packet->packet_index());
  }  // ~lock

  // Recycle can happen while stopped, but this CodecAdapater has no way yet
  // to return frames while stopped, or to re-use buffers/frames across a
  // stream switch.  Any new stream will request allocation of new frames.
  device_->ReturnBuffer(buffer);
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

    // The old all_output_buffers_ are no longer valid.
    all_output_buffers_.clear();
    all_output_packets_.clear();
    free_output_packets_.clear();
  }
  buffer_settings_[port].reset();
}

std::unique_ptr<const fuchsia::media::StreamOutputConstraints>
CodecAdapterH264::CoreCodecBuildNewOutputConstraints(
    uint64_t stream_lifetime_ordinal, uint64_t new_output_buffer_constraints_version_ordinal,
    bool buffer_constraints_action_required) {
  constexpr uint32_t kDefaultPacketCountForClient = 2;

  uint32_t per_packet_buffer_bytes = kOutputPerPacketBufferBytesMax;

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
      .set_packet_count_for_server(min_buffer_count_[kOutputPort])
      .set_packet_count_for_client(kDefaultPacketCountForClient)
      // Packed NV12 (no extra padding, min UV offset, min stride).
      .set_per_packet_buffer_bytes(per_packet_buffer_bytes)
      .set_single_buffer_mode(false);

  // For the moment, let's tell the client to allocate this exact size.
  constraints->set_per_packet_buffer_bytes_min(per_packet_buffer_bytes)
      .set_per_packet_buffer_bytes_recommended(per_packet_buffer_bytes)
      .set_per_packet_buffer_bytes_max(per_packet_buffer_bytes)

      // The hardware only needs min_buffer_count_ buffers - more aren't better.
      .set_packet_count_for_server_min(min_buffer_count_[kOutputPort])
      .set_packet_count_for_server_recommended(min_buffer_count_[kOutputPort])
      .set_packet_count_for_server_recommended_max(min_buffer_count_[kOutputPort])
      .set_packet_count_for_server_max(min_buffer_count_[kOutputPort])
      .set_packet_count_for_client_min(0);

  // Ensure that if the client allocates its max + the server max that it won't go over the hardware
  // limit (max_buffer_count).
  if (max_buffer_count_[kOutputPort] <= min_buffer_count_[kOutputPort]) {
    events_->onCoreCodecFailCodec("Impossible for client to satisfy buffer counts");
    return nullptr;
  }
  constraints->set_packet_count_for_client_max(max_buffer_count_[kOutputPort] -
                                               min_buffer_count_[kOutputPort]);

  // False because it's not required and not encouraged for a video encoder
  // output to allow single buffer mode.
  constraints->set_single_buffer_mode_allowed(false);

  constraints->set_is_physically_contiguous_required(true);
  ::zx::bti very_temp_kludge_bti;
  zx_status_t dup_status =
      ::zx::unowned_bti(device_->bti())->duplicate(ZX_RIGHT_SAME_RIGHTS, &very_temp_kludge_bti);
  if (dup_status != ZX_OK) {
    events_->onCoreCodecFailCodec("BTI duplicate failed - status: %d", dup_status);
    return nullptr;
  }
  // This is very temporary.  The BufferAllocator should handle this directly,
  // not the client.
  constraints->set_very_temp_kludge_bti_handle(std::move(very_temp_kludge_bti));

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

  // The CodecImpl already checked that these are set and that they're
  // consistent with packet count constraints.
  ZX_DEBUG_ASSERT(partial_settings.has_packet_count_for_server());
  ZX_DEBUG_ASSERT(partial_settings.has_packet_count_for_client());

  if (port == kInputPort) {
    // We don't override CoreCodecBuildNewInputConstraints() for now, so pick these up from what was
    // set by default implementation of CoreCodecBuildNewInputConstraints().
    min_buffer_count_[kInputPort] = stream_buffer_constraints.packet_count_for_server_min();
    max_buffer_count_[kInputPort] = stream_buffer_constraints.packet_count_for_server_max();
  }

  ZX_DEBUG_ASSERT(min_buffer_count_[port] != 0);
  ZX_DEBUG_ASSERT(max_buffer_count_[port] != 0);

  result.min_buffer_count_for_camping = min_buffer_count_[port];

  // Some slack is nice overall, but avoid having each participant ask for
  // dedicated slack.  Using sysmem the client will ask for it's own buffers for
  // camping and any slack, so the codec doesn't need to ask for any extra on
  // behalf of the client.
  ZX_DEBUG_ASSERT(result.min_buffer_count_for_dedicated_slack == 0);
  ZX_DEBUG_ASSERT(result.min_buffer_count_for_shared_slack == 0);
  result.max_buffer_count = max_buffer_count_[port];

  uint32_t per_packet_buffer_bytes_min;
  uint32_t per_packet_buffer_bytes_max;
  if (port == kOutputPort) {
    per_packet_buffer_bytes_min = kOutputPerPacketBufferBytesMin;
    per_packet_buffer_bytes_max = kOutputPerPacketBufferBytesMax;
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
    // TODO(MTWN-251): confirm that REC709 is always what we want here, or plumb
    // actual YUV color space if it can ever be REC601_*.  Since 2020 and 2100
    // are minimum 10 bits per Y sample and we're outputting NV12, 601 is the
    // only other potential possibility here.
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::REC709;

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
  buffer_settings_[port].emplace(buffer_collection_info.settings);
}

fuchsia::media::StreamOutputFormat CodecAdapterH264::CoreCodecGetOutputFormat(
    uint64_t stream_lifetime_ordinal, uint64_t new_output_format_details_version_ordinal) {
  fuchsia::media::StreamOutputFormat result;
  result.set_stream_lifetime_ordinal(stream_lifetime_ordinal);
  result.mutable_format_details()->set_format_details_version_ordinal(
      new_output_format_details_version_ordinal);

  result.mutable_format_details()->set_mime_type("video/h264");

  fuchsia::media::VideoFormat video_format;

  result.mutable_format_details()->mutable_domain()->set_video(std::move(video_format));

  return result;
}

void CodecAdapterH264::CoreCodecMidStreamOutputBufferReConfigPrepare() {
  // For this adapter, the core codec just needs us to get new frame buffers
  // set up, so nothing to do here.
  //
  // CoreCodecEnsureBuffersNotConfigured() will run soon.
}

void CodecAdapterH264::CoreCodecMidStreamOutputBufferReConfigFinish() {
  // Now that the client has configured output buffers, hand them to encoder.

  std::vector<const CodecBuffer*> buffers;
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    for (uint32_t i = 0; i < all_output_buffers_.size(); i++) {
      ZX_DEBUG_ASSERT(all_output_buffers_[i]->index() == i);
      ZX_DEBUG_ASSERT(all_output_buffers_[i]->codec_buffer().buffer_index() == i);
      buffers.push_back(all_output_buffers_[i]);
    }
  }  // ~lock

  device_->SetOutputBuffers(std::move(buffers));
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

  while (true) {
    CodecInputItem item = DequeueInputItem();

    if (!item.is_valid()) {
      return;
    }

    if (item.is_format_details()) {
      auto format_details = fidl::Clone(item.format_details());

      // TODO(afoxley) handle setting up new encode params here
      device_->SetEncodeParams(std::move(format_details));
      continue;
    }

    if (item.is_end_of_stream()) {
      events_->onCoreCodecOutputEndOfStream(/*error_detected_before=*/false);
      continue;
    }

    ZX_DEBUG_ASSERT(item.is_packet());

    uint8_t* data = item.packet()->buffer()->base() + item.packet()->start_offset();
    uint32_t len = item.packet()->valid_length_bytes();

    device_->EncodeFrame(item.packet()->buffer(), data, len);

    events_->onCoreCodecInputPacketDone(item.packet());
    // At this point CodecInputItem is holding a packet pointer which may get
    // re-used in a new CodecInputItem, but that's ok since CodecInputItem is
    // going away here.
    //
    // ~item
  }
}

void CodecAdapterH264::OnCoreCodecFailStream(fuchsia::media::StreamError error) {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    is_stream_failed_ = true;
  }
  events_->onCoreCodecFailStream(error);
}

CodecPacket* CodecAdapterH264::GetFreePacket() {
  std::lock_guard<std::mutex> lock(lock_);
  // The h264 decoder won't repeatedly output a buffer multiple times
  // concurrently, so a free buffer (for which the caller needs a packet)
  // implies a free packet.
  ZX_DEBUG_ASSERT(!free_output_packets_.empty());
  uint32_t free_index = free_output_packets_.back();
  free_output_packets_.pop_back();
  return all_output_packets_[free_index];
}

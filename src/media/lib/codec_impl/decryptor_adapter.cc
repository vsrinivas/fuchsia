// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/media/codec_impl/codec_impl.h>
#include <lib/media/codec_impl/codec_port.h>
#include <lib/media/codec_impl/decryptor_adapter.h>
#include <lib/media/codec_impl/log.h>
#include <zircon/assert.h>

#include <algorithm>
#include <cstdint>
#include <limits>

namespace {

constexpr uint64_t kInputBufferConstraintsVersionOrdinal = 1;
constexpr uint64_t kInputDefaultBufferConstraintsVersionOrdinal =
    kInputBufferConstraintsVersionOrdinal;

constexpr uint32_t kInputPacketCountForServerMin = 2;
constexpr uint32_t kInputPacketCountForServerRecommended = 3;
constexpr uint32_t kInputPacketCountForServerRecommendedMax = 16;
constexpr uint32_t kInputPacketCountForServerMax = 64;
constexpr uint32_t kInputDefaultPacketCountForServer = kInputPacketCountForServerRecommended;

constexpr uint32_t kInputPacketCountForClientMin = 2;
constexpr uint32_t kInputPacketCountForClientMax = std::numeric_limits<uint32_t>::max();
constexpr uint32_t kInputDefaultPacketCountForClient = 5;

constexpr bool kInputSingleBufferModeAllowed = false;
constexpr bool kInputDefaultSingleBufferMode = false;

// This is fairly arbitrary, but roughly speaking, ~266 KiB for an average frame
// at 50 Mbps for 4k video, rounded up to 512 KiB buffer space per packet to
// allow most but not all frames to fit in one packet.  It could be equally
// reasonable to say the average-size compressed frame should barely fit in one
// packet's buffer space, or the average-size compressed frame should split to
// ~1.5 packets, but we don't want an excessive number of packets required per
// frame (not even for I frames).
constexpr uint32_t kInputPerPacketBufferBytesMin = 8 * 1024;
constexpr uint32_t kInputPerPacketBufferBytesRecommended = 512 * 1024;
constexpr uint32_t kInputPerPacketBufferBytesMax = 4 * 1024 * 1024;
constexpr uint32_t kInputDefaultPerPacketBufferBytes = kInputPerPacketBufferBytesRecommended;

// TODO(rjascani): For now, just use identical values as input for the output
// constraints. These should likely be tweaked once we have E2E tests to validate
// them.
constexpr uint32_t kOutputPacketCountForServerMin = 2;
constexpr uint32_t kOutputPacketCountForServerRecommended = 3;
constexpr uint32_t kOutputPacketCountForServerRecommendedMax = 16;
constexpr uint32_t kOutputPacketCountForServerMax = 64;
constexpr uint32_t kOutputDefaultPacketCountForServer = kOutputPacketCountForServerRecommended;

constexpr uint32_t kOutputPacketCountForClientMin = 2;
constexpr uint32_t kOutputPacketCountForClientMax = std::numeric_limits<uint32_t>::max();
constexpr uint32_t kOutputDefaultPacketCountForClient = 5;

constexpr bool kOutputSingleBufferModeAllowed = false;
constexpr bool kOutputDefaultSingleBufferMode = false;

constexpr uint32_t kOutputPerPacketBufferBytesMin = 8 * 1024;
constexpr uint32_t kOutputPerPacketBufferBytesRecommended = 512 * 1024;
constexpr uint32_t kOutputPerPacketBufferBytesMax = 4 * 1024 * 1024;
constexpr uint32_t kOutputDefaultPerPacketBufferBytes = kOutputPerPacketBufferBytesRecommended;

}  // namespace

DecryptorAdapter::DecryptorAdapter(std::mutex& lock, CodecAdapterEvents* codec_adapter_events)
    : CodecAdapter(lock, codec_adapter_events),
      input_processing_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
  ZX_DEBUG_ASSERT(codec_adapter_events);
}

bool DecryptorAdapter::IsCoreCodecRequiringOutputConfigForFormatDetection() { return true; }

bool DecryptorAdapter::IsCoreCodecMappedBufferUseful(CodecPort port) {
  // Only require mapped buffers for input and clear output buffers.
  return (port == kInputPort) || (port == kOutputPort && !is_secure());
}

bool DecryptorAdapter::IsCoreCodecHwBased(CodecPort port) { return false; }

void DecryptorAdapter::CoreCodecInit(
    const fuchsia::media::FormatDetails& initial_input_format_details) {
  zx_status_t result = input_processing_loop_.StartThread(
      "DecryptorAdapter::input_processing_thread_", &input_processing_thread_);
  if (result != ZX_OK) {
    events_->onCoreCodecFailCodec(
        "In DecryptorAdapter::CoreCodecInit(), StartThread() failed (input)");
    return;
  }
}

void DecryptorAdapter::CoreCodecSetSecureMemoryMode(
    CodecPort port, fuchsia::mediacodec::SecureMemoryMode secure_memory_mode) {
  if (port == kInputPort) {
    if (secure_memory_mode != fuchsia::mediacodec::SecureMemoryMode::OFF) {
      events_->onCoreCodecFailCodec("Decryptors don't do secure input.");
      return;
    }
    // Ignore OFF for input; that's what the code assumes elsewhere.
  } else {
    ZX_DEBUG_ASSERT(port == kOutputPort);
    if (secure_memory_mode != fuchsia::mediacodec::SecureMemoryMode::OFF &&
        secure_memory_mode != fuchsia::mediacodec::SecureMemoryMode::ON) {
      events_->onCoreCodecFailCodec("Unexpected output SecureMemoryMode (maybe DYNAMIC?)");
    }
    secure_mode_ = (secure_memory_mode == fuchsia::mediacodec::SecureMemoryMode::ON);
  }
}

fuchsia::sysmem::BufferCollectionConstraints
DecryptorAdapter::CoreCodecGetBufferCollectionConstraints(
    CodecPort port, const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
    const fuchsia::media::StreamBufferPartialSettings& partial_settings) {
  fuchsia::sysmem::BufferCollectionConstraints result;

  // Not supporting single buffer mode
  ZX_DEBUG_ASSERT(!partial_settings.has_single_buffer_mode() ||
                  !partial_settings.single_buffer_mode());
  // The CodecImpl won't hand us the sysmem token, so we shouldn't expect to have the token here.
  ZX_DEBUG_ASSERT(!partial_settings.has_sysmem_token());

  ZX_DEBUG_ASSERT(partial_settings.has_packet_count_for_server());
  ZX_DEBUG_ASSERT(partial_settings.has_packet_count_for_client());

  result.min_buffer_count_for_camping = partial_settings.packet_count_for_server();
  // Some slack is nice overall, but avoid having each participant ask for
  // dedicated slack.  Using sysmem the client will ask for it's own buffers for
  // camping and any slack, so the codec doesn't need to ask for any extra on
  // behalf of the client.
  ZX_DEBUG_ASSERT(result.min_buffer_count_for_dedicated_slack == 0);
  ZX_DEBUG_ASSERT(result.min_buffer_count_for_shared_slack == 0);
  ZX_DEBUG_ASSERT(result.max_buffer_count == 0);

  result.has_buffer_memory_constraints = true;

  if (port == kOutputPort && is_secure()) {
    result.buffer_memory_constraints = GetSecureOutputMemoryConstraints();
  } else {
    result.buffer_memory_constraints.physically_contiguous_required = false;
    result.buffer_memory_constraints.secure_required = false;
  }
  result.buffer_memory_constraints.min_size_bytes =
      stream_buffer_constraints.per_packet_buffer_bytes_min();
  result.buffer_memory_constraints.max_size_bytes =
      stream_buffer_constraints.per_packet_buffer_bytes_max();

  ZX_DEBUG_ASSERT(result.image_format_constraints_count == 0);

  // We don't have to fill out usage - CodecImpl takes care of that.
  ZX_DEBUG_ASSERT(!result.usage.cpu);
  ZX_DEBUG_ASSERT(!result.usage.display);
  ZX_DEBUG_ASSERT(!result.usage.vulkan);
  ZX_DEBUG_ASSERT(!result.usage.video);
  ZX_DEBUG_ASSERT(!result.usage.none);

  return result;
}

void DecryptorAdapter::CoreCodecSetBufferCollectionInfo(
    CodecPort port, const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) {
  const auto& buffer_settings = buffer_collection_info.settings.buffer_settings;
  if (port == kInputPort) {
    if (buffer_settings.coherency_domain != fuchsia::sysmem::CoherencyDomain::CPU) {
      events_->onCoreCodecFailCodec("DecryptorAdapter only supports CPU coherent input buffers");
      return;
    }
  } else if (!is_secure()) {  // port == kOutputPort
    if (buffer_settings.coherency_domain != fuchsia::sysmem::CoherencyDomain::CPU) {
      events_->onCoreCodecFailCodec(
          "DecryptorAdapter only supports CPU coherent clear output buffers");
      return;
    }
  } else {  // port == kOutputPort && is_secure()
    if (!buffer_settings.is_secure) {
      events_->onCoreCodecFailCodec("Secure DecryptorAdapter requires secure buffers");
      return;
    }
    if (buffer_settings.coherency_domain != fuchsia::sysmem::CoherencyDomain::INACCESSIBLE) {
      events_->onCoreCodecFailCodec(
          "Secure DecryptorAdapter only supports INACCESSIBLE coherent output buffers");
      return;
    }
  }
}

void DecryptorAdapter::CoreCodecStartStream() {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    is_stream_failed_ = false;
  }  // ~lock
  input_queue_.clear();
  constexpr bool kKeepData = true;
  free_output_packets_.Reset(kKeepData);
  free_output_buffers_.Reset(kKeepData);
}

void DecryptorAdapter::CoreCodecQueueInputFormatDetails(
    const fuchsia::media::FormatDetails& per_stream_override_format_details) {
  QueueInputItem(CodecInputItem::FormatDetails(per_stream_override_format_details));
}

void DecryptorAdapter::CoreCodecQueueInputPacket(CodecPacket* packet) {
  QueueInputItem(CodecInputItem::Packet(packet));
}

void DecryptorAdapter::CoreCodecQueueInputEndOfStream() {
  // This queues a marker, but doesn't force the decryptor to necessarily decrypt all the way up to
  // the marker, depending on whether the client closes the stream or switches to a different stream
  // first - in those cases it's fine for the marker to never show up as output EndOfStream.
  QueueInputItem(CodecInputItem::EndOfStream());
}

void DecryptorAdapter::CoreCodecStopStream() {
  free_output_packets_.StopAllWaits();
  free_output_buffers_.StopAllWaits();

  std::unique_lock<std::mutex> lock(lock_);

  // This helps any previously-queued ProcessInput() calls return faster.
  is_cancelling_input_processing_ = true;
  // TODO(dustingreen): Remove testonly=true from one_shot_event, and use it here.  But keep
  // is_cancelling_input_processing_ since needed to make input loop stop quickly.
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
}

void DecryptorAdapter::CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) {
  if (port == kOutputPort) {
    all_output_buffers_.push_back(buffer);
  }
}

void DecryptorAdapter::CoreCodecConfigureBuffers(
    CodecPort port, const std::vector<std::unique_ptr<CodecPacket>>& packets) {
  if (port != kOutputPort) {
    return;
  }

  ZX_DEBUG_ASSERT(!all_output_buffers_.empty());

  std::vector<CodecPacket*> all_packets;
  for (auto& packet : packets) {
    all_packets.push_back(packet.get());
  }
  std::shuffle(all_packets.begin(), all_packets.end(), not_for_security_prng_);
  for (CodecPacket* packet : all_packets) {
    free_output_packets_.Push(packet);
  }

  for (auto buffer : all_output_buffers_) {
    free_output_buffers_.Push(buffer);
  }
}

void DecryptorAdapter::CoreCodecRecycleOutputPacket(CodecPacket* packet) {
  if (packet->is_new()) {
    packet->SetIsNew(false);
    return;
  }
  ZX_DEBUG_ASSERT(!packet->is_new());

  const CodecBuffer* buffer = packet->buffer();
  packet->SetBuffer(nullptr);

  free_output_packets_.Push(packet);
  free_output_buffers_.Push(buffer);
}

void DecryptorAdapter::CoreCodecEnsureBuffersNotConfigured(CodecPort port) {
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
    free_output_buffers_.Reset();
    free_output_packets_.Reset();
  }
}

std::unique_ptr<const fuchsia::media::StreamBufferConstraints>
DecryptorAdapter::CoreCodecBuildNewInputConstraints() {
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
      .set_single_buffer_mode_allowed(kInputSingleBufferModeAllowed);

  return constraints;
}

std::unique_ptr<const fuchsia::media::StreamOutputConstraints>
DecryptorAdapter::CoreCodecBuildNewOutputConstraints(
    uint64_t stream_lifetime_ordinal, uint64_t new_output_buffer_constraints_version_ordinal,
    bool buffer_constraints_action_required) {
  auto config = std::make_unique<fuchsia::media::StreamOutputConstraints>();

  config->set_stream_lifetime_ordinal(stream_lifetime_ordinal);

  auto* constraints = config->mutable_buffer_constraints();

  // For the moment, there will be only one StreamOutputConstraints, and it'll
  // need output buffers configured for it.
  ZX_DEBUG_ASSERT(buffer_constraints_action_required);
  config->set_buffer_constraints_action_required(buffer_constraints_action_required);
  constraints->set_buffer_constraints_version_ordinal(
      new_output_buffer_constraints_version_ordinal);

  // 0 is intentionally invalid - the client must fill out the buffer_lifetime_ordinal.
  constraints->mutable_default_settings()
      ->set_buffer_lifetime_ordinal(0)
      .set_buffer_constraints_version_ordinal(new_output_buffer_constraints_version_ordinal)
      .set_packet_count_for_server(kOutputDefaultPacketCountForServer)
      .set_packet_count_for_client(kOutputDefaultPacketCountForClient)
      .set_per_packet_buffer_bytes(kOutputDefaultPerPacketBufferBytes)
      .set_single_buffer_mode(kOutputDefaultSingleBufferMode);

  constraints->set_per_packet_buffer_bytes_min(kOutputPerPacketBufferBytesMin)
      .set_per_packet_buffer_bytes_recommended(kOutputPerPacketBufferBytesRecommended)
      .set_per_packet_buffer_bytes_max(kOutputPerPacketBufferBytesMax)
      .set_packet_count_for_server_min(kOutputPacketCountForServerMin)
      .set_packet_count_for_server_recommended(kOutputPacketCountForServerRecommended)
      .set_packet_count_for_server_recommended_max(kOutputPacketCountForServerRecommendedMax)
      .set_packet_count_for_server_max(kOutputPacketCountForServerMax)
      .set_packet_count_for_client_min(kOutputPacketCountForClientMin)
      .set_packet_count_for_client_max(kOutputPacketCountForClientMax)
      .set_single_buffer_mode_allowed(kOutputSingleBufferModeAllowed)
      .set_is_physically_contiguous_required(false);

  return config;
}

fuchsia::media::StreamOutputFormat DecryptorAdapter::CoreCodecGetOutputFormat(
    uint64_t stream_lifetime_ordinal, uint64_t new_output_format_details_version_ordinal) {
  fuchsia::media::StreamOutputFormat result;
  result.set_stream_lifetime_ordinal(stream_lifetime_ordinal);
  result.mutable_format_details()->set_format_details_version_ordinal(
      new_output_format_details_version_ordinal);

  // This sets each of format_details, domain, crypto, decrypted.  So far there aren't any fields in
  // DecryptedFormat.
  result.mutable_format_details()->mutable_domain()->crypto().decrypted();

  return result;
}

void DecryptorAdapter::CoreCodecMidStreamOutputBufferReConfigPrepare() {
  // For this adapter, nothing to do here.
}

void DecryptorAdapter::CoreCodecMidStreamOutputBufferReConfigFinish() {
  // For this adapter, nothing to do here.
}

fuchsia::sysmem::BufferMemoryConstraints DecryptorAdapter::GetSecureOutputMemoryConstraints()
    const {
  fuchsia::sysmem::BufferMemoryConstraints constraints;
  constraints.physically_contiguous_required = true;
  constraints.secure_required = true;
  constraints.ram_domain_supported = false;
  constraints.cpu_domain_supported = false;
  constraints.inaccessible_domain_supported = true;

  constraints.heap_permitted_count = 1;
  constraints.heap_permitted[0] = fuchsia::sysmem::HeapType::SYSTEM_RAM;
  return constraints;
}

void DecryptorAdapter::PostSerial(async_dispatcher_t* dispatcher, fit::closure to_run) {
  zx_status_t post_result = async::PostTask(dispatcher, std::move(to_run));
  ZX_ASSERT_MSG(post_result == ZX_OK, "async::PostTask() failed - result: %d", post_result);
}

void DecryptorAdapter::PostToInputProcessingThread(fit::closure to_run) {
  PostSerial(input_processing_loop_.dispatcher(), std::move(to_run));
}

void DecryptorAdapter::QueueInputItem(CodecInputItem input_item) {
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
    PostToInputProcessingThread(fit::bind_member(this, &DecryptorAdapter::ProcessInput));
  }
}

void DecryptorAdapter::ProcessInput() {
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
      if (!item.format_details().has_domain() || !item.format_details().domain().is_crypto() ||
          !item.format_details().domain().crypto().is_encrypted()) {
        events_->onCoreCodecFailCodec("InputFormatDetails does not include EncryptedFormat");
        return;
      }

      if (!UpdateEncryptionParams(item.format_details().domain().crypto().encrypted())) {
        events_->onCoreCodecFailCodec("Invalid EncryptedFormat");
      }
      continue;
    }

    if (item.is_end_of_stream()) {
      events_->onCoreCodecOutputEndOfStream(false);
      continue;
    }

    ZX_DEBUG_ASSERT(item.is_packet());

    std::optional<CodecPacket*> maybe_output_packet = free_output_packets_.WaitForElement();

    if (!maybe_output_packet) {
      return;
    }
    auto output_packet = *maybe_output_packet;
    ZX_DEBUG_ASSERT(output_packet);

    std::optional<const CodecBuffer*> maybe_output_buffer = free_output_buffers_.WaitForElement();
    if (!maybe_output_buffer) {
      // Return the output_packet to the free list.
      free_output_packets_.Push(output_packet);
      return;
    }
    auto output_buffer = *maybe_output_buffer;
    ZX_DEBUG_ASSERT(output_buffer);

    uint32_t data_length = item.packet()->valid_length_bytes();

    InputBuffer input;
    input.data = item.packet()->buffer()->base() + item.packet()->start_offset();
    input.data_length = data_length;

    OutputBuffer output;
    if (is_secure()) {
      SecureOutputBuffer secure_output;
      secure_output.vmo = zx::unowned_vmo(output_buffer->vmo());
      secure_output.data_offset = output_buffer->vmo_offset();
      secure_output.data_length = output_buffer->size();
      output = secure_output;
    } else if (IsCoreCodecMappedBufferUseful(kOutputPort)) {
      ClearOutputBuffer clear_output;
      clear_output.data = output_buffer->base();
      clear_output.data_length = output_buffer->size();
      output = clear_output;
    } else {
      events_->onCoreCodecFailCodec("Unmapped clear output buffer is unsupported.");
      return;
    }

    auto error = Decrypt(encryption_params_, input, output, output_packet);
    if (error) {
      OnCoreCodecFailStream(*error);
      return;
    }

    output_packet->SetBuffer(output_buffer);
    output_packet->SetStartOffset(0);
    output_packet->SetValidLengthBytes(data_length);
    if (item.packet()->has_timestamp_ish()) {
      output_packet->SetTimstampIsh(item.packet()->timestamp_ish());
    } else {
      output_packet->ClearTimestampIsh();
    }

    events_->onCoreCodecOutputPacket(output_packet, false, false);
    events_->onCoreCodecInputPacketDone(item.packet());
    // At this point CodecInputItem is holding a packet pointer which may get
    // re-used in a new CodecInputItem, but that's ok since CodecInputItem is
    // going away here.
    //
    // ~item
  }
}

bool DecryptorAdapter::UpdateEncryptionParams(
    const fuchsia::media::EncryptedFormat& encrypted_format) {
  if (encrypted_format.has_scheme()) {
    encryption_params_.scheme = encrypted_format.scheme();
  }
  if (encrypted_format.has_key_id()) {
    encryption_params_.key_id = encrypted_format.key_id();
  }
  if (encrypted_format.has_init_vector()) {
    encryption_params_.init_vector = encrypted_format.init_vector();
  }
  if (encrypted_format.has_pattern()) {
    encryption_params_.pattern = encrypted_format.pattern();
  }
  if (encrypted_format.has_subsamples()) {
    encryption_params_.subsamples = encrypted_format.subsamples();
  }

  return true;
}

CodecInputItem DecryptorAdapter::DequeueInputItem() {
  std::lock_guard<std::mutex> lock(lock_);
  if (is_stream_failed_ || is_cancelling_input_processing_ || input_queue_.empty()) {
    return CodecInputItem::Invalid();
  }
  CodecInputItem to_ret = std::move(input_queue_.front());
  input_queue_.pop_front();
  return to_ret;
}

void DecryptorAdapter::OnCoreCodecFailStream(fuchsia::media::StreamError error) {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    is_stream_failed_ = true;
  }
  events_->onCoreCodecFailStream(error);
}

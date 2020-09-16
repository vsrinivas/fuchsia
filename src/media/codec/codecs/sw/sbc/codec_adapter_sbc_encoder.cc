// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_adapter_sbc_encoder.h"

#include <lib/media/codec_impl/codec_buffer.h>
#include <lib/trace/event.h>

namespace {

// A client using the min shouldn't necessarily expect performance to be
// acceptable when running higher bit-rates.
constexpr uint32_t kInputPerPacketBufferBytesMin = SBC_MAX_PCM_BUFFER_SIZE;
// This is an arbitrary cap for now.
constexpr uint32_t kInputPerPacketBufferBytesMax = 4 * 1024 * 1024;

constexpr char kSbcMimeType[] = "audio/sbc";

}  // namespace

CodecAdapterSbcEncoder::CodecAdapterSbcEncoder(std::mutex& lock,
                                               CodecAdapterEvents* codec_adapter_events)
    : CodecAdapterSW(lock, codec_adapter_events) {}

CodecAdapterSbcEncoder::~CodecAdapterSbcEncoder() = default;

void CodecAdapterSbcEncoder::ProcessInputLoop() {
  std::optional<CodecInputItem> maybe_input_item;
  while ((maybe_input_item = input_queue_.WaitForElement())) {
    CodecInputItem input_item = std::move(maybe_input_item.value());
    if (input_item.is_format_details()) {
      if (context_) {
        events_->onCoreCodecFailCodec("Midstream input format change is not supported.");
        return;
      }

      if (CreateContext(std::move(input_item.format_details())) != kOk) {
        // Creation failed; a failure was reported through `events_`.
        return;
      }

      events_->onCoreCodecMidStreamOutputConstraintsChange(
          /*output_re_config_required=*/true);
    } else if (input_item.is_end_of_stream()) {
      ZX_DEBUG_ASSERT(context_);
      if (EncodeInput(nullptr) == kShouldTerminate) {
        // A failure was reported through `events_` or the stream was stopped.
        return;
      }
      events_->onCoreCodecOutputEndOfStream(/*error_detected_before=*/false);
    } else if (input_item.is_packet()) {
      ZX_DEBUG_ASSERT(context_);

      if (EncodeInput(input_item.packet()) == kShouldTerminate) {
        // A failure was reported through `events_` or the stream was stopped.
        return;
      }
    }
  }
}

void CodecAdapterSbcEncoder::CleanUpAfterStream() { context_ = std::nullopt; }

std::pair<fuchsia::media::FormatDetails, size_t> CodecAdapterSbcEncoder::OutputFormatDetails() {
  FX_DCHECK(context_);
  fuchsia::media::AudioCompressedFormatSbc sbc;
  fuchsia::media::AudioCompressedFormat compressed_format;
  compressed_format.set_sbc(std::move(sbc));

  fuchsia::media::AudioFormat audio_format;
  audio_format.set_compressed(std::move(compressed_format));

  fuchsia::media::FormatDetails format_details;
  format_details.set_mime_type(kSbcMimeType);
  format_details.mutable_domain()->set_audio(std::move(audio_format));

  return {std::move(format_details), context_->sbc_frame_length()};
}

fuchsia::sysmem::BufferCollectionConstraints
CodecAdapterSbcEncoder::CoreCodecGetBufferCollectionConstraints(
    CodecPort port, const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
    const fuchsia::media::StreamBufferPartialSettings& partial_settings) {
  std::lock_guard<std::mutex> lock(lock_);

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

  ZX_DEBUG_ASSERT(partial_settings.has_packet_count_for_server());
  ZX_DEBUG_ASSERT(partial_settings.has_packet_count_for_client());
  uint32_t packet_count =
      partial_settings.packet_count_for_server() + partial_settings.packet_count_for_client();

  // For now this is true - when we plumb more flexible buffer count range this
  // will change to account for a range.
  ZX_DEBUG_ASSERT(port != kOutputPort ||
                  packet_count >= kMinOutputPacketCount && packet_count <= kMaxOutputPacketCount);

  // TODO(MTWN-250): plumb/permit range of buffer count from further down,
  // instead of single number frame_count, and set this to the actual
  // stream-required # of reference frames + # that can concurrently decode.
  // Packets and buffers are not the same thing, and we should permit the # of
  // packets to be >= the # of buffers.  We shouldn't be
  // allocating buffers on behalf of the client here, but until we plumb the
  // range of frame_count and are more flexible on # of allocated buffers, we
  // have to make sure there are at least as many buffers as packets.  We
  // categorize the buffers as for camping and for slack.  This should change to
  // be just the buffers needed for camping and maybe 1 for shared slack.  If
  // the client wants more buffers the client can demand buffers in its own
  // fuchsia::sysmem::BufferCollection::SetConstraints().
  result.min_buffer_count_for_camping = partial_settings.packet_count_for_server();
  ZX_DEBUG_ASSERT(result.min_buffer_count_for_dedicated_slack == 0);
  ZX_DEBUG_ASSERT(result.min_buffer_count_for_shared_slack == 0);

  uint32_t per_packet_buffer_bytes_min;
  uint32_t per_packet_buffer_bytes_max;
  if (port == kInputPort) {
    per_packet_buffer_bytes_min = kInputPerPacketBufferBytesMin;
    per_packet_buffer_bytes_max = kInputPerPacketBufferBytesMax;
  } else {
    ZX_ASSERT(context_.has_value());

    ZX_DEBUG_ASSERT(port == kOutputPort);
    per_packet_buffer_bytes_min = context_->sbc_frame_length();
    // At least for now, don't cap the per-packet buffer size for output.
    per_packet_buffer_bytes_max = 0xFFFFFFFF;
  }

  result.has_buffer_memory_constraints = true;
  result.buffer_memory_constraints.min_size_bytes = per_packet_buffer_bytes_min;
  result.buffer_memory_constraints.max_size_bytes = per_packet_buffer_bytes_max;

  // These are all false because SW encode.
  result.buffer_memory_constraints.physically_contiguous_required = false;
  result.buffer_memory_constraints.secure_required = false;

  ZX_DEBUG_ASSERT(result.image_format_constraints_count == 0);

  // We don't have to fill out usage - CodecImpl takes care of that.
  ZX_DEBUG_ASSERT(!result.usage.cpu);
  ZX_DEBUG_ASSERT(!result.usage.display);
  ZX_DEBUG_ASSERT(!result.usage.vulkan);
  ZX_DEBUG_ASSERT(!result.usage.video);

  return result;
}

void CodecAdapterSbcEncoder::CoreCodecStopStream() {
  async::PostTask(input_processing_loop_.dispatcher(), [this] {
    if (output_buffer_) {
      // If we have an output buffer pending but not sent, return it to the pool. CodecAdapterSW
      // expects all buffers returned after stream is stopped.
      auto base = output_buffer_->base();
      output_buffer_pool_.FreeBuffer(base);
      output_buffer_ = nullptr;
    }
  });

  CodecAdapterSW::CoreCodecStopStream();
}

CodecAdapterSbcEncoder::InputLoopStatus CodecAdapterSbcEncoder::CreateContext(
    const fuchsia::media::FormatDetails& format_details) {
  if (!format_details.has_domain() || !format_details.domain().is_audio() ||
      !format_details.domain().audio().is_uncompressed() ||
      !format_details.domain().audio().uncompressed().is_pcm()) {
    events_->onCoreCodecFailCodec(
        "SBC Encoder received input that was not uncompressed pcm audio.");
    return kShouldTerminate;
  }
  auto& input_format = format_details.domain().audio().uncompressed().pcm();

  if (input_format.bits_per_sample != 16) {
    events_->onCoreCodecFailCodec(
        "SBC Encoder only encodes audio with signed 16 bit little endian "
        "linear samples.");
    return kShouldTerminate;
  }

  int16_t sampling_freq;
  switch (input_format.frames_per_second) {
    case 48000:
      sampling_freq = SBC_sf48000;
      break;
    case 44100:
      sampling_freq = SBC_sf44100;
      break;
    case 32000:
      sampling_freq = SBC_sf32000;
      break;
    case 16000:
      sampling_freq = SBC_sf16000;
      break;
    default:
      events_->onCoreCodecFailCodec("SBC Encoder received input with unsupported frequency.");
      return kShouldTerminate;
  }

  if (!format_details.has_encoder_settings() || !format_details.encoder_settings().is_sbc()) {
    events_->onCoreCodecFailCodec("SBC Encoder received input without encoder settings.");
    return kShouldTerminate;
  }
  auto& settings = format_details.encoder_settings().sbc();

  if (settings.channel_mode == fuchsia::media::SbcChannelMode::MONO &&
      input_format.channel_map.size() != 1) {
    events_->onCoreCodecFailCodec(
        "SBC Encoder received request for MONO encoding, but input does "
        "not have exactly 1 channel.");
    return kShouldTerminate;
  }

  if (settings.channel_mode != fuchsia::media::SbcChannelMode::MONO &&
      input_format.channel_map.size() != 2) {
    events_->onCoreCodecFailCodec(
        "SBC Encoder received request for DUAL, STEREO, or JOINT_STEREO "
        "encoding, but input does not have exactly 2 channels.");
    return kShouldTerminate;
  }

  SBC_ENC_PARAMS params = {};
  params.s16SamplingFreq = sampling_freq;
  params.s16ChannelMode = static_cast<int16_t>(settings.channel_mode);
  params.s16NumOfSubBands = static_cast<int16_t>(settings.sub_bands);
  params.s16NumOfBlocks = static_cast<int16_t>(settings.block_count);
  params.s16AllocationMethod = static_cast<int16_t>(settings.allocation);
  SBC_Encoder_Init(&params);

  // The encoder will suggest a value for the bitpool, but since the client
  // provides that we ignore the suggestion and set it after
  // SBC_Encoder_Init.
  params.s16BitPool = settings.bit_pool;

  const uint64_t bytes_per_second =
      input_format.frames_per_second * sizeof(uint16_t) * input_format.channel_map.size();
  context_ = {
      {.settings = std::move(settings), .input_format = std::move(input_format), .params = params}};
  chunk_input_stream_.emplace(
      context_->pcm_batch_size(),
      format_details.has_timebase()
          ? TimestampExtrapolator(format_details.timebase(), bytes_per_second)
          : TimestampExtrapolator(),
      [this](const ChunkInputStream::InputBlock input_block) {
        if (input_block.non_padding_len == 0) {
          return ChunkInputStream::kContinue;
        }

        if (output_packet_ == nullptr) {
          std::optional<CodecPacket*> maybe_output_packet = free_output_packets_.WaitForElement();
          if (!maybe_output_packet) {
            // The stream is ending.
            return ChunkInputStream::kTerminate;
          }
          FX_DCHECK(*maybe_output_packet != nullptr);

          output_packet_ = *maybe_output_packet;
          if (input_block.timestamp_ish) {
            output_packet_->SetTimstampIsh(*input_block.timestamp_ish);
          }
        }

        uint8_t* output = NextOutputBlock();
        if (output == nullptr) {
          // The stream is ending.
          return ChunkInputStream::kTerminate;
        }
        FX_DCHECK(output_buffer_);

        SBC_Encode(&context_->params,
                   reinterpret_cast<int16_t*>(const_cast<uint8_t*>(input_block.data)), output);

        if (output_offset_ + context_->sbc_frame_length() > output_buffer_->size() ||
            input_block.is_end_of_stream) {
          FX_DCHECK(output_packet_ != nullptr);

          output_packet_->SetBuffer(output_buffer_);
          output_packet_->SetValidLengthBytes(output_offset_);
          output_packet_->SetStartOffset(0);

          SendOutputPacket(output_packet_);
          output_packet_ = nullptr;
          output_buffer_ = nullptr;
          output_offset_ = 0;
        }

        return ChunkInputStream::kContinue;
      });

  return kOk;
}

// TODO(turnage): Store progress on an output buffer so it can be used across
//                multiple input packets if we're behind.
CodecAdapterSbcEncoder::InputLoopStatus CodecAdapterSbcEncoder::EncodeInput(
    CodecPacket* input_packet) {
  FX_DCHECK(context_);
  FX_DCHECK(chunk_input_stream_);

  auto return_to_client = fit::defer([this, input_packet]() {
    if (input_packet) {
      events_->onCoreCodecInputPacketDone(input_packet);
    }
  });

  ChunkInputStream::Status status;
  if (input_packet == nullptr) {
    status = chunk_input_stream_->Flush();
  } else {
    status = chunk_input_stream_->ProcessInputPacket(input_packet);
  }

  switch (status) {
    case ChunkInputStream::kExtrapolationFailedWithoutTimebase:
      events_->onCoreCodecFailCodec(
          "Extrapolation was required for a timestamp because the input "
          "was unaligned, but no timebase is set.");
    case ChunkInputStream::kUserTerminated:
      return kShouldTerminate;
    default:
      return kOk;
  };
}

void CodecAdapterSbcEncoder::SendOutputPacket(CodecPacket* output_packet) {
  {
    TRACE_INSTANT("codec_runner", "Media:PacketSent", TRACE_SCOPE_THREAD);

    fit::closure free_buffer = [this, base = output_packet->buffer()->base()] {
      output_buffer_pool_.FreeBuffer(base);
    };
    std::lock_guard<std::mutex> lock(lock_);
    in_use_by_client_[output_packet] = fit::defer(std::move(free_buffer));
  }

  events_->onCoreCodecOutputPacket(output_packet,
                                   /*error_detected_before=*/false,
                                   /*error_detected_during=*/false);
}

uint8_t* CodecAdapterSbcEncoder::NextOutputBlock() {
  if (!output_buffer_) {
    output_buffer_ = output_buffer_pool_.AllocateBuffer();
    output_offset_ = 0;
  }

  if (!output_buffer_) {
    return nullptr;
  }

  // We assume sysmem has enforced our minimum requested buffer size of at
  // least one sbc frame length.
  FX_DCHECK(output_buffer_->size() >= context_->sbc_frame_length());

  // Caller must set `output_buffer` to `nullptr` when space is insufficient.
  uint8_t* output = output_buffer_->base() + output_offset_;
  output_offset_ += context_->sbc_frame_length();
  return output;
}

void CodecAdapterSbcEncoder::CoreCodecSetBufferCollectionInfo(
    CodecPort port, const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) {
  if (port == kInputPort) {
    ZX_DEBUG_ASSERT(buffer_collection_info.buffer_count >= kMinInputBufferCountForCamping);
  } else {
    ZX_DEBUG_ASSERT(buffer_collection_info.buffer_count >= kMinOutputBufferCountForCamping);
  }
}

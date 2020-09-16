// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_adapter_sbc_decoder.h"

#include <lib/media/codec_impl/codec_buffer.h>
#include <lib/trace/event.h>

#include <iomanip>

#include "fuchsia/media/cpp/fidl.h"
#include "oi_codec_sbc.h"

namespace {

constexpr char kSbcMimeType[] = "audio/sbc";
constexpr char kPcmMimeType[] = "audio/pcm";
constexpr uint8_t kPcmBitsPerSample = 16;
constexpr size_t kMaxInputFrames = 64;

}  // namespace

CodecAdapterSbcDecoder::CodecAdapterSbcDecoder(std::mutex& lock,
                                               CodecAdapterEvents* codec_adapter_events)
    : CodecAdapterSW(lock, codec_adapter_events) {}

CodecAdapterSbcDecoder::~CodecAdapterSbcDecoder() = default;

void CodecAdapterSbcDecoder::ProcessInputLoop() {
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

      if (DecodeInput(nullptr) == kShouldTerminate) {
        events_->onCoreCodecFailCodec("Failed to stop stream");
        return;
      }
    } else if (input_item.is_packet()) {
      ZX_DEBUG_ASSERT(context_);

      if (DecodeInput(input_item.packet()) == kShouldTerminate) {
        events_->onCoreCodecFailCodec("Failed to decode packet");
        return;
      }
    }
  }
}

void CodecAdapterSbcDecoder::CleanUpAfterStream() { context_ = std::nullopt; }

std::pair<fuchsia::media::FormatDetails, size_t> CodecAdapterSbcDecoder::OutputFormatDetails() {
  FX_DCHECK(context_);

  fuchsia::media::AudioUncompressedFormat uncompressed;
  uncompressed.set_pcm(context_->output_format);

  fuchsia::media::AudioFormat audio_format;
  audio_format.set_uncompressed(std::move(uncompressed));

  fuchsia::media::FormatDetails format_details;
  format_details.set_mime_type(kPcmMimeType);
  format_details.mutable_domain()->set_audio(std::move(audio_format));

  return {std::move(format_details), context_->max_pcm_chunk_size()};
}

fuchsia::sysmem::BufferCollectionConstraints
CodecAdapterSbcDecoder::CoreCodecGetBufferCollectionConstraints(
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
    per_packet_buffer_bytes_min = kMaxInputFrames * SBC_MAX_FRAME_LEN;
    per_packet_buffer_bytes_max = kMaxInputFrames * SBC_MAX_FRAME_LEN;
  } else {
    ZX_ASSERT(context_.has_value());

    ZX_DEBUG_ASSERT(port == kOutputPort);
    per_packet_buffer_bytes_min = context_->max_pcm_chunk_size();
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

void CodecAdapterSbcDecoder::CoreCodecStopStream() {
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

fuchsia::media::PcmFormat CodecAdapterSbcDecoder::DecodeCodecInfo(
    const std::vector<uint8_t>& oob_bytes) {
  fuchsia::media::PcmFormat out;
  SbcCodecInfo codec_info;

  FX_DCHECK(oob_bytes.size() == sizeof(SbcCodecInfo));

  // SBC codec info assumed to be msbf order
  memcpy(&codec_info, oob_bytes.data(), sizeof(SbcCodecInfo));

  out.bits_per_sample = kPcmBitsPerSample;

  switch (codec_info.channel_mode) {
    case kSbcChannelModeMono:
      out.channel_map = {fuchsia::media::AudioChannelId::LF};
      break;
    default:
      out.channel_map = {fuchsia::media::AudioChannelId::LF, fuchsia::media::AudioChannelId::RF};
      break;
  }

  switch (codec_info.sampling_frequency) {
    case kSbcSamplingFrequency16000Hz:
      out.frames_per_second = 16000;
      break;
    case kSbcSamplingFrequency32000Hz:
      out.frames_per_second = 32000;
      break;
    case kSbcSamplingFrequency44100Hz:
      out.frames_per_second = 44100;
      break;
    case kSbcSamplingFrequency48000Hz:
      out.frames_per_second = 48000;
      break;
    default:
      FX_LOGS(WARNING) << "invalid frequency";
      break;
  }

  return out;
}

CodecAdapterSbcDecoder::InputLoopStatus CodecAdapterSbcDecoder::CreateContext(
    const fuchsia::media::FormatDetails& format_details) {
  if (!format_details.has_mime_type() || format_details.mime_type() != kSbcMimeType ||
      !format_details.has_oob_bytes() ||
      format_details.oob_bytes().size() != sizeof(SbcCodecInfo)) {
    events_->onCoreCodecFailCodec("SBC Decoder received input that was not compressed sbc audio.");
    return kShouldTerminate;
  }

  auto output_pcm_format = DecodeCodecInfo(format_details.oob_bytes());

  context_ = {.output_format = output_pcm_format};
  OI_STATUS status = OI_CODEC_SBC_DecoderReset(
      &context_->context, context_->context_data, sizeof(context_->context_data), SBC_MAX_CHANNELS,
      /*pcmStride=*/kPcmBitsPerSample / 8, /*enhanced=*/false);
  if (!OI_SUCCESS(status)) {
    events_->onCoreCodecFailCodec("Failed to reset SBC decoder");
    return kShouldTerminate;
  }

  return kOk;
}

CodecAdapterSbcDecoder::InputLoopStatus CodecAdapterSbcDecoder::DecodeInput(
    CodecPacket* input_packet) {
  FX_DCHECK(context_);

  if (!input_packet) {
    events_->onCoreCodecOutputEndOfStream(/*error_detected_before=*/false);
    return kOk;
  }

  auto return_to_client =
      fit::defer([this, input_packet]() { events_->onCoreCodecInputPacketDone(input_packet); });

  uint32_t bytes_left = input_packet->valid_length_bytes();
  uint8_t* input_data = input_packet->buffer()->base() + input_packet->start_offset();

  while (bytes_left > 0) {
    uint8_t* output = CurrentOutputBlock();

    if (output == nullptr) {
      // The stream is ending.
      return kShouldTerminate;
    }

    FX_DCHECK(output_buffer_);
    FX_DCHECK(output_packet_);

    uint32_t output_bytes = output_buffer_->size() - output_offset_;

    OI_STATUS status =
        OI_CODEC_SBC_DecodeFrame(&context_->context, const_cast<const OI_BYTE**>(&input_data),
                                 &bytes_left, reinterpret_cast<int16_t*>(output), &output_bytes);
    if (!OI_SUCCESS(status)) {
      FX_LOGS(WARNING) << "decode failure " << status;
      break;
    }

    QueueAndSend(output_bytes);
  }

  SendQueuedOutput();

  return kOk;
}

uint8_t* CodecAdapterSbcDecoder::CurrentOutputBlock() {
  if (output_packet_ == nullptr) {
    std::optional<CodecPacket*> maybe_output_packet = free_output_packets_.WaitForElement();
    if (!maybe_output_packet) {
      // The stream is ending.
      return nullptr;
    }
    FX_DCHECK(*maybe_output_packet != nullptr);
    output_packet_ = *maybe_output_packet;
  }

  if (!output_buffer_) {
    output_buffer_ = output_buffer_pool_.AllocateBuffer();
    output_offset_ = 0;
  }

  if (!output_buffer_) {
    return nullptr;
  }

  FX_DCHECK(output_offset_ < output_buffer_->size());

  // Caller must set `output_buffer` to `nullptr` when space is insufficient.
  uint8_t* output = output_buffer_->base() + output_offset_;
  return output;
}

void CodecAdapterSbcDecoder::SendQueuedOutput() {
  if (!output_buffer_ || !output_packet_ || !output_offset_) {
    return;
  }

  TRACE_INSTANT("codec_runner", "Media:PacketSent", TRACE_SCOPE_THREAD);

  output_packet_->SetBuffer(output_buffer_);
  output_packet_->SetValidLengthBytes(output_offset_);
  output_packet_->SetStartOffset(0);

  {
    fit::closure free_buffer = [this, base = output_packet_->buffer()->base()] {
      output_buffer_pool_.FreeBuffer(base);
    };
    std::lock_guard<std::mutex> lock(lock_);
    in_use_by_client_[output_packet_] = fit::defer(std::move(free_buffer));
  }

  events_->onCoreCodecOutputPacket(output_packet_,
                                   /*error_detected_before=*/false,
                                   /*error_detected_during=*/false);
  output_packet_ = nullptr;
  output_buffer_ = nullptr;
  output_offset_ = 0;
}

void CodecAdapterSbcDecoder::QueueAndSend(size_t bytes_read) {
  FX_DCHECK(output_offset_ + bytes_read <= output_buffer_->size());

  output_offset_ += bytes_read;

  if (output_offset_ == output_buffer_->size()) {
    SendQueuedOutput();
  }
}

void CodecAdapterSbcDecoder::CoreCodecSetBufferCollectionInfo(
    CodecPort port, const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) {
  if (port == kInputPort) {
    ZX_DEBUG_ASSERT(buffer_collection_info.buffer_count >= kMinInputBufferCountForCamping);
  } else {
    ZX_DEBUG_ASSERT(buffer_collection_info.buffer_count >= kMinOutputBufferCountForCamping);
  }
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_adapter_aac_encoder.h"

#include <lib/async/cpp/task.h>
#include <lib/trace/event.h>

#include "chunk_input_stream.h"
#include "output_sink.h"

namespace {

constexpr char kAacMimeType[] = "audio/aac";

void PostTask(async_dispatcher_t* dispatcher, fit::closure task) {
  auto result = async::PostTask(dispatcher, std::move(task));
  ZX_ASSERT_MSG(result == ZX_OK, "Failed to post to dispatcher: %d", result);
}

}  // namespace

CodecAdapterAacEncoder::CodecAdapterAacEncoder(std::mutex& lock,
                                               CodecAdapterEvents* codec_adapter_events)
    : CodecAdapter(lock, codec_adapter_events),
      input_processing_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

CodecAdapterAacEncoder::~CodecAdapterAacEncoder() {}

bool CodecAdapterAacEncoder::IsCoreCodecRequiringOutputConfigForFormatDetection() { return false; }

bool CodecAdapterAacEncoder::IsCoreCodecMappedBufferUseful(CodecPort port) { return true; }

bool CodecAdapterAacEncoder::IsCoreCodecHwBased(CodecPort port) { return false; }

void CodecAdapterAacEncoder::CoreCodecInit(
    const fuchsia::media::FormatDetails& initial_input_format_details) {
  ZX_DEBUG_ASSERT(!output_sink_);

  thrd_t input_processing_thread;
  zx_status_t result = input_processing_loop_.StartThread(nullptr, &input_processing_thread);
  if (result != ZX_OK) {
    events_->onCoreCodecFailCodec(
        "CodecCodecInit(): Failed to start input processing thread with "
        "zx_status_t: %d",
        result);
    return;
  }

  output_sink_.emplace(/*sender=*/
                       [this](CodecPacket* output_packet) {
                         TRACE_INSTANT("codec_runner", "Media:PacketSent", TRACE_SCOPE_THREAD);
                         events_->onCoreCodecOutputPacket(output_packet,
                                                          /*error_detected_before=*/false,
                                                          /*error_detected_during=*/false);
                         return OutputSink::kSuccess;
                       },
                       /*writer_thread=*/input_processing_thread);
}

fuchsia::sysmem::BufferCollectionConstraints
CodecAdapterAacEncoder::CoreCodecGetBufferCollectionConstraints(
    CodecPort port, const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
    const fuchsia::media::StreamBufferPartialSettings& partial_settings) {
  auto constraints = fuchsia::sysmem::BufferCollectionConstraints{
      .min_buffer_count_for_camping = partial_settings.packet_count_for_server(),
      .has_buffer_memory_constraints = true,
  };

  if (port == kOutputPort) {
    std::lock_guard<std::mutex> lock(lock_);
    ZX_DEBUG_ASSERT_MSG(format_configuration_,
                        "The input thread triggered this call to generate "
                        "buffer constraints, so "
                        "it should have prepared the format configuration.");
    constraints.buffer_memory_constraints.min_size_bytes =
        static_cast<uint32_t>(format_configuration_->recommended_output_buffer_size);
  } else {
    // TODO(turnage): Allow codec adapters to specify that input format details
    // are required before buffer collection constraints can be provided, so
    // that a stream-specific recommendation can be made here.
    constraints.buffer_memory_constraints.min_size_bytes = 2048;
  }

  return constraints;
}

void CodecAdapterAacEncoder::CoreCodecSetBufferCollectionInfo(
    CodecPort port, const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) {
  // Nothing to do here.
}

void CodecAdapterAacEncoder::CoreCodecStartStream() {
  output_sink_->Reset(/*keep_data=*/true);
  {
    std::lock_guard<std::mutex> lock(lock_);
    stream_active_ = true;
  }

  TRACE_INSTANT("codec_runner", "Media:Start", TRACE_SCOPE_THREAD);
}

void CodecAdapterAacEncoder::CoreCodecQueueInputFormatDetails(
    const fuchsia::media::FormatDetails& per_stream_override_format_details) {
  PostTask(input_processing_loop_.dispatcher(),
           // We clone in case the reference does not live long enough.
           [this, format_details = fidl::Clone(per_stream_override_format_details)]() {
             ProcessInput(CodecInputItem::FormatDetails(format_details));
           });
}

void CodecAdapterAacEncoder::CoreCodecQueueInputPacket(CodecPacket* packet) {
  TRACE_INSTANT("codec_runner", "Media:PacketReceived", TRACE_SCOPE_THREAD);
  PostTask(input_processing_loop_.dispatcher(),
           [this, packet]() { ProcessInput(CodecInputItem::Packet(packet)); });
}

void CodecAdapterAacEncoder::CoreCodecQueueInputEndOfStream() {
  PostTask(input_processing_loop_.dispatcher(),
           [this]() { ProcessInput(CodecInputItem::EndOfStream()); });
}

void CodecAdapterAacEncoder::CoreCodecStopStream() {
  ZX_DEBUG_ASSERT(output_sink_);

  {
    std::lock_guard<std::mutex> lock(lock_);
    stream_active_ = false;
  }
  output_sink_->StopAllWaits();

  // TODO(turnage): Replace with OneShotEvent when it is in-tree.
  zx::event stream_stopped;
  zx_status_t status = zx::event::create(0, &stream_stopped);
  ZX_DEBUG_ASSERT_MSG(status == ZX_OK, "Failed to create event object: %d", status);

  PostTask(input_processing_loop_.dispatcher(), [this, &stream_stopped]() {
    stream_ = std::nullopt;
    stream_stopped.signal(ZX_EVENT_SIGNAL_MASK, ZX_EVENT_SIGNALED);
  });

  stream_stopped.wait_one(ZX_EVENT_SIGNALED, zx::time::infinite(), nullptr);

  TRACE_INSTANT("codec_runner", "Media:Stop", TRACE_SCOPE_THREAD);
}

void CodecAdapterAacEncoder::CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) {
  ZX_DEBUG_ASSERT(output_sink_);

  if (port != kOutputPort) {
    return;
  }

  staged_buffers_.Push(std::move(buffer));
}

void CodecAdapterAacEncoder::CoreCodecConfigureBuffers(
    CodecPort port, const std::vector<std::unique_ptr<CodecPacket>>& packets) {
  // Nothing to do here.
}

void CodecAdapterAacEncoder::CoreCodecRecycleOutputPacket(CodecPacket* packet) {
  ZX_DEBUG_ASSERT(output_sink_);

  output_sink_->AddOutputPacket(packet);
}

void CodecAdapterAacEncoder::CoreCodecEnsureBuffersNotConfigured(CodecPort port) {
  ZX_DEBUG_ASSERT(output_sink_);

  output_sink_->Reset();
}

std::unique_ptr<const fuchsia::media::StreamOutputConstraints>
CodecAdapterAacEncoder::CoreCodecBuildNewOutputConstraints(
    uint64_t stream_lifetime_ordinal, uint64_t new_output_buffer_constraints_version_ordinal,
    bool buffer_constraints_action_required) {
  ZX_DEBUG_ASSERT(output_sink_);
  // Immediately call a lambda so we can have a const on output_buffer_size.
  const uint32_t output_buffer_size = [this] {
    std::lock_guard<std::mutex> lock(lock_);
    ZX_DEBUG_ASSERT_MSG(format_configuration_,
                        "The input thread triggered this call to generate output constraints, so "
                        "it should have prepared the format configuration.");
    return format_configuration_->recommended_output_buffer_size;
  }();

  constexpr size_t kServerPacketCount = 1;
  constexpr size_t kClientPacketCount = 1;

  // These ceilings are arbitrary, but prevent the client from using this codec
  // to request unbounded memory from sysmem.
  constexpr size_t kMaxPacketCount = 100;
  const size_t kMaxBufferSize = output_buffer_size * 10;

  auto constraints = std::make_unique<fuchsia::media::StreamOutputConstraints>();

  constraints->set_stream_lifetime_ordinal(stream_lifetime_ordinal)
      .set_buffer_constraints_action_required(buffer_constraints_action_required);

  auto* buffer_constraints = constraints->mutable_buffer_constraints();
  buffer_constraints->mutable_default_settings()
      ->set_packet_count_for_server(kServerPacketCount)
      .set_per_packet_buffer_bytes(output_buffer_size)
      .set_packet_count_for_client(kClientPacketCount)
      // 0 is invalid to force the client to set this field.
      .set_buffer_lifetime_ordinal(0)
      .set_buffer_constraints_version_ordinal(new_output_buffer_constraints_version_ordinal);

  buffer_constraints->set_per_packet_buffer_bytes_min(output_buffer_size)
      .set_per_packet_buffer_bytes_recommended(output_buffer_size)
      .set_per_packet_buffer_bytes_max(kMaxBufferSize)
      .set_packet_count_for_server_min(1)
      .set_packet_count_for_server_recommended(kServerPacketCount)
      .set_packet_count_for_server_recommended_max(kServerPacketCount)
      .set_packet_count_for_server_max(kMaxPacketCount)
      .set_packet_count_for_client_min(1)
      .set_packet_count_for_client_max(kMaxPacketCount)
      .set_single_buffer_mode_allowed(false)
      .set_buffer_constraints_version_ordinal(new_output_buffer_constraints_version_ordinal);

  return constraints;
}

fuchsia::media::StreamOutputFormat CodecAdapterAacEncoder::CoreCodecGetOutputFormat(
    uint64_t stream_lifetime_ordinal, uint64_t new_output_format_details_version_ordinal) {
  ZX_DEBUG_ASSERT(output_sink_);

  auto audio_compressed_format = fuchsia::media::AudioCompressedFormat();
  audio_compressed_format.set_aac(fuchsia::media::AudioCompressedFormatAac());

  auto audio_format = fuchsia::media::AudioFormat();
  audio_format.set_compressed(std::move(audio_compressed_format));

  auto format_details = fuchsia::media::FormatDetails();
  format_details.set_format_details_version_ordinal(new_output_format_details_version_ordinal);
  format_details.set_mime_type(kAacMimeType);
  format_details.mutable_domain()->set_audio(std::move(audio_format));

  {
    std::lock_guard<std::mutex> lock(lock_);
    ZX_DEBUG_ASSERT_MSG(format_configuration_,
                        "The input thread triggered this call to generate output format, so it "
                        "should have prepared the format configuration.");
    format_details.set_oob_bytes(format_configuration_->oob_bytes);
  }

  auto format = fuchsia::media::StreamOutputFormat();
  format.set_stream_lifetime_ordinal(stream_lifetime_ordinal);
  format.set_format_details(std::move(format_details));

  return format;
}

void CodecAdapterAacEncoder::CoreCodecMidStreamOutputBufferReConfigPrepare() {
  // Nothing to do here.
}

void CodecAdapterAacEncoder::CoreCodecMidStreamOutputBufferReConfigFinish() {
  ZX_DEBUG_ASSERT(output_sink_);

  std::vector<const CodecBuffer*> buffers;
  std::optional<const CodecBuffer*> staged_buffer;
  while ((staged_buffer = staged_buffers_.Pop())) {
    buffers.push_back(*staged_buffer);
  }

  // Defense against Hyrum's Law.
  std::shuffle(buffers.begin(), buffers.end(), not_for_security_prng_);

  for (const auto buffer : buffers) {
    output_sink_->AddOutputBuffer(buffer);
  }
}

void CodecAdapterAacEncoder::ProcessInput(CodecInputItem input_item) {
  ZX_DEBUG_ASSERT(output_sink_);

  auto return_packet = fit::defer([this, &input_item] {
    if (input_item.is_packet()) {
      events_->onCoreCodecInputPacketDone(input_item.packet());
    }
  });

  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    if (!stream_active_) {
      // The stream is no longer active; we should not process this input.
      //
      // ~lock
      // ~return_packet
      return;
    }
  }  // ~lock

  if (input_item.is_format_details()) {
    if (stream_) {
      if (!(stream_->format_details_version_ordinal ==
            input_item.format_details().format_details_version_ordinal())) {
        events_->onCoreCodecFailCodec("Midstream format change not supported.");
      }
      return;
    }

    auto build_stream_result = BuildStreamFromFormatDetails(input_item.format_details());
    if (build_stream_result.is_error()) {
      ReportError(build_stream_result.error());
      return;
    }

    events_->onCoreCodecMidStreamOutputConstraintsChange(
        /*output_re_config_required=*/true);

    return;
  }

  ChunkInputStream::Status status;
  if (input_item.is_packet()) {
    status = stream_->chunk_input_stream.ProcessInputPacket(input_item.packet());
  } else {
    ZX_DEBUG_ASSERT(input_item.is_end_of_stream());
    status = stream_->chunk_input_stream.Flush();
  }

  switch (status) {
    case ChunkInputStream::kExtrapolationFailedWithoutTimebase:
      fprintf(stderr,
              "Codec stream failed; extrapolation was needed because "
              "of an unaligned timestamp, but no timebase was provided "
              "in `input_details`.\n");
      events_->onCoreCodecFailStream(fuchsia::media::StreamError::ENCODER_UNKNOWN);
    case ChunkInputStream::kUserTerminated:
      // A failure was reported through `events_`.
      stream_ = std::nullopt;
    case ChunkInputStream::kOk:
      return;
  };
}

fit::result<void, CodecAdapterAacEncoder::Error>
CodecAdapterAacEncoder::BuildStreamFromFormatDetails(
    const fuchsia::media::FormatDetails& format_details) {
  if (!format_details.has_encoder_settings() || !format_details.encoder_settings().is_aac()) {
    return fit::error(kSettingsMissing);
  }
  auto& encoder_settings = format_details.encoder_settings().aac();

  auto pcm_format_result = ValidateInputFormat(format_details);
  if (pcm_format_result.is_error()) {
    return fit::error(pcm_format_result.error());
  }
  auto pcm_format = pcm_format_result.take_value();

  auto create_encoder_result = CreateEncoder(pcm_format, encoder_settings);
  if (create_encoder_result.is_error()) {
    return fit::error(create_encoder_result.error());
  }
  auto encoder = create_encoder_result.take_value();

  AACENC_InfoStruct enc_info = {};
  AACENC_ERROR status = aacEncInfo(encoder.get(), &enc_info);
  if (status != AACENC_OK) {
    return fit::error(status);
  }

  // FDK can output in one frame at most 6144 bits per channel (from
  // documentation; tediously, the constant is not exported).
  constexpr size_t kFdkMaxOutBytesPerChannel = 6144 / 8;
  const size_t max_output_size = kFdkMaxOutBytesPerChannel * pcm_format.channel_map.size();

  std::vector<uint8_t> oob_bytes(enc_info.confSize, 0);
  memcpy(&oob_bytes[0], enc_info.confBuf, enc_info.confSize);
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    format_configuration_.emplace(FormatConfiguration{
        .oob_bytes = std::move(oob_bytes),
        .recommended_output_buffer_size = max_output_size,
    });
  }  // ~lock

  ChunkInputStream::InputBlockProcessor input_block_processor =
      [this](ChunkInputStream::InputBlock input_block) { return ProcessInputBlock(input_block); };

  const size_t pcm_frame_size = enc_info.inputChannels * sizeof(int16_t);
  const size_t bytes_per_second = pcm_format.frames_per_second * pcm_frame_size;
  auto extrapolator = format_details.has_timebase()
                          ? TimestampExtrapolator(format_details.timebase(), bytes_per_second)
                          : TimestampExtrapolator();

  const size_t pcm_frames_per_aac_frame = enc_info.frameLength;
  const size_t chunk_input_size = pcm_frame_size * pcm_frames_per_aac_frame;
  stream_.emplace(chunk_input_size, std::move(extrapolator), std::move(input_block_processor),
                  std::move(encoder), format_details.format_details_version_ordinal(),
                  max_output_size);

  return fit::ok();
}

fit::result<fuchsia::media::PcmFormat, CodecAdapterAacEncoder::InputError>
CodecAdapterAacEncoder::ValidateInputFormat(const fuchsia::media::FormatDetails& format_details) {
  if (!format_details.domain().is_audio()) {
    return fit::error(kNotAudio);
  }

  if (!format_details.domain().audio().is_uncompressed()) {
    return fit::error(kCompressed);
  }

  if (!format_details.domain().audio().uncompressed().is_pcm()) {
    return fit::error(kNotPcm);
  }
  auto& pcm_format = format_details.domain().audio().uncompressed().pcm();

  if (!(pcm_format.pcm_mode == fuchsia::media::AudioPcmMode::LINEAR)) {
    return fit::error(kNotLinear);
  }

  if (!(pcm_format.bits_per_sample == 16u)) {
    return fit::error(kNot16Bit);
  }

  return fit::ok(fidl::Clone(pcm_format));
}

fit::result<CodecAdapterAacEncoder::Encoder, CodecAdapterAacEncoder::Error>
CodecAdapterAacEncoder::CreateEncoder(const fuchsia::media::PcmFormat& pcm_format,
                                      const fuchsia::media::AacEncoderSettings& encoder_settings) {
  HANDLE_AACENCODER encoder = nullptr;
  AACENC_ERROR status = aacEncOpen(&encoder, 0, 0);
  if (status != AACENC_OK) {
    return fit::error(status);
  }

  uint32_t aot = INT_MAX;
  switch (encoder_settings.aot) {
    case fuchsia::media::AacAudioObjectType::MPEG2_AAC_LC:
      aot = AOT_MP2_AAC_LC;
      break;
    case fuchsia::media::AacAudioObjectType::MPEG4_AAC_LC:
      aot = AOT_AAC_LC;
      break;
    default:
      return fit::error(kUnsupportedObjectType);
  };

  if ((status = aacEncoder_SetParam(encoder, AACENC_AOT, aot)) != AACENC_OK) {
    return fit::error(status);
  }

  constexpr uint32_t kFdkMono = MODE_1;
  constexpr uint32_t kFdkStereo = MODE_2;

  uint32_t channel_mode = INT_MAX;
  switch (encoder_settings.channel_mode) {
    case fuchsia::media::AacChannelMode::MONO:
      channel_mode = kFdkMono;
      break;
    case fuchsia::media::AacChannelMode::STEREO:
      channel_mode = kFdkStereo;
      break;
    default:
      return fit::error(kUnsupportedChannelMode);
  };

  if ((status = aacEncoder_SetParam(encoder, AACENC_CHANNELMODE, channel_mode)) != AACENC_OK) {
    return fit::error(status);
  }

  if ((status = aacEncoder_SetParam(encoder, AACENC_SAMPLERATE, pcm_format.frames_per_second)) !=
      AACENC_OK) {
    return fit::error(status);
  }

  uint32_t bit_rate = 0;
  uint32_t bit_rate_mode = 0;
  if (encoder_settings.bit_rate.is_constant()) {
    bit_rate = static_cast<uint32_t>(encoder_settings.bit_rate.constant().bit_rate);
  } else {
    bit_rate_mode = static_cast<uint32_t>(encoder_settings.bit_rate.variable());
  }

  if ((status = aacEncoder_SetParam(encoder, AACENC_BITRATEMODE, bit_rate_mode)) != AACENC_OK) {
    return fit::error(status);
  }

  if ((status = aacEncoder_SetParam(encoder, AACENC_BITRATE, bit_rate)) != AACENC_OK) {
    return fit::error(status);
  }

  uint32_t transmux = INT_MAX;
  if (encoder_settings.transport.is_raw()) {
    transmux = TT_MP4_RAW;
  } else if (encoder_settings.transport.is_latm()) {
    if (encoder_settings.transport.latm().mux_config_present) {
      transmux = TT_MP4_LATM_MCP1;
    } else {
      transmux = TT_MP4_LATM_MCP0;
    }
  } else if (encoder_settings.transport.is_adts()) {
    transmux = TT_MP4_ADTS;
  } else {
    return fit::error(kUnsupportedTransport);
  }

  if ((status = aacEncoder_SetParam(encoder, AACENC_TRANSMUX, transmux)) != AACENC_OK) {
    return fit::error(status);
  }

  if (transmux == TT_MP4_LATM_MCP1) {
    uint32_t header_period = 1;
    if ((status = aacEncoder_SetParam(encoder, AACENC_HEADER_PERIOD, header_period)) != AACENC_OK) {
      return fit::error(status);
    }

    uint32_t audio_mux_version = 2;
    if ((status = aacEncoder_SetParam(encoder, AACENC_AUDIOMUXVER, audio_mux_version)) !=
        AACENC_OK) {
      return fit::error(status);
    }
  }

  // Enable extra psychoacoustic processing for better audio quality. Not observed to use an
  // appreciable amount of extra CPU.
  uint32_t afterburner = 1;
  if ((status = aacEncoder_SetParam(encoder, AACENC_AFTERBURNER, afterburner)) != AACENC_OK) {
    return fit::error(status);
  }

  if ((status = aacEncoder_SetParam(encoder, AACENC_SIGNALING_MODE, SIG_EXPLICIT_BW_COMPATIBLE)) !=
      AACENC_OK) {
    return fit::error(status);
  }

  if ((status = aacEncEncode(encoder, NULL, NULL, NULL, NULL)) != AACENC_OK) {
    return fit::error(status);
  }

  return fit::ok(Encoder(encoder, [](AACENCODER* encoder) { aacEncClose(&encoder); }));
}

ChunkInputStream::ControlFlow CodecAdapterAacEncoder::ProcessInputBlock(
    ChunkInputStream::InputBlock input_block) {
  EncodeResult encode_result;
  if (input_block.non_padding_len > 0) {
    auto output_sink_status = output_sink_->NextOutputBlock(
        stream_->output_buffer_size, input_block.timestamp_ish,
        [this, &input_block,
         &encode_result](OutputSink::OutputBlock output_block) -> OutputSink::OutputResult {
          ZX_DEBUG_ASSERT(output_block.len == stream_->output_buffer_size);

          auto result = Encode(input_block, output_block);
          if (result.is_error()) {
            events_->onCoreCodecFailCodec("Encoding failed: %d", result.error());
            return {.len = 0, .status = OutputSink::kError};
          }

          encode_result = result.take_value();
          return {.len = encode_result.bytes_written, .status = OutputSink::kSuccess};
        });
    if (output_sink_status != OutputSink::kOk) {
      ReportOutputSinkError(output_sink_status);
      return ChunkInputStream::kTerminate;
    }
  }

  auto flush_timestamp = [timestamp = input_block.flush_timestamp_ish]() mutable {
    auto value = timestamp;
    timestamp = std::nullopt;
    return value;
  };

  while (input_block.is_end_of_stream && !encode_result.is_end_of_stream) {
    auto output_sink_status = output_sink_->NextOutputBlock(
        stream_->output_buffer_size, flush_timestamp(),
        [this, &encode_result](OutputSink::OutputBlock output_block) -> OutputSink::OutputResult {
          auto result = Flush(output_block);
          if (result.is_error()) {
            events_->onCoreCodecFailCodec("Flushing encoder failed: %d", result.error());
            return {.len = 0, .status = OutputSink::kError};
          }
          encode_result = result.take_value();
          return {.len = encode_result.bytes_written, .status = OutputSink::kSuccess};
        });
    if (output_sink_status != OutputSink::kOk) {
      ReportOutputSinkError(output_sink_status);
      return ChunkInputStream::kTerminate;
    }
  }

  if (input_block.is_end_of_stream) {
    auto output_sink_status = output_sink_->Flush();
    if (output_sink_status != OutputSink::kOk) {
      ReportOutputSinkError(output_sink_status);
      return ChunkInputStream::kTerminate;
    }
    events_->onCoreCodecOutputEndOfStream(/*error_encountered_before=*/false);
  }

  return ChunkInputStream::kContinue;
}

fit::result<CodecAdapterAacEncoder::EncodeResult, AACENC_ERROR> CodecAdapterAacEncoder::Encode(
    ChunkInputStream::InputBlock input_block, OutputSink::OutputBlock output_block) {
  void* input_buffers[] = {const_cast<uint8_t*>(input_block.data)};
  INT input_buffer_identifiers[] = {IN_AUDIO_DATA};
  INT input_buffer_sizes[] = {static_cast<INT>(input_block.non_padding_len)};
  INT input_buffer_element_sizes[] = {sizeof(int16_t)};
  AACENC_InArgs input_args = {
      .numInSamples = static_cast<INT>(input_block.len / sizeof(int16_t)),
      .numAncBytes = 0,
  };
  AACENC_BufDesc input_buffer_descriptor = {.numBufs = 1,
                                            .bufs = static_cast<void**>(input_buffers),
                                            .bufferIdentifiers = input_buffer_identifiers,
                                            .bufSizes = input_buffer_sizes,
                                            .bufElSizes = input_buffer_element_sizes};

  return CallEncoder(&input_args, &input_buffer_descriptor, output_block);
}

fit::result<CodecAdapterAacEncoder::EncodeResult, AACENC_ERROR> CodecAdapterAacEncoder::Flush(
    OutputSink::OutputBlock output_block) {
  void* input_buffers[] = {};
  INT input_buffer_identifiers[] = {IN_AUDIO_DATA};
  INT input_buffer_sizes[] = {};
  INT input_buffer_element_sizes[] = {sizeof(uint8_t)};
  AACENC_InArgs input_args = {
      .numInSamples = -1,
      .numAncBytes = 0,
  };
  AACENC_BufDesc input_buffer_descriptor = {.numBufs = 0,
                                            .bufs = static_cast<void**>(input_buffers),
                                            .bufferIdentifiers = input_buffer_identifiers,
                                            .bufSizes = input_buffer_sizes,
                                            .bufElSizes = input_buffer_element_sizes};

  return CallEncoder(&input_args, &input_buffer_descriptor, output_block);
}

fit::result<CodecAdapterAacEncoder::EncodeResult, AACENC_ERROR> CodecAdapterAacEncoder::CallEncoder(
    AACENC_InArgs* in_args, AACENC_BufDesc* in_buffer, OutputSink::OutputBlock output_block) {
  void* output_buffers[] = {output_block.data};
  INT output_buffer_identifiers[] = {OUT_BITSTREAM_DATA};
  INT output_buffer_sizes[] = {static_cast<INT>(output_block.len)};
  INT output_buffer_element_sizes[] = {sizeof(uint8_t)};

  AACENC_OutArgs output_args = {};
  AACENC_BufDesc output_buffer_descriptor = {
      .numBufs = 1,
      .bufs = static_cast<void**>(output_buffers),
      .bufferIdentifiers = output_buffer_identifiers,
      .bufSizes = output_buffer_sizes,
      .bufElSizes = output_buffer_element_sizes,
  };

  EncodeResult result = {};
  AACENC_ERROR status = aacEncEncode(stream_->encoder.get(), in_buffer, &output_buffer_descriptor,
                                     in_args, &output_args);
  if (status == AACENC_ENCODE_EOF) {
    result.is_end_of_stream = true;
  } else if (status != AACENC_OK) {
    return fit::error(status);
  }

  result.bytes_written = output_args.numOutBytes;
  return fit::ok(result);
}

void CodecAdapterAacEncoder::ReportError(Error error) {
  std::visit(
      [this](auto&& error) {
        using T = std::decay_t<decltype(error)>;

        if constexpr (std::is_same_v<T, InputError>) {
          switch (error) {
            case kNotAudio:
              events_->onCoreCodecFailCodec("Input to aac encoder must be audio.");
              break;
            case kNotPcm:
              events_->onCoreCodecFailCodec("Input to aac encoder must be pcm.");
              break;
            case kNot16Bit:
              events_->onCoreCodecFailCodec("Input to aac encoder must be 16bit samples.");
              break;
            case kNotLinear:
              events_->onCoreCodecFailCodec("Input to aac encoder must be linear samples.");
              break;
            case kCompressed:
              events_->onCoreCodecFailCodec("Input to aac encoder must be uncompressed.");
              break;
          }
        } else if constexpr (std::is_same_v<T, SettingsError>) {
          switch (error) {
            case kSettingsMissing:
              events_->onCoreCodecFailCodec("AAC encoder settings missing.");
              break;
            case kUnsupportedObjectType:
              events_->onCoreCodecFailCodec("Unsupported object type.");
              break;
            case kUnsupportedTransport:
              events_->onCoreCodecFailCodec("Unsupported transport.");
              break;
            case kUnsupportedChannelMode:
              events_->onCoreCodecFailCodec("Unsupported channel mode.");
              break;
          }
        } else if constexpr (std::is_same_v<T, AACENC_ERROR>) {
          events_->onCoreCodecFailCodec("FDK error: %d; consult FDK_audio.h.", error);
        }
      },
      error);
}

void CodecAdapterAacEncoder::ReportOutputSinkError(OutputSink::Status status) {
  switch (status) {
    case OutputSink::kBuffersTooSmall:
      events_->onCoreCodecFailCodec(
          "Output buffers do not satisfy the codec's minimum size "
          "constraints.");
      break;
    default:
      // Other errors originate from us; we report them ourselves.
      break;
  }
}

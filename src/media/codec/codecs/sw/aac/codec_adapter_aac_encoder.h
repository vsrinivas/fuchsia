// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_CODECS_SW_AAC_CODEC_ADAPTER_AAC_ENCODER_H_
#define SRC_MEDIA_CODEC_CODECS_SW_AAC_CODEC_ADAPTER_AAC_ENCODER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/result.h>
#include <lib/media/codec_impl/codec_adapter.h>
#include <lib/zx/bti.h>

#include <atomic>
#include <variant>

#include <third_party/android/platform/external/aac/libAACenc/include/aacenc_lib.h>

#include "chunk_input_stream.h"
#include "output_sink.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"

class CodecAdapterAacEncoder : public CodecAdapter {
 public:
  explicit CodecAdapterAacEncoder(std::mutex& lock, CodecAdapterEvents* codec_adapter_events);
  ~CodecAdapterAacEncoder();

  bool IsCoreCodecRequiringOutputConfigForFormatDetection() override;
  bool IsCoreCodecMappedBufferUseful(CodecPort port) override;
  bool IsCoreCodecHwBased(CodecPort port) override;

  void CoreCodecInit(const fuchsia::media::FormatDetails& initial_input_format_details) override;
  void CoreCodecStartStream() override;
  void CoreCodecStopStream() override;

  void CoreCodecQueueInputFormatDetails(
      const fuchsia::media::FormatDetails& per_stream_override_format_details) override;
  void CoreCodecQueueInputPacket(CodecPacket* packet) override;
  void CoreCodecQueueInputEndOfStream() override;

  void CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) override;
  void CoreCodecConfigureBuffers(CodecPort port,
                                 const std::vector<std::unique_ptr<CodecPacket>>& packets) override;
  void CoreCodecRecycleOutputPacket(CodecPacket* packet) override;
  void CoreCodecEnsureBuffersNotConfigured(CodecPort port) override;
  std::unique_ptr<const fuchsia::media::StreamOutputConstraints> CoreCodecBuildNewOutputConstraints(
      uint64_t stream_lifetime_ordinal, uint64_t new_output_buffer_constraints_version_ordinal,
      bool buffer_constraints_action_required) override;
  fuchsia::sysmem::BufferCollectionConstraints CoreCodecGetBufferCollectionConstraints(
      CodecPort port, const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
      const fuchsia::media::StreamBufferPartialSettings& partial_settings) override;
  void CoreCodecSetBufferCollectionInfo(
      CodecPort port,
      const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) override;
  fuchsia::media::StreamOutputFormat CoreCodecGetOutputFormat(
      uint64_t stream_lifetime_ordinal,
      uint64_t new_output_format_details_version_ordinal) override;
  void CoreCodecMidStreamOutputBufferReConfigPrepare() override;
  void CoreCodecMidStreamOutputBufferReConfigFinish() override;

 private:
  using Encoder = std::unique_ptr<AACENCODER, fit::function<void(AACENCODER*)>>;

  enum InputError {
    kNotAudio,
    kNotPcm,
    kNot16Bit,
    kNotLinear,
    kCompressed,
  };

  enum SettingsError {
    kSettingsMissing,
    kUnsupportedObjectType,
    kUnsupportedChannelMode,
    kUnsupportedTransport,
  };

  using Error = std::variant<AACENC_ERROR, InputError, SettingsError>;

  struct FormatConfiguration {
    std::vector<uint8_t> oob_bytes;
    size_t recommended_output_buffer_size;
  };

  struct EncodeResult {
    size_t bytes_written;
    bool is_end_of_stream;
  };

  // This struct contains data who live the lifetime of a stream.
  struct Stream {
    Stream(size_t chunk_size, TimestampExtrapolator&& timestamp_extrapolator,
           ChunkInputStream::InputBlockProcessor&& input_block_processor, Encoder in_encoder,
           uint64_t in_format_details_version_ordinal, size_t in_output_buffer_size)
        : encoder(std::move(in_encoder)),
          chunk_input_stream(chunk_size, std::move(timestamp_extrapolator),
                             std::move(input_block_processor)),
          format_details_version_ordinal(in_format_details_version_ordinal),
          output_buffer_size(in_output_buffer_size) {}

    Encoder encoder;
    ChunkInputStream chunk_input_stream;
    uint64_t format_details_version_ordinal;
    size_t output_buffer_size;
  };

  void ProcessInput(CodecInputItem input_item);

  fit::result<void, CodecAdapterAacEncoder::Error> BuildStreamFromFormatDetails(
      const fuchsia::media::FormatDetails& format_details);

  fit::result<fuchsia::media::PcmFormat, InputError> ValidateInputFormat(
      const fuchsia::media::FormatDetails& format_details);

  fit::result<Encoder, Error> CreateEncoder(
      const fuchsia::media::PcmFormat& pcm_format,
      const fuchsia::media::AacEncoderSettings& encoder_settings);

  ChunkInputStream::ControlFlow ProcessInputBlock(ChunkInputStream::InputBlock input_block);

  fit::result<EncodeResult, AACENC_ERROR> Encode(ChunkInputStream::InputBlock input_block,
                                                 OutputSink::OutputBlock output_block);

  fit::result<EncodeResult, AACENC_ERROR> Flush(OutputSink::OutputBlock output_block);

  fit::result<EncodeResult, AACENC_ERROR> CallEncoder(AACENC_InArgs* in_args,
                                                      AACENC_BufDesc* in_buffer,
                                                      OutputSink::OutputBlock output_block);

  void ReportError(Error error);

  void ReportOutputSinkError(OutputSink::Status status);

  std::optional<OutputSink> output_sink_;
  std::optional<Stream> stream_;

  // Should only be changed atomically.
  bool stream_active_ FXL_GUARDED_BY(lock_) = false;
  std::optional<FormatConfiguration> format_configuration_ FXL_GUARDED_BY(lock_);

  // Buffers the user is in the process of adding.
  // TODO(turnage): Remove when manual buffer additions are removed in favor
  // of sysmem.
  MpscQueue<const CodecBuffer*> staged_buffers_;
  async::Loop input_processing_loop_;
};

#endif  // SRC_MEDIA_CODEC_CODECS_SW_AAC_CODEC_ADAPTER_AAC_ENCODER_H_

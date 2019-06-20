// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_AAC_CODEC_ADAPTER_AAC_ENCODER_H_
#define GARNET_BIN_MEDIA_CODECS_SW_AAC_CODEC_ADAPTER_AAC_ENCODER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/media/codec_impl/codec_adapter.h>
#include <lib/zx/bti.h>

class CodecAdapterAacEncoder : public CodecAdapter {
 public:
  explicit CodecAdapterAacEncoder(std::mutex& lock,
                                  CodecAdapterEvents* codec_adapter_events);
  ~CodecAdapterAacEncoder() {}

  bool IsCoreCodecRequiringOutputConfigForFormatDetection() override;
  bool IsCoreCodecMappedBufferNeeded(CodecPort port) override;
  bool IsCoreCodecHwBased() override;

  void CoreCodecInit(const fuchsia::media::FormatDetails&
                         initial_input_format_details) override;
  void CoreCodecStartStream() override;
  void CoreCodecStopStream() override;

  void CoreCodecQueueInputFormatDetails(
      const fuchsia::media::FormatDetails& per_stream_override_format_details)
      override;
  void CoreCodecQueueInputPacket(CodecPacket* packet) override;
  void CoreCodecQueueInputEndOfStream() override;

  void CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) override;
  void CoreCodecConfigureBuffers(
      CodecPort port,
      const std::vector<std::unique_ptr<CodecPacket>>& packets) override;
  void CoreCodecRecycleOutputPacket(CodecPacket* packet) override;
  void CoreCodecEnsureBuffersNotConfigured(CodecPort port) override;
  std::unique_ptr<const fuchsia::media::StreamOutputConstraints>
  CoreCodecBuildNewOutputConstraints(
      uint64_t stream_lifetime_ordinal,
      uint64_t new_output_buffer_constraints_version_ordinal,
      bool buffer_constraints_action_required) override;
  fuchsia::sysmem::BufferCollectionConstraints
  CoreCodecGetBufferCollectionConstraints(
      CodecPort port,
      const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
      const fuchsia::media::StreamBufferPartialSettings& partial_settings)
      override;
  void CoreCodecSetBufferCollectionInfo(
      CodecPort port,
      const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info)
      override;
  fuchsia::media::StreamOutputFormat CoreCodecGetOutputFormat(
      uint64_t stream_lifetime_ordinal,
      uint64_t new_output_format_details_version_ordinal) override;
  void CoreCodecMidStreamOutputBufferReConfigPrepare() override;
  void CoreCodecMidStreamOutputBufferReConfigFinish() override;

 private:
};

#endif  // GARNET_BIN_MEDIA_CODECS_SW_AAC_CODEC_ADAPTER_AAC_ENCODER_H_

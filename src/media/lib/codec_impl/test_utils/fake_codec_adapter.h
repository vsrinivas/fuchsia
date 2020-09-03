// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_LIB_CODEC_IMPL_TEST_UTILS_FAKE_CODEC_ADAPTER_H_
#define SRC_MEDIA_LIB_CODEC_IMPL_TEST_UTILS_FAKE_CODEC_ADAPTER_H_

#include <lib/media/codec_impl/codec_adapter.h>

class FakeCodecAdapter : public CodecAdapter {
 public:
  explicit FakeCodecAdapter(std::mutex& lock, CodecAdapterEvents* codec_adapter_events);
  virtual ~FakeCodecAdapter();

  // CodecAdapter interface:
  bool IsCoreCodecRequiringOutputConfigForFormatDetection() override;
  bool IsCoreCodecMappedBufferUseful(CodecPort port) override;
  bool IsCoreCodecHwBased(CodecPort port) override;
  void CoreCodecInit(const fuchsia::media::FormatDetails& initial_input_format_details) override;
  fuchsia::sysmem::BufferCollectionConstraints CoreCodecGetBufferCollectionConstraints(
      CodecPort port, const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
      const fuchsia::media::StreamBufferPartialSettings& partial_settings) override;
  void CoreCodecSetBufferCollectionInfo(
      CodecPort port,
      const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) override;
  void CoreCodecStartStream() override;
  void CoreCodecQueueInputFormatDetails(
      const fuchsia::media::FormatDetails& per_stream_override_format_details) override;
  void CoreCodecQueueInputPacket(CodecPacket* packet) override;
  void CoreCodecQueueInputEndOfStream() override;
  void CoreCodecStopStream() override;
  void CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) override;
  void CoreCodecConfigureBuffers(CodecPort port,
                                 const std::vector<std::unique_ptr<CodecPacket>>& packets) override;
  void CoreCodecRecycleOutputPacket(CodecPacket* packet) override;
  void CoreCodecEnsureBuffersNotConfigured(CodecPort port) override;
  std::unique_ptr<const fuchsia::media::StreamOutputConstraints> CoreCodecBuildNewOutputConstraints(
      uint64_t stream_lifetime_ordinal, uint64_t new_output_buffer_constraints_version_ordinal,
      bool buffer_constraints_action_required) override;
  fuchsia::media::StreamOutputFormat CoreCodecGetOutputFormat(
      uint64_t stream_lifetime_ordinal,
      uint64_t new_output_format_details_version_ordinal) override;
  void CoreCodecMidStreamOutputBufferReConfigPrepare() override;
  void CoreCodecMidStreamOutputBufferReConfigFinish() override;

  // Test hooks
  void SetBufferCollectionConstraints(CodecPort port,
                                      fuchsia::sysmem::BufferCollectionConstraints constraints);

 private:
  std::optional<fuchsia::sysmem::BufferCollectionConstraints>
      buffer_collection_constraints_[kPortCount];
};

#endif  // SRC_MEDIA_LIB_CODEC_IMPL_TEST_UTILS_FAKE_CODEC_ADAPTER_H_

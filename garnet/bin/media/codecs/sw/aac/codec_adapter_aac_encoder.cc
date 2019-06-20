// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_adapter_aac_encoder.h"

#include "chunk_input_stream.h"
#include "output_sink.h"

CodecAdapterAacEncoder::CodecAdapterAacEncoder(
    std::mutex& lock, CodecAdapterEvents* codec_adapter_events)
    : CodecAdapter(lock, codec_adapter_events) {}

bool CodecAdapterAacEncoder::
    IsCoreCodecRequiringOutputConfigForFormatDetection() {
  return false;
}
bool CodecAdapterAacEncoder::IsCoreCodecMappedBufferNeeded(CodecPort port) {
  return true;
}
bool CodecAdapterAacEncoder::IsCoreCodecHwBased() { return false; }

void CodecAdapterAacEncoder::CoreCodecInit(
    const fuchsia::media::FormatDetails& initial_input_format_details) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

fuchsia::sysmem::BufferCollectionConstraints
CodecAdapterAacEncoder::CoreCodecGetBufferCollectionConstraints(
    CodecPort port,
    const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
    const fuchsia::media::StreamBufferPartialSettings& partial_settings) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
  return fuchsia::sysmem::BufferCollectionConstraints();
}

void CodecAdapterAacEncoder::CoreCodecSetBufferCollectionInfo(
    CodecPort port,
    const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterAacEncoder::CoreCodecStartStream() {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterAacEncoder::CoreCodecQueueInputFormatDetails(
    const fuchsia::media::FormatDetails& per_stream_override_format_details) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterAacEncoder::CoreCodecQueueInputPacket(CodecPacket* packet) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterAacEncoder::CoreCodecQueueInputEndOfStream() {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterAacEncoder::CoreCodecStopStream() {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterAacEncoder::CoreCodecAddBuffer(CodecPort port,
                                                const CodecBuffer* buffer) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterAacEncoder::CoreCodecConfigureBuffers(
    CodecPort port, const std::vector<std::unique_ptr<CodecPacket>>& packets) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterAacEncoder::CoreCodecRecycleOutputPacket(CodecPacket* packet) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterAacEncoder::CoreCodecEnsureBuffersNotConfigured(
    CodecPort port) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

std::unique_ptr<const fuchsia::media::StreamOutputConstraints>
CodecAdapterAacEncoder::CoreCodecBuildNewOutputConstraints(
    uint64_t stream_lifetime_ordinal,
    uint64_t new_output_buffer_constraints_version_ordinal,
    bool buffer_constraints_action_required) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
  return std::make_unique<const fuchsia::media::StreamOutputConstraints>();
}

fuchsia::media::StreamOutputFormat
CodecAdapterAacEncoder::CoreCodecGetOutputFormat(
    uint64_t stream_lifetime_ordinal,
    uint64_t new_output_format_details_version_ordinal) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
  return fuchsia::media::StreamOutputFormat();
}

void CodecAdapterAacEncoder::CoreCodecMidStreamOutputBufferReConfigPrepare() {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterAacEncoder::CoreCodecMidStreamOutputBufferReConfigFinish() {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

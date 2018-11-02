// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_adapter_ffmpeg_decoder.h"

CodecAdapterFfmpegDecoder::CodecAdapterFfmpegDecoder(
    std::mutex& lock, CodecAdapterEvents* codec_adapter_events)
    : CodecAdapter(lock, codec_adapter_events) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

CodecAdapterFfmpegDecoder::~CodecAdapterFfmpegDecoder() {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

bool CodecAdapterFfmpegDecoder::
    IsCoreCodecRequiringOutputConfigForFormatDetection() {
  return false;
}

void CodecAdapterFfmpegDecoder::CoreCodecInit(
    const fuchsia::mediacodec::CodecFormatDetails&
        initial_input_format_details) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterFfmpegDecoder::CoreCodecStartStream() {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterFfmpegDecoder::CoreCodecQueueInputFormatDetails(
    const fuchsia::mediacodec::CodecFormatDetails&
        per_stream_override_format_details) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterFfmpegDecoder::CoreCodecQueueInputPacket(CodecPacket* packet) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterFfmpegDecoder::CoreCodecQueueInputEndOfStream() {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterFfmpegDecoder::CoreCodecStopStream() {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterFfmpegDecoder::CoreCodecAddBuffer(CodecPort port,
                                                   const CodecBuffer* buffer) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterFfmpegDecoder::CoreCodecConfigureBuffers(
    CodecPort port, const std::vector<std::unique_ptr<CodecPacket>>& packets) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterFfmpegDecoder::CoreCodecRecycleOutputPacket(
    CodecPacket* packet) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterFfmpegDecoder::CoreCodecEnsureBuffersNotConfigured(
    CodecPort port) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

std::unique_ptr<const fuchsia::mediacodec::CodecOutputConfig>
CodecAdapterFfmpegDecoder::CoreCodecBuildNewOutputConfig(
    uint64_t stream_lifetime_ordinal,
    uint64_t new_output_buffer_constraints_version_ordinal,
    uint64_t new_output_format_details_version_ordinal,
    bool buffer_constraints_action_required) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
  return std::make_unique<const fuchsia::mediacodec::CodecOutputConfig>();
}

void CodecAdapterFfmpegDecoder::
    CoreCodecMidStreamOutputBufferReConfigPrepare() {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterFfmpegDecoder::CoreCodecMidStreamOutputBufferReConfigFinish() {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}
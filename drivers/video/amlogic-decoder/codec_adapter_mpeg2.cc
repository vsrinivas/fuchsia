// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_adapter_mpeg2.h"

#include "device_ctx.h"

CodecAdapterMpeg2::CodecAdapterMpeg2(std::mutex& lock,
                                     CodecAdapterEvents* codec_adapter_events,
                                     DeviceCtx* device)
    : CodecAdapter(lock, codec_adapter_events),
      device_(device),
      video_(device_->video()) {
  ZX_DEBUG_ASSERT(device_);
  ZX_DEBUG_ASSERT(video_);
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
  (void)video_;
}

CodecAdapterMpeg2::~CodecAdapterMpeg2() {
  ZX_DEBUG_ASSERT_MSG(false,
                      "not yet implemented - power off probably can go here");
}

bool CodecAdapterMpeg2::IsCoreCodecRequiringOutputConfigForFormatDetection() {
  return false;
}

void CodecAdapterMpeg2::CoreCodecInit(
    const fuchsia::mediacodec::CodecFormatDetails&
        initial_input_format_details) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterMpeg2::CoreCodecStartStream() {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterMpeg2::CoreCodecQueueInputFormatDetails(
    const fuchsia::mediacodec::CodecFormatDetails&
        per_stream_override_format_details) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterMpeg2::CoreCodecQueueInputPacket(CodecPacket* packet) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterMpeg2::CoreCodecQueueInputEndOfStream() {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterMpeg2::CoreCodecStopStream() {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterMpeg2::CoreCodecAddBuffer(CodecPort port,
                                           const CodecBuffer* buffer) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterMpeg2::CoreCodecConfigureBuffers(
    CodecPort port, const std::vector<std::unique_ptr<CodecPacket>>& packets) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterMpeg2::CoreCodecRecycleOutputPacket(CodecPacket* packet) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterMpeg2::CoreCodecEnsureBuffersNotConfigured(CodecPort port) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

std::unique_ptr<const fuchsia::mediacodec::CodecOutputConfig>
CodecAdapterMpeg2::CoreCodecBuildNewOutputConfig(
    uint64_t stream_lifetime_ordinal,
    uint64_t new_output_buffer_constraints_version_ordinal,
    uint64_t new_output_format_details_version_ordinal,
    bool buffer_constraints_action_required) {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
  return std::make_unique<const fuchsia::mediacodec::CodecOutputConfig>();
}

void CodecAdapterMpeg2::CoreCodecMidStreamOutputBufferReConfigPrepare() {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

void CodecAdapterMpeg2::CoreCodecMidStreamOutputBufferReConfigFinish() {
  ZX_DEBUG_ASSERT_MSG(false, "not yet implemented");
}

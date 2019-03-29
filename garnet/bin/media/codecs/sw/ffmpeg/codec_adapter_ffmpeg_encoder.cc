// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_adapter_ffmpeg_encoder.h"

#include <lib/media/codec_impl/codec_buffer.h>

CodecAdapterFfmpegEncoder::CodecAdapterFfmpegEncoder(
    std::mutex& lock, CodecAdapterEvents* codec_adapter_events)
    : CodecAdapterSW(lock, codec_adapter_events) {
  ZX_ASSERT_MSG(false, "Not implemented.");
}

CodecAdapterFfmpegEncoder::~CodecAdapterFfmpegEncoder() = default;

void CodecAdapterFfmpegEncoder::CoreCodecAddBuffer(CodecPort port,
                                                   const CodecBuffer* buffer) {
  ZX_ASSERT_MSG(false, "Not implemented.");
}

void CodecAdapterFfmpegEncoder::ProcessInputLoop() {
  ZX_ASSERT_MSG(false, "Not implemented.");
}

void CodecAdapterFfmpegEncoder::UnreferenceOutputPacket(CodecPacket* packet) {
  ZX_ASSERT_MSG(false, "Not implemented.");
}

void CodecAdapterFfmpegEncoder::UnreferenceClientBuffers() {
  ZX_ASSERT_MSG(false, "Not implemented.");
}

void CodecAdapterFfmpegEncoder::BeginStopInputProcessing() {
  ZX_ASSERT_MSG(false, "Not implemented.");
}

void CodecAdapterFfmpegEncoder::CleanUpAfterStream() {
  ZX_ASSERT_MSG(false, "Not implemented.");
}

std::pair<fuchsia::media::FormatDetails, size_t>
CodecAdapterFfmpegEncoder::OutputFormatDetails() {
  ZX_ASSERT_MSG(false, "Not implemented.");
  return {fuchsia::media::FormatDetails{}, 0};
}
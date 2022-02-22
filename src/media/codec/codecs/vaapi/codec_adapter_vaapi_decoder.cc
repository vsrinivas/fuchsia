// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_adapter_vaapi_decoder.h"

#include <zircon/status.h>

#include <va/va_drmcommon.h>

#include "media/gpu/h264_decoder.h"

#define LOG(x, ...) fprintf(stderr, __VA_ARGS__)

void CodecAdapterVaApiDecoder::CoreCodecInit(
    const fuchsia::media::FormatDetails& initial_input_format_details) {
  if (!initial_input_format_details.has_format_details_version_ordinal()) {
    events_->onCoreCodecFailCodec(
        "CoreCodecInit(): Initial input format details missing version "
        "ordinal.");
    return;
  }
  // Will always be 0 for now.
  input_format_details_version_ordinal_ =
      initial_input_format_details.format_details_version_ordinal();

  const std::string& mime_type = initial_input_format_details.mime_type();
  if (mime_type == "video/h264-multi" || mime_type == "video/h264") {
    // TODO: Create media decoder.
  } else {
    events_->onCoreCodecFailCodec("CodecCodecInit(): Unknown mime_type %s\n", mime_type.c_str());
    return;
  }
  zx_status_t result =
      input_processing_loop_.StartThread("input_processing_thread_", &input_processing_thread_);
  if (result != ZX_OK) {
    events_->onCoreCodecFailCodec(
        "CodecCodecInit(): Failed to start input processing thread with "
        "zx_status_t: %d",
        result);
    return;
  }
}

void CodecAdapterVaApiDecoder::ProcessInputLoop() {
  std::optional<CodecInputItem> maybe_input_item;
  while ((maybe_input_item = input_queue_.WaitForElement())) {
    CodecInputItem input_item = std::move(maybe_input_item.value());
    if (input_item.is_format_details()) {
      // TODO: Create config
    } else if (input_item.is_end_of_stream()) {
      // TODO: Flush decoder.
      events_->onCoreCodecOutputEndOfStream(/*error_detected_before=*/false);
    } else if (input_item.is_packet()) {
      auto* packet = input_item.packet();
      ZX_DEBUG_ASSERT(packet->has_start_offset());
      uint8_t* buffer_start = packet->buffer()->base() + packet->start_offset();
      // TODO(fxbug.dev/94139): Remove this copy.
      std::vector<uint8_t> data(buffer_start, buffer_start + packet->valid_length_bytes());
      if (packet->has_timestamp_ish()) {
        stream_to_pts_map_.emplace_back(next_stream_id_, packet->timestamp_ish());
        constexpr size_t kMaxPtsMapSize = 64;
        if (stream_to_pts_map_.size() > kMaxPtsMapSize)
          stream_to_pts_map_.pop_front();
      }
      events_->onCoreCodecInputPacketDone(input_item.packet());

      // TODO: Decode packet.
    }
  }
}

void CodecAdapterVaApiDecoder::CleanUpAfterStream() {
  // TODO: Output end of stream delimiter and flush.
}

VaApiOutput::~VaApiOutput() {
  if (adapter_)
    adapter_->output_buffer_pool_.FreeBuffer(base_address_);
}

VaApiOutput::VaApiOutput(VaApiOutput&& other) noexcept {
  adapter_ = other.adapter_;
  base_address_ = other.base_address_;
  other.adapter_ = nullptr;
}

VaApiOutput& VaApiOutput::operator=(VaApiOutput&& other) noexcept {
  adapter_ = other.adapter_;
  base_address_ = other.base_address_;
  other.adapter_ = nullptr;
  return *this;
}

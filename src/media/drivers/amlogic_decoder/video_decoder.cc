// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "video_decoder.h"

namespace amlogic_decoder {

namespace {

std::atomic<uint32_t> next_decoder_id_;

}  // namespace

VideoDecoder::VideoDecoder(
    media_metrics::StreamProcessorEvents2MetricDimensionImplementation implementation,
    std::string_view implementation_name, Owner* owner, Client* client, bool is_secure)
    : decoder_id_(next_decoder_id_++),
      owner_(owner),
      client_(client),
      is_secure_(is_secure),
      implementation_(implementation),
      diagnostics_(owner_->diagnostics().CreateCodec(implementation_name)) {
  pts_manager_ = std::make_unique<PtsManager>();
  LogEvent(media_metrics::StreamProcessorEvents2MetricDimensionEvent_CoreCreated);
}

VideoDecoder::~VideoDecoder() {
  LogEvent(media_metrics::StreamProcessorEvents2MetricDimensionEvent_CoreDeleted);
}

void VideoDecoder::LogEvent(media_metrics::StreamProcessorEvents2MetricDimensionEvent event) {
  metrics().LogEvent(implementation_, event);
}

}  // namespace amlogic_decoder

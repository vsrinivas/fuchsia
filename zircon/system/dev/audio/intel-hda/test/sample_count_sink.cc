// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sample_count_sink.h"

#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>
#include <iostream>

#include <audio-proto-utils/format-utils.h>
#include <audio-utils/audio-device-stream.h>
#include <audio-utils/audio-input.h>
#include <audio-utils/audio-output.h>
#include <audio-utils/audio-stream.h>

namespace audio::intel_hda {

SampleCountSink::SampleCountSink(uint32_t samples_to_capture)
    : samples_to_capture_(samples_to_capture) {}

zx_status_t SampleCountSink::SetFormat(const Format& format) {
  format_ = format;
  return ZX_OK;
}

zx_status_t SampleCountSink::PutFrames(const void* buffer, uint32_t bytes) {
  if (!format_.has_value()) {
    std::cerr << "SetFormat() not called.\n";
    return ZX_ERR_BAD_STATE;
  }
  uint32_t frame_size = audio::utils::ComputeFrameSize(format_->channels, format_->sample_format);
  if (bytes % frame_size != 0) {
    std::cerr << "Passed a buffer with a fractional number of samples.\n";
    return ZX_ERR_INVALID_ARGS;
  }

  total_samples_ += bytes / frame_size;

  if (total_samples_ >= samples_to_capture_) {
    return ZX_ERR_STOP;
  }

  return ZX_OK;
}

zx_status_t SampleCountSink::Finalize() { return ZX_OK; }

uint32_t SampleCountSink::total_samples() const { return total_samples_; }

}  // namespace audio::intel_hda

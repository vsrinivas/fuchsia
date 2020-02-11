// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "silence_generator.h"

#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>

#include <audio-proto-utils/format-utils.h>
#include <audio-utils/audio-device-stream.h>
#include <audio-utils/audio-input.h>
#include <audio-utils/audio-output.h>
#include <audio-utils/audio-stream.h>

namespace audio::intel_hda {

SilenceGenerator::SilenceGenerator(const audio::utils::AudioStream::Format& format,
                                   double duration_seconds)
    : format_(format) {
  // Ensure the format is signed or double. This allows "memset(buffer, 0,
  // size)" to produce silence.
  ZX_ASSERT_MSG((format.sample_format & AUDIO_SAMPLE_FORMAT_FLAG_UNSIGNED) == 0,
                "Only signed and double formats are supported.");
  samples_ = static_cast<uint32_t>(format.frame_rate * duration_seconds);
}

zx_status_t SilenceGenerator::GetFormat(Format* out_format) {
  *out_format = format_;
  return ZX_OK;
}

zx_status_t SilenceGenerator::GetFrames(void* buffer, uint32_t buf_space, uint32_t* out_packed) {
  // Return frames of "0" samples.
  uint32_t frame_size = audio::utils::ComputeFrameSize(format_.channels, format_.sample_format);
  uint32_t num_samples = std::min(buf_space / frame_size, samples_);
  memset(buffer, 0, num_samples * frame_size);
  *out_packed = num_samples * frame_size;
  samples_ -= num_samples;
  return ZX_OK;
}

bool SilenceGenerator::finished() const { return samples_ <= 0; }

}  // namespace audio::intel_hda

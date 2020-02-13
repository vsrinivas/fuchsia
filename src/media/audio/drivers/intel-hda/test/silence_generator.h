// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_TEST_SILENCE_GENERATOR_H_
#define SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_TEST_SILENCE_GENERATOR_H_

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

// An AudioSource that simply generates silence on the output.
class SilenceGenerator : public audio::utils::AudioSource {
 public:
  SilenceGenerator(const audio::utils::AudioStream::Format& format, double duration_seconds);

  zx_status_t GetFormat(Format* out_format) override;
  zx_status_t GetFrames(void* buffer, uint32_t buf_space, uint32_t* out_packed) override;
  bool finished() const override;

 private:
  const audio::utils::AudioStream::Format format_;  // Output format.
  uint32_t samples_;                                // Number of samples left to produce.
};

}  // namespace audio::intel_hda

#endif  // SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_TEST_SILENCE_GENERATOR_H_

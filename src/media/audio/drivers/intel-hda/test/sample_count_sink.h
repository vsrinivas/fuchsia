// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_TEST_SAMPLE_COUNT_SINK_H_
#define SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_TEST_SAMPLE_COUNT_SINK_H_

#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <optional>

#include <audio-proto-utils/format-utils.h>
#include <audio-utils/audio-device-stream.h>
#include <audio-utils/audio-input.h>
#include <audio-utils/audio-output.h>
#include <audio-utils/audio-stream.h>

namespace audio::intel_hda {

// An AudioSink that counts the number of received samples.
class SampleCountSink : public audio::utils::AudioSink {
 public:
  // Stop capturing after receiving |samples_to_capture| samples.
  explicit SampleCountSink(uint32_t samples_to_capture);

  // Return the total number of samples recorded.
  uint32_t total_samples() const;

  // Implementation of AudioSink.
  zx_status_t SetFormat(const Format& format) override;
  zx_status_t PutFrames(const void* buffer, uint32_t bytes) override;
  zx_status_t Finalize() override;

 private:
  const uint32_t samples_to_capture_ = 0;
  uint32_t total_samples_ = 0;
  std::optional<audio::utils::AudioStream::Format> format_;  // Input format.
};

}  // namespace audio::intel_hda

#endif  // SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_TEST_SAMPLE_COUNT_SINK_H_

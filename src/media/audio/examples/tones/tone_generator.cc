// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/examples/tones/tone_generator.h"

#include <cmath>

namespace examples {

ToneGenerator::ToneGenerator(uint32_t frames_per_second, float frequency, float volume, float decay)
    : frames_per_second_(frames_per_second),
      frequency_(frequency),
      volume_(volume),
      decay_factor_(pow(1.0f - decay, 1.0f / frames_per_second)),
      real_sample_(0.0f),
      imaginary_sample_(1.0f) {}

void ToneGenerator::MixSamples(float* dest, uint32_t frame_count, uint32_t channel_count) {
  // We're using the 'slope iteration method' here to avoid calling |sin| for
  // every sample or having to build a lookup table. While this method is
  // theoretically correct, rounding errors will cause the resulting wave to
  // deviate from the results we would get using |sin|. We get the best results
  // when the wave frequency is much lower than the sample frequency. Given
  // that we're producing a low-frequency transient tone with decay, the results
  // are reasonable.
  //
  // The principle is that |realSample| and |imaginarySample| are x and y
  // values on a unit circle centered on the origin. We start with 0,1 and
  // rotate the point slightly around the origin for each sample. We use only
  // the real values, which we scale to get the desired amplitude.
  float constant = (2.0f * M_PI * frequency_) / frames_per_second_;
  float realSample = real_sample_;
  float imaginarySample = imaginary_sample_;
  float volume = volume_;

  for (uint32_t i = 0; i < frame_count; i++) {
    // Note that we're only producing one channel here, as descibed in the
    // documentation for the method.
    *dest += realSample * volume;

    // Rotate |realSample|,|imaginarySample| around the origin.
    realSample -= imaginarySample * constant;
    imaginarySample += realSample * constant;

    volume = volume * decay_factor_;
    dest += channel_count;
  }

  // Capture these values so we pick up where we left off.
  real_sample_ = realSample;
  imaginary_sample_ = imaginarySample;
  volume_ = volume;
}

}  // namespace examples

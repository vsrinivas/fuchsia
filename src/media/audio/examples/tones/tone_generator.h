// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_EXAMPLES_TONES_TONE_GENERATOR_H_
#define SRC_MEDIA_AUDIO_EXAMPLES_TONES_TONE_GENERATOR_H_

#include <cstdint>

namespace examples {

// Generates a single channel of tone in FLOAT format.
class ToneGenerator {
 public:
  // Constructs a tone generator that produces a tone at |frequency| hertz.
  // A |volume| values in excess of 1.0 are likely to produce distortion.
  // |decay| specifies the factor by which volume should be reduced in one
  // second. A |decay| value of 0.0 produces a constant tone. A |decay| value
  // of 0.9 reducees the volume 90% (to 10%) in one second.
  ToneGenerator(uint32_t frames_per_second, float frequency, float volume, float decay);

  // Mixes |frame_count| samples into |dest|, summing the first sample into
  // |*dest|, the next into |*(dest + channel_count)|, etc. |channel_count| is
  // used for stride only. Only one channel of audio is generated.
  void MixSamples(float* dest, uint32_t frame_count, uint32_t channel_count);

  // Returns the volume, subject to decay.
  float volume() { return volume_; }

 private:
  uint32_t frames_per_second_;
  float frequency_;
  float volume_;
  float decay_factor_;
  float real_sample_;
  float imaginary_sample_;
};

}  // namespace examples

#endif  // SRC_MEDIA_AUDIO_EXAMPLES_TONES_TONE_GENERATOR_H_

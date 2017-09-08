// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio/test/test_utils.h"

namespace media {
namespace test {

template <>
float SilentSamples(uint32_t channel, int64_t pts) {
  return 0.0f;
}

template <>
float DistinctSamples(uint32_t channel, int64_t pts) {
  return float(pts) / float(1 + channel);
}

template <>
SampleFunc<float> FadeSamples(int64_t from_pts,
                              float from_sample,
                              int64_t to_pts,
                              float to_sample) {
  return [=](uint32_t channel, int64_t pts) {
    return from_sample + (to_sample - from_sample) *
                             (float(pts) - float(from_pts)) /
                             (float(to_pts) - float(from_pts));
  };
}

bool RoughlyEquals(float a, float b, float epsilon) {
  return std::abs(a - b) < epsilon;
}

template <>
void VerifyBuffer(float* buffer,
                  uint32_t channel_count,
                  uint32_t frame_count,
                  int64_t first_pts,
                  const SampleFunc<float>& sample_func) {
  bool all_samples_verified = true;
  for (uint32_t frame_index = 0; frame_index < frame_count; ++frame_index) {
    for (uint32_t channel = 0; channel < channel_count; ++channel) {
      if (!RoughlyEquals(sample_func(channel, first_pts + frame_index),
                         buffer[channel_count * frame_index + channel])) {
        FTL_LOG(WARNING) << "VerifyBuffer expected "
                         << sample_func(channel, first_pts + frame_index)
                         << " got "
                         << buffer[channel_count * frame_index + channel]
                         << " on channel " << channel << " at pts "
                         << first_pts + frame_index;
        all_samples_verified = false;
      }
    }
  }

  EXPECT_TRUE(all_samples_verified);
}

}  // namespace test
}  // namespace media

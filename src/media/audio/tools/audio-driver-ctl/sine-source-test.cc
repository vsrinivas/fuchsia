// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sine-source.h"

#include <unordered_map>

#include <zxtest/zxtest.h>

namespace {

TEST(SineSourceTest, SingleWave) {
  const int kNumSamples = 100;
  SineSource source;
  audio::utils::Duration duration = 1.0f;  // Seconds.
  ASSERT_OK(source.Init(/*freq=*/1.0, /*amp=*/1.0, /*duration=*/duration,
                        /*frame_rate=*/kNumSamples,
                        /*channels=*/1, /*active=*/SineSource::kAllChannelsActive,
                        /*sample_format=*/AUDIO_SAMPLE_FORMAT_32BIT));
  EXPECT_FALSE(source.finished());

  // Fetch some samples.
  int32_t buffer[kNumSamples];
  uint32_t samples_produced;
  ASSERT_OK(source.GetFrames(&buffer, sizeof(buffer), &samples_produced));
  ASSERT_EQ(samples_produced, kNumSamples * sizeof(int32_t));

  // For a single, full sine wave, we don't expect to see the same value more than twice.
  std::unordered_map<int32_t, int> value_counts;
  for (size_t i = 0; i < kNumSamples; i++) {
    int& val = value_counts[buffer[i]];
    EXPECT_LT(val, 2);
    val++;
  }

  // Expect the stream to be finished.
  EXPECT_TRUE(source.finished());
}

}  // namespace

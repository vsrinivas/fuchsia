// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "noise-source.h"

#include <zxtest/zxtest.h>

namespace {

TEST(NoiseSourceTest, RandomContent) {
  constexpr int kNumFrames = 100;
  constexpr int kNumChannels = 4;
  NoiseSource source;
  ASSERT_OK(source.Init(/*freq=*/1.0, /*amp=*/1.0, /*duration_secs=*/1.0,
                        /*frame_rate=*/kNumFrames,
                        /*channels=*/kNumChannels, /*active=*/GeneratedSource::kAllChannelsActive,
                        /*sample_format=*/AUDIO_SAMPLE_FORMAT_32BIT));
  EXPECT_FALSE(source.finished());

  // Fetch some samples.
  int32_t buffer[kNumFrames * kNumChannels];
  uint32_t bytes_produced;
  ASSERT_OK(source.GetFrames(&buffer, sizeof(buffer), &bytes_produced));
  ASSERT_EQ(bytes_produced, kNumFrames * kNumChannels * sizeof(int32_t));

  // Across every frame, we would not expect all channels to have identical values.
  bool all_channels_not_identical = false;
  for (size_t i = 0; i < kNumFrames * kNumChannels; i += kNumChannels) {
    for (auto chan = 1; chan < kNumChannels; ++chan) {
      if (buffer[i] != buffer[i + chan]) {
        all_channels_not_identical = true;
        break;
      }
    }
  }
  EXPECT_TRUE(all_channels_not_identical);

  // Expect the stream to be finished.
  EXPECT_TRUE(source.finished());
}

}  // namespace

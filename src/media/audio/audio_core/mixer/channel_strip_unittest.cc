// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/channel_strip.h"

#include <gtest/gtest.h>

namespace media::audio {
namespace {

void ValidateConstruction(mixer::ChannelStrip* strip, int32_t num_channels, int32_t length) {
  ASSERT_NE(strip, nullptr);
  ASSERT_GT(num_channels, 0);
  ASSERT_GT(length, 0);

  EXPECT_EQ(strip->num_channels(), num_channels);
  EXPECT_EQ(strip->length(), length);

  for (auto chan = 0; chan < num_channels; ++chan) {
    EXPECT_EQ((*strip)[chan].size(), static_cast<size_t>(length));
    for (auto idx = 0; idx < length; ++idx) {
      EXPECT_EQ((*strip)[chan][idx], 0.0f);
    }
  }
}

// Validate ctor default and parameters
TEST(ChannelStripTest, Construction) {
  mixer::ChannelStrip data;
  auto num_chans = 1;
  auto strip_len = 1;
  ValidateConstruction(&data, num_chans, strip_len);

  num_chans = 1;
  strip_len = 3;
  mixer::ChannelStrip data2(num_chans, strip_len);
  ValidateConstruction(&data2, num_chans, strip_len);

  num_chans = 4;
  strip_len = 2;
  mixer::ChannelStrip data3(num_chans, strip_len);
  ValidateConstruction(&data3, num_chans, strip_len);
}

// Sanity test for [] operator
TEST(ChannelStripTest, SetValues) {
  mixer::ChannelStrip data(2, 3);

  auto& channel_0 = data[0];
  auto& channel_0_watcher = data[0];
  auto& channel_1 = data[1];
  auto& channel_1_watcher = data[1];
  auto& data_watcher = data;

  data[0][0] = 1.0f;
  channel_0[1] = 2.0f;
  channel_1[0] = 3.0f;
  data[1][1] = 4.0f;

  EXPECT_EQ(data_watcher[0][0], 1.0f);
  EXPECT_EQ(channel_0_watcher[1], 2.0f);
  EXPECT_EQ(channel_1_watcher[0], 3.0f);
  EXPECT_EQ(data_watcher[1][1], 4.0f);
}

// Clear should zero-out the entire length of each chan, regardless of num_chans/length.
TEST(ChannelStripTest, Clear) {
  mixer::ChannelStrip data(2, 2);
  auto& channel_1 = data[1];

  data[0][0] = 1.0f;
  data[0][1] = 2.0f;
  channel_1[0] = -3.0f;
  channel_1[1] = -4.0f;

  data.Clear();

  EXPECT_EQ(data[0][0], 0.0f);
  EXPECT_EQ(data[0][1], 0.0f);
  EXPECT_EQ(data[1][0], 0.0f);
  EXPECT_EQ(data[1][1], 0.0f);
  EXPECT_EQ(channel_1[0], 0.0f);
  EXPECT_EQ(channel_1[1], 0.0f);
}

// Test shifting by various conditions. Shift-by-0 should lead to no change. Shift-by-length (or
// more) should clear the strip: all values are shifted out, replaced by shifted-in zeroes.
TEST(ChannelStripTest, ShiftBy) {
  mixer::ChannelStrip data(2, 2);

  data[0][0] = 1.0f;
  data[0][1] = 2.0f;

  data[1][0] = -1.0f;
  data[1][1] = -2.0f;

  // Shift by 0 -- channels unchanged
  data.ShiftBy(0);

  EXPECT_EQ(data[0][0], 1.0f);
  EXPECT_EQ(data[0][1], 2.0f);
  EXPECT_EQ(data[1][0], -1.0f);
  EXPECT_EQ(data[1][1], -2.0f);

  // Shift by 1 - channels shift left, adding a zero
  data.ShiftBy(1);

  EXPECT_EQ(data[0][0], 2.0f);
  EXPECT_EQ(data[0][1], 0.0f);
  EXPECT_EQ(data[1][0], -2.0f);
  EXPECT_EQ(data[1][1], 0.0f);

  // Shift by 2 (len_), all data "shifted out", entirely zero
  data[0][1] = 3.0f;
  data[1][1] = -3.0f;
  data.ShiftBy(2);

  EXPECT_EQ(data[0][0], 0.0f);
  EXPECT_EQ(data[0][1], 0.0f);
  EXPECT_EQ(data[1][0], 0.0f);
  EXPECT_EQ(data[1][1], 0.0f);

  // Shift by 3 (more than len_), allowed and same as shifting by len_ (all zeros)
  data[0][0] = 4.0f;
  data[0][1] = 5.0f;
  data[1][0] = -4.0f;
  data[1][1] = -5.0f;
  data.ShiftBy(3);

  EXPECT_EQ(data[0][0], 0.0f);
  EXPECT_EQ(data[0][1], 0.0f);
  EXPECT_EQ(data[1][0], 0.0f);
  EXPECT_EQ(data[1][1], 0.0f);
}

}  // namespace
}  // namespace media::audio

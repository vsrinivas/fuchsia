// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/processing/channel_strip.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace media_audio {
namespace {

using ::testing::AllOf;
using ::testing::Each;
using ::testing::SizeIs;

void ValidateConstruction(const ChannelStrip& strip, int64_t channel_count, int64_t frame_count) {
  ASSERT_GT(channel_count, 0);
  ASSERT_GT(frame_count, 0);

  EXPECT_EQ(strip.channel_count(), channel_count);
  EXPECT_EQ(strip.frame_count(), frame_count);
  for (auto chan = 0; chan < channel_count; ++chan) {
    EXPECT_THAT(strip[chan], AllOf(SizeIs(frame_count), Each(0.0f)));
  }
}

TEST(ChannelStripTest, Construction) {
  auto num_chans = 1;
  auto strip_len = 1;
  ChannelStrip data(num_chans, strip_len);
  ValidateConstruction(data, num_chans, strip_len);

  num_chans = 1;
  strip_len = 3;
  ChannelStrip data2(num_chans, strip_len);
  ValidateConstruction(data2, num_chans, strip_len);

  num_chans = 4;
  strip_len = 2;
  ChannelStrip data3(num_chans, strip_len);
  ValidateConstruction(data3, num_chans, strip_len);
}

TEST(ChannelStripTest, SetValues) {
  ChannelStrip data(2, 3);
  ValidateConstruction(data, 2, 3);

  auto channel_0 = data[0];
  auto channel_1 = data[1];

  data[0][0] = 1.0f;
  channel_0[1] = 2.0f;
  channel_1[0] = 3.0f;
  data[1][1] = 4.0f;

  EXPECT_FLOAT_EQ(channel_0[1], 2.0f);
  EXPECT_FLOAT_EQ(channel_1[0], 3.0f);
  EXPECT_FLOAT_EQ(data[0][0], 1.0f);
  EXPECT_FLOAT_EQ(data[0][1], 2.0f);
  EXPECT_FLOAT_EQ(data[1][0], 3.0f);
  EXPECT_FLOAT_EQ(data[1][1], 4.0f);
}

TEST(ChannelStripTest, Clear) {
  ChannelStrip data(2, 2);
  ValidateConstruction(data, 2, 2);

  auto channel_1 = data[1];

  data[0][0] = 1.0f;
  data[0][1] = 2.0f;
  channel_1[0] = -3.0f;
  channel_1[1] = -4.0f;

  EXPECT_FLOAT_EQ(channel_1[0], -3.0f);
  EXPECT_FLOAT_EQ(channel_1[1], -4.0f);
  EXPECT_FLOAT_EQ(data[0][0], 1.0f);
  EXPECT_FLOAT_EQ(data[0][1], 2.0f);
  EXPECT_FLOAT_EQ(data[1][0], -3.0f);
  EXPECT_FLOAT_EQ(data[1][1], -4.0f);

  data.Clear();

  EXPECT_FLOAT_EQ(channel_1[0], 0.0f);
  EXPECT_FLOAT_EQ(channel_1[1], 0.0f);
  EXPECT_FLOAT_EQ(data[0][0], 0.0f);
  EXPECT_FLOAT_EQ(data[0][1], 0.0f);
  EXPECT_FLOAT_EQ(data[1][0], 0.0f);
  EXPECT_FLOAT_EQ(data[1][1], 0.0f);
}

TEST(ChannelStripTest, ShiftBy) {
  ChannelStrip data(2, 2);
  ValidateConstruction(data, 2, 2);

  data[0][0] = 1.0f;
  data[0][1] = 2.0f;
  data[1][0] = -1.0f;
  data[1][1] = -2.0f;

  EXPECT_FLOAT_EQ(data[0][0], 1.0f);
  EXPECT_FLOAT_EQ(data[0][1], 2.0f);
  EXPECT_FLOAT_EQ(data[1][0], -1.0f);
  EXPECT_FLOAT_EQ(data[1][1], -2.0f);

  // Shift by 0, which should have no effect.
  data.ShiftBy(0);

  EXPECT_FLOAT_EQ(data[0][0], 1.0f);
  EXPECT_FLOAT_EQ(data[0][1], 2.0f);
  EXPECT_FLOAT_EQ(data[1][0], -1.0f);
  EXPECT_FLOAT_EQ(data[1][1], -2.0f);

  // Shift by 1, which should shift channels left, adding a single zero at the end.
  data.ShiftBy(1);

  EXPECT_FLOAT_EQ(data[0][0], 2.0f);
  EXPECT_FLOAT_EQ(data[0][1], 0.0f);
  EXPECT_FLOAT_EQ(data[1][0], -2.0f);
  EXPECT_FLOAT_EQ(data[1][1], 0.0f);

  // Reset shifted data, and shift by frame count, which should entirely zero out all channels.
  data[0][1] = 3.0f;
  data[1][1] = -3.0f;
  data.ShiftBy(2);

  EXPECT_FLOAT_EQ(data[0][0], 0.0f);
  EXPECT_FLOAT_EQ(data[0][1], 0.0f);
  EXPECT_FLOAT_EQ(data[1][0], 0.0f);
  EXPECT_FLOAT_EQ(data[1][1], 0.0f);

  // Reset shifted data, and shift by more than frame count, which should again entirely zero out
  // all channels.
  data[0][0] = 4.0f;
  data[0][1] = 5.0f;
  data[1][0] = -4.0f;
  data[1][1] = -5.0f;
  data.ShiftBy(3);

  EXPECT_FLOAT_EQ(data[0][0], 0.0f);
  EXPECT_FLOAT_EQ(data[0][1], 0.0f);
  EXPECT_FLOAT_EQ(data[1][0], 0.0f);
  EXPECT_FLOAT_EQ(data[1][1], 0.0f);
}

}  // namespace
}  // namespace media_audio

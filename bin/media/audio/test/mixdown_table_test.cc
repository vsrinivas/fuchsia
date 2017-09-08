// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio/mixdown_table.h"

#include "gtest/gtest.h"

namespace media {
namespace test {

// Tests expectations regarding silent mixdown tables.
TEST(MixdownTableTest, Silent) {
  static constexpr uint32_t kInChannelCount = 3;
  static constexpr uint32_t kOutChannelCount = 4;

  std::unique_ptr<MixdownTable<float>> under_test =
      MixdownTable<float>::CreateSilent(kInChannelCount, kOutChannelCount);

  EXPECT_EQ(kInChannelCount, under_test->in_channel_count());
  EXPECT_EQ(kOutChannelCount, under_test->out_channel_count());

  for (uint32_t out_channel = 0; out_channel < kOutChannelCount;
       ++out_channel) {
    for (uint32_t in_channel = 0; in_channel < kInChannelCount; ++in_channel) {
      // Level is initially silent.
      EXPECT_EQ(Level<float>::Silence,
                under_test->get_level(in_channel, out_channel));

      // When set to unity, the level remains at unity.
      under_test->get_level(in_channel, out_channel) = Level<float>::Unity;
      EXPECT_EQ(Level<float>::Unity,
                under_test->get_level(in_channel, out_channel));
    }
  }
}

// Tests expectations regarding level change mixdown tables.
TEST(MixdownTableTest, LevelChange) {
  static constexpr uint32_t kChannels = 3;
  static const Level<float> kLevel = Level<float>(2.0f);

  std::unique_ptr<MixdownTable<float>> under_test =
      MixdownTable<float>::CreateLevelChange(kChannels, kLevel);

  EXPECT_EQ(kChannels, under_test->in_channel_count());
  EXPECT_EQ(kChannels, under_test->out_channel_count());

  for (uint32_t out_channel = 0; out_channel < kChannels; ++out_channel) {
    for (uint32_t in_channel = 0; in_channel < kChannels; ++in_channel) {
      if (out_channel == in_channel) {
        // Levels on the diagonal are kLevel.
        EXPECT_EQ(kLevel, under_test->get_level(in_channel, out_channel));
      } else {
        // Levels not the diagonal are silent.
        EXPECT_EQ(Level<float>::Silence,
                  under_test->get_level(in_channel, out_channel));
      }
    }
  }
}

// Tests expectations regarding passthrough mixdown tables.
TEST(MixdownTableTest, Passthrough) {
  static constexpr uint32_t kChannels = 3;

  std::unique_ptr<MixdownTable<float>> under_test =
      MixdownTable<float>::CreatePassthrough(kChannels);

  EXPECT_EQ(kChannels, under_test->in_channel_count());
  EXPECT_EQ(kChannels, under_test->out_channel_count());

  for (uint32_t out_channel = 0; out_channel < kChannels; ++out_channel) {
    for (uint32_t in_channel = 0; in_channel < kChannels; ++in_channel) {
      if (out_channel == in_channel) {
        // Levels on the diagonal are unity.
        EXPECT_EQ(Level<float>::Unity,
                  under_test->get_level(in_channel, out_channel));
      } else {
        // Levels not the diagonal are silent.
        EXPECT_EQ(Level<float>::Silence,
                  under_test->get_level(in_channel, out_channel));
      }
    }
  }
}

// Test that simple increment can be used to enumerate levels.
TEST(MixdownTableTest, Iteration) {
  static constexpr uint32_t kInChannelCount = 7;
  static constexpr uint32_t kOutChannelCount = 2;

  std::unique_ptr<MixdownTable<float>> under_test =
      MixdownTable<float>::CreateSilent(kInChannelCount, kOutChannelCount);

  EXPECT_EQ(kInChannelCount, under_test->in_channel_count());
  EXPECT_EQ(kOutChannelCount, under_test->out_channel_count());

  auto iter = under_test->begin();

  for (uint32_t out_channel = 0; out_channel < kOutChannelCount;
       ++out_channel) {
    for (uint32_t in_channel = 0; in_channel < kInChannelCount; ++in_channel) {
      // Level is initially silent.
      EXPECT_EQ(&under_test->get_level(in_channel, out_channel), &(*iter));
      ++iter;
    }
  }

  EXPECT_EQ(under_test->end(), iter);
}

}  // namespace test
}  // namespace media

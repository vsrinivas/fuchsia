// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/format2/channel_mapper.h"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "src/media/audio/lib/format2/sample_converter.h"

namespace media_audio {
namespace {

TEST(ChannelMapperTest, SameChannels) {
  ChannelMapper<int16_t, 4, 4> mapper;

  const std::vector<int16_t> source_frame = {-0x4000, kMinInt16, 0, 0x4000};
  const std::vector<float> expected = {-0.5f, -1.0f, 0.0f, 0.5f};
  for (size_t channel = 0; channel < expected.size(); ++channel) {
    EXPECT_FLOAT_EQ(mapper.Map(source_frame.data(), channel), expected[channel]);
  }
}

TEST(ChannelMapperTest, MonoToStereo) {
  ChannelMapper<int16_t, 1, 2> mapper;

  const int16_t source_frame = 0x4000;  // 0.5f
  for (size_t channel = 0; channel < 2; ++channel) {
    EXPECT_FLOAT_EQ(mapper.Map(&source_frame, 0), 0.5f);
  }
}

TEST(ChannelMapperTest, MonoToThreeChannels) {
  ChannelMapper<uint8_t, 1, 3> mapper;

  const uint8_t source_frame = 0x40;  // -0.5f
  for (size_t channel = 0; channel < 3; ++channel) {
    EXPECT_FLOAT_EQ(mapper.Map(&source_frame, 0), -0.5f);
  }
}

TEST(ChannelMapperTest, MonoToFourChannels) {
  ChannelMapper<float, 1, 4> mapper;

  const float source_frame = 0.2f;
  for (size_t channel = 0; channel < 4; ++channel) {
    EXPECT_FLOAT_EQ(mapper.Map(&source_frame, 0), 0.2f);
  }
}

TEST(ChannelMapperTest, StereoToMono) {
  ChannelMapper<int16_t, 2, 1> mapper;

  const std::vector<int16_t> source_frame = {-0x2000, -0x4000};  // {-0.25f, -0.5f}
  EXPECT_FLOAT_EQ(mapper.Map(source_frame.data(), 0), -0.375f);
}

TEST(ChannelMapperTest, StereoToThreeChannels) {
  ChannelMapper<float, 2, 3> mapper;

  const std::vector<float> source_frame = {-0.25f, 0.75f};
  const std::vector<float> expected = {-0.25f, 0.75f, 0.25f};
  for (size_t channel = 0; channel < expected.size(); ++channel) {
    EXPECT_FLOAT_EQ(mapper.Map(source_frame.data(), channel), expected[channel]);
  }
}

TEST(ChannelMapperTest, StereoToFourChannels) {
  ChannelMapper<float, 2, 4> mapper;

  const std::vector<float> source_frame = {-0.25f, 0.75f};
  const std::vector<float> expected = {-0.25f, 0.75f, -0.25f, 0.75f};
  for (size_t channel = 0; channel < expected.size(); ++channel) {
    EXPECT_FLOAT_EQ(mapper.Map(source_frame.data(), channel), expected[channel]);
  }
}

TEST(ChannelMapperTest, ThreeChannelsToMono) {
  ChannelMapper<float, 3, 1> mapper;

  const std::vector<float> source_frame = {-1.0f, 0.5f, -0.1f};
  EXPECT_FLOAT_EQ(mapper.Map(source_frame.data(), 0), -0.2f);
}

TEST(ChannelMapperTest, ThreeChannelsToStereo) {
  ChannelMapper<float, 3, 2> mapper;

  const std::vector<float> source_frame = {1.0f, -0.5f, -0.5f};
  const std::vector<float> expected = {0.378679656f, -0.5f};
  for (size_t channel = 0; channel < expected.size(); ++channel) {
    EXPECT_FLOAT_EQ(mapper.Map(source_frame.data(), channel), expected[channel]);
  }
}

TEST(ChannelMapperTest, FourChannelsToMono) {
  ChannelMapper<float, 4, 1> mapper;

  const std::vector<float> source_frame = {1.0f, 2.0f, 3.0f, 4.0f};
  if constexpr (kEnable4ChannelWorkaround) {
    EXPECT_FLOAT_EQ(mapper.Map(source_frame.data(), 0), 1.5f);
  } else {
    EXPECT_FLOAT_EQ(mapper.Map(source_frame.data(), 0), 5.0f);
  }
}

TEST(ChannelMapperTest, FourChannelsToStereo) {
  ChannelMapper<float, 4, 2> mapper;

  const std::vector<float> source_frame = {1.0f, 2.0f, 3.0f, 4.0f};
  if constexpr (kEnable4ChannelWorkaround) {
    EXPECT_FLOAT_EQ(mapper.Map(source_frame.data(), 0), 1.0f);
    EXPECT_FLOAT_EQ(mapper.Map(source_frame.data(), 1), 2.0f);
  } else {
    EXPECT_FLOAT_EQ(mapper.Map(source_frame.data(), 0), 1.5f);
    EXPECT_FLOAT_EQ(mapper.Map(source_frame.data(), 1), 3.0f);
  }
}

TEST(ChannelMapperTest, CustomizableSameChannels) {
  ChannelMapper<float, 2, 2, /*Customizable=*/true> mapper({{
      {-1.0f, 0.0f},
      {0.5f, 0.5f},
  }});

  const std::vector<float> source_frame = {2.0f, 3.0f};
  const std::vector<float> expected = {-2.0f, 2.5f};
  for (size_t channel = 0; channel < expected.size(); ++channel) {
    EXPECT_FLOAT_EQ(mapper.Map(source_frame.data(), channel), expected[channel]);
  }
}

TEST(ChannelMapperTest, CustomizableSingleToMulti) {
  ChannelMapper<float, 1, 3, /*Customizable=*/true> mapper({{{1.0f}, {-2.0f}, {3.0f}}});

  const std::vector<float> source_frame = {0.5f};
  const std::vector<float> expected = {0.5f, -1.0f, 1.5f};
  for (size_t channel = 0; channel < expected.size(); ++channel) {
    EXPECT_FLOAT_EQ(mapper.Map(source_frame.data(), channel), expected[channel]);
  }
}

TEST(ChannelMapperTest, CustomizableMultiToSingle) {
  ChannelMapper<float, 2, 1, /*Customizable=*/true> mapper({{{1.0f, 0.25f}}});

  const std::vector<float> source_frame = {2.0f, 4.0f};
  EXPECT_FLOAT_EQ(mapper.Map(source_frame.data(), 0), 3.0f);
}

TEST(ChannelMapperTest, CustomizableMultiToMulti) {
  ChannelMapper<float, 3, 5, /*Customizable=*/true> mapper({{
      {1.0f, 0.0f, 0.0f},
      {0.0f, 1.0f, 0.0f},
      {0.0f, 0.0f, 1.0f},
      {1.0f, 1.0f, 1.0f},
      {-1.0f, 2.0f, -3.0f},
  }});

  const std::vector<float> source_frame = {1.0f, 2.0f, 3.0f};
  const std::vector<float> expected = {1.0f, 2.0f, 3.0f, 6.0f, -6.0f};
  for (size_t channel = 0; channel < expected.size(); ++channel) {
    EXPECT_FLOAT_EQ(mapper.Map(source_frame.data(), channel), expected[channel]);
  }
}

}  // namespace
}  // namespace media_audio

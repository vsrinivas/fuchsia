// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/channel_attributes.h"

#include <fuchsia/media/cpp/fidl.h>

#include <vector>

#include <gtest/gtest.h>

namespace media::audio {
namespace {

constexpr uint32_t kTestAudibleFrequency = 2000;
constexpr uint32_t kTestUltrasonicFrequency = 27000;

// Return true if it overlaps the audible range at all. Must include more than just boundary value.
TEST(ChannelAttributesTest, ChannelIncludesAudible) {
  EXPECT_FALSE(ChannelAttributes().IncludesAudible());

  EXPECT_FALSE(ChannelAttributes(0, 0).IncludesAudible());
  EXPECT_FALSE(ChannelAttributes(ChannelAttributes::kAudibleUltrasonicBoundaryHz,
                                 fuchsia::media::MAX_PCM_FRAMES_PER_SECOND)
                   .IncludesAudible());

  EXPECT_TRUE(ChannelAttributes(0, 1).IncludesAudible());
  EXPECT_TRUE(ChannelAttributes(ChannelAttributes::kAudibleUltrasonicBoundaryHz - 1,
                                ChannelAttributes::kAudibleUltrasonicBoundaryHz)
                  .IncludesAudible());
  EXPECT_TRUE(ChannelAttributes(kTestAudibleFrequency, kTestAudibleFrequency).IncludesAudible());
}

// Return true if it overlaps ultrasonic range. Must include more than just the boundary value.
TEST(ChannelAttributesTest, ChannelIncludesUltrasonic) {
  EXPECT_FALSE(ChannelAttributes().IncludesUltrasonic());

  EXPECT_FALSE(
      ChannelAttributes(0, ChannelAttributes::kAudibleUltrasonicBoundaryHz).IncludesUltrasonic());
  EXPECT_FALSE(ChannelAttributes(fuchsia::media::MAX_PCM_FRAMES_PER_SECOND / 2,
                                 fuchsia::media::MAX_PCM_FRAMES_PER_SECOND)
                   .IncludesUltrasonic());

  EXPECT_TRUE(ChannelAttributes(0, ChannelAttributes::kAudibleUltrasonicBoundaryHz + 1)
                  .IncludesUltrasonic());
  EXPECT_TRUE(ChannelAttributes(fuchsia::media::MAX_PCM_FRAMES_PER_SECOND / 2 - 1,
                                fuchsia::media::MAX_PCM_FRAMES_PER_SECOND)
                  .IncludesUltrasonic());
  EXPECT_TRUE(
      ChannelAttributes(kTestUltrasonicFrequency, kTestUltrasonicFrequency).IncludesUltrasonic());
}

// If any channel includes any of the range, then the channel set supports audible
TEST(ChannelAttributesTest, VectorIncludesAudible) {
  std::vector<ChannelAttributes> channel_attribs;
  EXPECT_FALSE(ChannelAttributes::IncludesAudible(channel_attribs));

  channel_attribs.push_back({0, 0});
  channel_attribs.push_back(
      {ChannelAttributes::kAudibleUltrasonicBoundaryHz, fuchsia::media::MAX_PCM_FRAMES_PER_SECOND});
  EXPECT_FALSE(ChannelAttributes::IncludesAudible(channel_attribs));

  channel_attribs.push_back({kTestAudibleFrequency, kTestAudibleFrequency});
  EXPECT_TRUE(ChannelAttributes::IncludesAudible(channel_attribs));
}

// The set of channels must cover the entire ultrasonic range, contiguously
TEST(ChannelAttributesTest, VectorIncludesUltrasonic) {
  std::vector<ChannelAttributes> channel_attribs;
  EXPECT_FALSE(ChannelAttributes::IncludesUltrasonic(channel_attribs));

  channel_attribs.push_back({0, ChannelAttributes::kAudibleUltrasonicBoundaryHz});
  channel_attribs.push_back(
      {fuchsia::media::MAX_PCM_FRAMES_PER_SECOND / 2, fuchsia::media::MAX_PCM_FRAMES_PER_SECOND});
  EXPECT_FALSE(ChannelAttributes::IncludesUltrasonic(channel_attribs));

  channel_attribs.push_back({kTestUltrasonicFrequency, kTestUltrasonicFrequency});
  EXPECT_TRUE(ChannelAttributes::IncludesUltrasonic(channel_attribs));
}

}  // namespace
}  // namespace media::audio

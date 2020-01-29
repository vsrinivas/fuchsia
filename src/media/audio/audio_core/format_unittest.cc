// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/format.h"

#include <gtest/gtest.h>

namespace media::audio {
namespace {

TEST(FormatTest, OperatorEquals) {
  Format format1 = Format(fuchsia::media::AudioStreamType{
      .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
      .channels = 2,
      .frames_per_second = 48000,
  });
  Format format2 = Format(fuchsia::media::AudioStreamType{
      .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
      .channels = 2,
      .frames_per_second = 48000,
  });

  EXPECT_EQ(format1, format2);
}

TEST(FormatTest, OperatorEqualsDifferentChannels) {
  Format format1 = Format(fuchsia::media::AudioStreamType{
      .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
      .channels = 2,
      .frames_per_second = 48000,
  });
  Format format2 = Format(fuchsia::media::AudioStreamType{
      .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
      .channels = 1,
      .frames_per_second = 48000,
  });

  EXPECT_NE(format1, format2);
}

TEST(FormatTest, OperatorEqualsDifferentRates) {
  Format format1 = Format(fuchsia::media::AudioStreamType{
      .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
      .channels = 2,
      .frames_per_second = 48000,
  });
  Format format2 = Format(fuchsia::media::AudioStreamType{
      .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
      .channels = 2,
      .frames_per_second = 96000,
  });

  EXPECT_NE(format1, format2);
}

TEST(FormatTest, OperatorEqualsDifferentSampleFormats) {
  Format format1 = Format(fuchsia::media::AudioStreamType{
      .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
      .channels = 2,
      .frames_per_second = 48000,
  });
  Format format2 = Format(fuchsia::media::AudioStreamType{
      .sample_format = fuchsia::media::AudioSampleFormat::UNSIGNED_8,
      .channels = 2,
      .frames_per_second = 48000,
  });

  EXPECT_NE(format1, format2);
}

}  // namespace
}  // namespace media::audio

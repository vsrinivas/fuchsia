// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/media/vnext/lib/formats/audio_format.h"

namespace fmlib {
namespace {

constexpr uint64_t kChannelCount = 3;
constexpr uint32_t kFramesPerSecond = 48000;
constexpr uint64_t kUint8SampleSize = 1;
constexpr uint64_t kSigned16SampleSize = 2;
constexpr uint64_t kSigned24In32SampleSize = 4;
constexpr uint64_t kSigned32SampleSize = 4;
constexpr uint64_t kFloatSampleSize = 4;
constexpr zx::duration kDuration = zx::sec(7);

// Tests the |bytes_per_sample|, |bytes_per_frame|, |frames_per| and |bytes_per| methods.
TEST(AudioFormatTests, Sizes) {
  {
    AudioFormat under_test(fuchsia::mediastreams::AudioSampleFormat::UNSIGNED_8, kChannelCount,
                           kFramesPerSecond, nullptr, nullptr);
    EXPECT_EQ(kUint8SampleSize, under_test.bytes_per_sample());
    EXPECT_EQ(kUint8SampleSize * kChannelCount, under_test.bytes_per_frame());
    EXPECT_EQ(static_cast<uint64_t>((kFramesPerSecond * kDuration.get()) / zx::sec(1).get()),
              under_test.frames_per(kDuration));
    EXPECT_EQ(kUint8SampleSize * kChannelCount *
                  static_cast<uint64_t>((kFramesPerSecond * kDuration.get()) / zx::sec(1).get()),
              under_test.bytes_per(kDuration));
  }

  {
    AudioFormat under_test(fuchsia::mediastreams::AudioSampleFormat::SIGNED_16, kChannelCount,
                           kFramesPerSecond, nullptr, nullptr);
    EXPECT_EQ(kSigned16SampleSize, under_test.bytes_per_sample());
    EXPECT_EQ(kSigned16SampleSize * kChannelCount, under_test.bytes_per_frame());
    EXPECT_EQ(static_cast<uint64_t>((kFramesPerSecond * kDuration.get()) / zx::sec(1).get()),
              under_test.frames_per(kDuration));
    EXPECT_EQ(kSigned16SampleSize * kChannelCount *
                  static_cast<uint64_t>((kFramesPerSecond * kDuration.get()) / zx::sec(1).get()),
              under_test.bytes_per(kDuration));
  }

  {
    AudioFormat under_test(fuchsia::mediastreams::AudioSampleFormat::SIGNED_24_IN_32, kChannelCount,
                           kFramesPerSecond, nullptr, nullptr);
    EXPECT_EQ(kSigned24In32SampleSize, under_test.bytes_per_sample());
    EXPECT_EQ(kSigned24In32SampleSize * kChannelCount, under_test.bytes_per_frame());
    EXPECT_EQ(static_cast<uint64_t>((kFramesPerSecond * kDuration.get()) / zx::sec(1).get()),
              under_test.frames_per(kDuration));
    EXPECT_EQ(kSigned24In32SampleSize * kChannelCount *
                  static_cast<uint64_t>((kFramesPerSecond * kDuration.get()) / zx::sec(1).get()),
              under_test.bytes_per(kDuration));
  }

  {
    AudioFormat under_test(fuchsia::mediastreams::AudioSampleFormat::SIGNED_32, kChannelCount,
                           kFramesPerSecond, nullptr, nullptr);
    EXPECT_EQ(kSigned32SampleSize, under_test.bytes_per_sample());
    EXPECT_EQ(kSigned32SampleSize * kChannelCount, under_test.bytes_per_frame());
    EXPECT_EQ(static_cast<uint64_t>((kFramesPerSecond * kDuration.get()) / zx::sec(1).get()),
              under_test.frames_per(kDuration));
    EXPECT_EQ(kSigned32SampleSize * kChannelCount *
                  static_cast<uint64_t>((kFramesPerSecond * kDuration.get()) / zx::sec(1).get()),
              under_test.bytes_per(kDuration));
  }

  {
    AudioFormat under_test(fuchsia::mediastreams::AudioSampleFormat::FLOAT, kChannelCount,
                           kFramesPerSecond, nullptr, nullptr);
    EXPECT_EQ(kFloatSampleSize, under_test.bytes_per_sample());
    EXPECT_EQ(kFloatSampleSize * kChannelCount, under_test.bytes_per_frame());
    EXPECT_EQ(static_cast<uint64_t>((kFramesPerSecond * kDuration.get()) / zx::sec(1).get()),
              under_test.frames_per(kDuration));
    EXPECT_EQ(kFloatSampleSize * kChannelCount *
                  static_cast<uint64_t>((kFramesPerSecond * kDuration.get()) / zx::sec(1).get()),
              under_test.bytes_per(kDuration));
  }
}

}  // namespace
}  // namespace fmlib

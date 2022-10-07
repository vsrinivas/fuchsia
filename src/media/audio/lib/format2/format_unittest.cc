// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/format2/format.h"

#include <gtest/gtest.h>

#include "fidl/fuchsia.mediastreams/cpp/wire_types.h"
#include "src/media/audio/lib/format2/fixed.h"

using SampleType = fuchsia_audio::SampleType;

namespace media_audio {

TEST(FormatTest, CreateFromWire) {
  fidl::Arena<> arena;
  auto msg = fuchsia_audio::wire::Format::Builder(arena)
                 .sample_type(SampleType::kInt32)
                 .channel_count(2)
                 .frames_per_second(48000)
                 .Build();

  Format format = Format::CreateOrDie(msg);
  EXPECT_EQ(format.sample_type(), SampleType::kInt32);
  EXPECT_EQ(format.channels(), 2);
  EXPECT_EQ(format.frames_per_second(), 48000);
  EXPECT_EQ(format.bytes_per_frame(), 8);
  EXPECT_EQ(format.bytes_per_sample(), 4);
  EXPECT_EQ(format.valid_bits_per_sample(), 32);
}

TEST(FormatTest, CreateFromNatural) {
  fuchsia_audio::Format msg;
  msg.sample_type() = SampleType::kInt32;
  msg.channel_count() = 2;
  msg.frames_per_second() = 48000;

  auto format = Format::CreateOrDie(msg);
  EXPECT_EQ(format.sample_type(), SampleType::kInt32);
  EXPECT_EQ(format.channels(), 2);
  EXPECT_EQ(format.frames_per_second(), 48000);
  EXPECT_EQ(format.bytes_per_frame(), 8);
  EXPECT_EQ(format.bytes_per_sample(), 4);
  EXPECT_EQ(format.valid_bits_per_sample(), 32);
}

TEST(FormatTest, CreateFromArgs) {
  Format format = Format::CreateOrDie({
      .sample_type = SampleType::kInt32,
      .channels = 2,
      .frames_per_second = 48000,
  });

  EXPECT_EQ(format.sample_type(), SampleType::kInt32);
  EXPECT_EQ(format.channels(), 2);
  EXPECT_EQ(format.frames_per_second(), 48000);
  EXPECT_EQ(format.bytes_per_frame(), 8);
  EXPECT_EQ(format.bytes_per_sample(), 4);
  EXPECT_EQ(format.valid_bits_per_sample(), 32);
}

TEST(FormatTest, ToWireFidl) {
  Format format = Format::CreateOrDie({
      .sample_type = SampleType::kInt32,
      .channels = 2,
      .frames_per_second = 48000,
  });

  fidl::Arena<> arena;
  auto msg = format.ToWireFidl(arena);
  ASSERT_TRUE(msg.has_sample_type());
  ASSERT_TRUE(msg.has_channel_count());
  ASSERT_TRUE(msg.has_frames_per_second());
  EXPECT_EQ(msg.sample_type(), SampleType::kInt32);
  EXPECT_EQ(msg.channel_count(), 2u);
  EXPECT_EQ(msg.frames_per_second(), 48000u);
}

TEST(FormatTest, ToNaturalFidl) {
  Format format = Format::CreateOrDie({
      .sample_type = SampleType::kInt32,
      .channels = 2,
      .frames_per_second = 48000,
  });

  auto msg = format.ToNaturalFidl();
  ASSERT_TRUE(msg.sample_type().has_value());
  ASSERT_TRUE(msg.channel_count().has_value());
  ASSERT_TRUE(msg.frames_per_second().has_value());
  EXPECT_EQ(*msg.sample_type(), SampleType::kInt32);
  EXPECT_EQ(*msg.channel_count(), 2u);
  EXPECT_EQ(*msg.frames_per_second(), 48000u);
}

TEST(FormatTest, ToLegacyFidl) {
  Format format = Format::CreateOrDie({
      .sample_type = SampleType::kInt32,
      .channels = 2,
      .frames_per_second = 48000,
  });

  auto msg = format.ToLegacyFidl();
  EXPECT_EQ(msg.sample_format, fuchsia_mediastreams::wire::AudioSampleFormat::kSigned24In32);
  EXPECT_EQ(msg.channel_count, 2u);
  EXPECT_EQ(msg.frames_per_second, 48000u);
}

TEST(FormatTest, OperatorEquals) {
  Format format1 = Format::CreateOrDie({
      .sample_type = SampleType::kFloat32,
      .channels = 2,
      .frames_per_second = 48000,
  });
  Format format2 = Format::CreateOrDie({
      .sample_type = SampleType::kFloat32,
      .channels = 2,
      .frames_per_second = 48000,
  });

  EXPECT_EQ(format1, format2);
}

TEST(FormatTest, OperatorEqualsDifferentChannels) {
  Format format1 = Format::CreateOrDie({
      .sample_type = SampleType::kFloat32,
      .channels = 2,
      .frames_per_second = 48000,
  });
  Format format2 = Format::CreateOrDie({
      .sample_type = SampleType::kFloat32,
      .channels = 1,
      .frames_per_second = 48000,
  });

  EXPECT_NE(format1, format2);
}

TEST(FormatTest, OperatorEqualsDifferentRates) {
  Format format1 = Format::CreateOrDie({
      .sample_type = SampleType::kFloat32,
      .channels = 2,
      .frames_per_second = 48000,
  });
  Format format2 = Format::CreateOrDie({
      .sample_type = SampleType::kFloat32,
      .channels = 2,
      .frames_per_second = 96000,
  });

  EXPECT_NE(format1, format2);
}

TEST(FormatTest, OperatorEqualsDifferentSampleTypes) {
  Format format1 = Format::CreateOrDie({
      .sample_type = SampleType::kFloat32,
      .channels = 2,
      .frames_per_second = 48000,
  });
  Format format2 = Format::CreateOrDie({
      .sample_type = SampleType::kUint8,
      .channels = 2,
      .frames_per_second = 48000,
  });

  EXPECT_NE(format1, format2);
}

TEST(FormatTest, IntegerFramesPer) {
  Format format = Format::CreateOrDie({
      .sample_type = SampleType::kFloat32,
      .channels = 2,
      .frames_per_second = 48000,
  });

  // Rounds up by default.
  EXPECT_EQ(format.integer_frames_per(zx::msec(10) - zx::nsec(1)), 480);
  EXPECT_EQ(format.integer_frames_per(zx::msec(10) + zx::nsec(0)), 480);
  EXPECT_EQ(format.integer_frames_per(zx::msec(10) + zx::nsec(1)), 481);

  // Round down should work too.
  constexpr auto kFloor = media::TimelineRate::RoundingMode::Floor;
  EXPECT_EQ(format.integer_frames_per(zx::msec(10) - zx::nsec(1), kFloor), 479);
  EXPECT_EQ(format.integer_frames_per(zx::msec(10) + zx::nsec(0), kFloor), 480);
  EXPECT_EQ(format.integer_frames_per(zx::msec(10) + zx::nsec(1), kFloor), 480);
}

TEST(FormatTest, FracFramesPer) {
  Format format = Format::CreateOrDie({
      .sample_type = SampleType::kFloat32,
      .channels = 2,
      .frames_per_second = 48000,
  });

  // For 48kHz audio, there are ~20833 ns/frame, which is ~2.5 ns/subframe
  // (since Fixed has 8192 subframes/frame). Hence, adding or subtracting
  // 1 ns should round to the same subframe.

  // 4.5 frames = 93750ns.
  const Fixed expected = ffl::FromRatio(9, 2);
  const Fixed expected_minus_one = expected - Fixed::FromRaw(1);
  const Fixed expected_plus_one = expected + Fixed::FromRaw(1);

  // Rounds up by default.
  EXPECT_EQ(format.frac_frames_per(zx::nsec(93750) - zx::nsec(1)), expected);
  EXPECT_EQ(format.frac_frames_per(zx::nsec(93750) + zx::nsec(0)), expected);
  EXPECT_EQ(format.frac_frames_per(zx::nsec(93750) + zx::nsec(1)), expected_plus_one);

  // Round down should work too.
  constexpr auto kFloor = media::TimelineRate::RoundingMode::Floor;
  EXPECT_EQ(format.frac_frames_per(zx::nsec(93750) - zx::nsec(1), kFloor), expected_minus_one);
  EXPECT_EQ(format.frac_frames_per(zx::nsec(93750) + zx::nsec(0), kFloor), expected);
  EXPECT_EQ(format.frac_frames_per(zx::nsec(93750) + zx::nsec(1), kFloor), expected);
}

TEST(FormatTest, BytesPer) {
  Format format = Format::CreateOrDie({
      .sample_type = SampleType::kFloat32,
      .channels = 2,
      .frames_per_second = 48000,
  });

  // Rounds up by default.
  EXPECT_EQ(format.bytes_per(zx::msec(10) - zx::nsec(1)), 480 * 8);
  EXPECT_EQ(format.bytes_per(zx::msec(10) + zx::nsec(0)), 480 * 8);
  EXPECT_EQ(format.bytes_per(zx::msec(10) + zx::nsec(1)), 481 * 8);

  // Round down should work too.
  constexpr auto kFloor = media::TimelineRate::RoundingMode::Floor;
  EXPECT_EQ(format.bytes_per(zx::msec(10) - zx::nsec(1), kFloor), 479 * 8);
  EXPECT_EQ(format.bytes_per(zx::msec(10) + zx::nsec(0), kFloor), 480 * 8);
  EXPECT_EQ(format.bytes_per(zx::msec(10) + zx::nsec(1), kFloor), 480 * 8);
}

TEST(FormatTest, DurationPer) {
  Format format = Format::CreateOrDie({
      .sample_type = SampleType::kFloat32,
      .channels = 2,
      .frames_per_second = 300000,
  });

  // For 300kHz audio, there are ~3333 ns/frame, which is ~2.4 subframes/ns
  // (since Fixed has 8192 subframes/frame). Hence, adding or subtracting
  // 1 subframe should round to the same ns.

  // For 300kHz audio, 4.5 frames = 15000ns.
  const Fixed frame = ffl::FromRatio(9, 2);

  // Rounds up by default.
  EXPECT_EQ(format.duration_per(frame - Fixed::FromRaw(1)), zx::nsec(15000));
  EXPECT_EQ(format.duration_per(frame + Fixed::FromRaw(0)), zx::nsec(15000));
  EXPECT_EQ(format.duration_per(frame + Fixed::FromRaw(1)), zx::nsec(15001));

  // Round down should work too.
  constexpr auto kFloor = media::TimelineRate::RoundingMode::Floor;
  EXPECT_EQ(format.duration_per(frame - Fixed::FromRaw(1), kFloor), zx::nsec(14999));
  EXPECT_EQ(format.duration_per(frame + Fixed::FromRaw(0), kFloor), zx::nsec(15000));
  EXPECT_EQ(format.duration_per(frame + Fixed::FromRaw(1), kFloor), zx::nsec(15000));
}

}  // namespace media_audio

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/processing/position_manager.h"

#include <iterator>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/format2/fixed.h"

namespace media_audio {
namespace {

TEST(PositionManagerTest, CurrentSourceFrame) {
  constexpr auto source_chans = 2u;
  constexpr auto dest_chans = 1u;
  PositionManager pos_mgr(source_chans, dest_chans, 1, kFracOneFrame);

  constexpr int64_t source_frame_count = 2;

  int16_t source[source_frame_count * source_chans];
  auto source_void = static_cast<void*>(source);

  auto source_start = 1;
  auto source_offset = Fixed(source_start);

  pos_mgr.SetSourceValues(source_void, source_frame_count, &source_offset);

  auto expected_source_frame = source + (source_start * source_chans);
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<int16_t>(), expected_source_frame);
  EXPECT_FALSE(pos_mgr.IsSourceConsumed());

  const auto expected_source_frame_u8 =
      reinterpret_cast<uint8_t*>(source_void) + (source_start * source_chans);
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<uint8_t>(), expected_source_frame_u8);

  const auto expected_source_frame_32 =
      reinterpret_cast<int32_t*>(source_void) + (source_start * source_chans);
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<int32_t>(), expected_source_frame_32);

  const auto expected_source_frame_float =
      reinterpret_cast<float*>(source_void) + (source_start * source_chans);
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<float>(), expected_source_frame_float);

  source_start = 0;
  source_offset = Fixed(source_start);
  pos_mgr.SetSourceValues(source_void, source_frame_count, &source_offset);

  expected_source_frame = source + (source_start * source_chans);
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<int16_t>(), expected_source_frame);
  EXPECT_FALSE(pos_mgr.IsSourceConsumed());
}

TEST(PositionManagerTest, CurrentDestFrame) {
  constexpr auto source_chans = 1u;
  constexpr auto dest_chans = 4u;
  PositionManager pos_mgr(source_chans, dest_chans, 1, kFracOneFrame);

  constexpr auto dest_frame_count = 2;
  float dest[dest_frame_count * dest_chans];
  int64_t dest_offset = 1;

  auto expected_dest_frame = dest + (dest_offset * dest_chans);
  pos_mgr.SetDestValues(dest, dest_frame_count, &dest_offset);
  EXPECT_EQ(pos_mgr.CurrentDestFrame(), expected_dest_frame);

  dest_offset = 0;
  expected_dest_frame = dest + (dest_offset * dest_chans);
  pos_mgr.SetDestValues(dest, dest_frame_count, &dest_offset);
  EXPECT_EQ(pos_mgr.CurrentDestFrame(), expected_dest_frame);
}

TEST(PositionManagerTest, UpdateOffsets) {
  PositionManager pos_mgr(1, 1, 1, kFracOneFrame);

  float input;
  void* source_void_ptr = static_cast<void*>(&input);
  constexpr int64_t source_frame_count = 1;
  auto source_offset = Fixed(0);
  pos_mgr.SetSourceValues(source_void_ptr, source_frame_count, &source_offset);

  float data;
  float* dest = &data;
  auto dest_frame_count = 1;
  int64_t dest_offset = 0;
  pos_mgr.SetDestValues(dest, dest_frame_count, &dest_offset);

  auto step_size = kFracOneFrame;
  auto step_size_modulo = 0ul;
  auto step_size_denominator = 2ul;
  auto source_position_modulo = 1ul;
  pos_mgr.SetRateValues(step_size, step_size_modulo, step_size_denominator, source_position_modulo);

  source_offset = Fixed::FromRaw(27);
  dest_offset = 42;
  pos_mgr.UpdateOffsets();
  source_position_modulo = pos_mgr.source_pos_modulo();

  EXPECT_EQ(source_offset, Fixed(0));
  EXPECT_EQ(dest_offset, 0);
  EXPECT_EQ(pos_mgr.source_pos_modulo(), 0ul);

  // Now that `step_size_modulo` and `step_size_denominator` are non-zero, `source_position_modulo`
  // should be updated.
  step_size_modulo = 1ul;
  step_size_denominator = 2ul;
  source_position_modulo = 1ul;
  pos_mgr.SetRateValues(step_size, step_size_modulo, step_size_denominator, source_position_modulo);

  source_offset = Fixed::FromRaw(27);
  dest_offset = 42;
  pos_mgr.UpdateOffsets();

  EXPECT_EQ(source_offset, Fixed(0));
  EXPECT_EQ(dest_offset, 0);
  EXPECT_EQ(pos_mgr.source_pos_modulo(), 1ul);
}

TEST(PositionManagerTest, CanFrameBeMixed) {
  constexpr auto source_chans = 1u;
  constexpr auto dest_chans = 1u;
  auto half = Fixed(ffl::FromRatio(1, 2)).raw_value();
  PositionManager pos_mgr(source_chans, dest_chans, half + 1, half + 1);

  int16_t source[2 * source_chans];
  const auto source_void = static_cast<void*>(source);
  Fixed source_offset = ffl::FromRatio(3, 2) - Fixed::FromRaw(1);
  pos_mgr.SetSourceValues(source_void, std::size(source), &source_offset);

  float dest[2u * dest_chans];
  int64_t dest_offset = 1;
  pos_mgr.SetDestValues(dest, 2, &dest_offset);
  EXPECT_TRUE(pos_mgr.CanFrameBeMixed());
  EXPECT_FALSE(pos_mgr.IsSourceConsumed());

  source_offset += Fixed::FromRaw(1);
  pos_mgr.SetSourceValues(source_void, std::size(source), &source_offset);
  EXPECT_FALSE(pos_mgr.CanFrameBeMixed());
  EXPECT_TRUE(pos_mgr.IsSourceConsumed());
}

TEST(PositionManagerTest, AdvanceFrameBasic) {
  PositionManager pos_mgr(1, 1, 1, kFracOneFrame);

  uint8_t source[3];
  auto source_offset = Fixed(1);
  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source), &source_offset);

  float dest[3];
  int64_t dest_offset = 1;
  pos_mgr.SetDestValues(dest, 3, &dest_offset);

  pos_mgr.SetRateValues(kFracOneFrame, 0, 1, 0ul);

  Fixed expected_source_offset = source_offset + Fixed(1);
  auto received_source_offset = Fixed::FromRaw(pos_mgr.AdvanceFrame());
  EXPECT_EQ(received_source_offset, expected_source_offset);
  EXPECT_TRUE(pos_mgr.CanFrameBeMixed());
  EXPECT_FALSE(pos_mgr.IsSourceConsumed());
}

TEST(PositionManagerTest, AdvanceFrameSourceReachesEnd) {
  PositionManager pos_mgr(1, 1, 1, kFracOneFrame);

  int32_t source[2];
  auto source_offset = Fixed(1);
  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source), &source_offset);

  float dest[3u];
  int64_t dest_offset = 2;
  pos_mgr.SetDestValues(dest, 3, &dest_offset);

  pos_mgr.SetRateValues(kFracOneFrame, 0, 1, 0ul);

  Fixed expected_source_offset = source_offset + Fixed(1);
  auto received_source_offset = Fixed::FromRaw(pos_mgr.AdvanceFrame());
  EXPECT_EQ(received_source_offset, expected_source_offset);
  EXPECT_FALSE(pos_mgr.CanFrameBeMixed());
  EXPECT_TRUE(pos_mgr.IsSourceConsumed());
}

TEST(PositionManagerTest, AdvanceFrame_SourceModuloReachesEnd) {
  PositionManager pos_mgr(1, 1, 1, kFracOneFrame);

  int16_t source[3];
  Fixed source_offset = Fixed(2) - Fixed::FromRaw(1);
  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source), &source_offset);

  float dest[3];
  int64_t dest_offset = 1;
  pos_mgr.SetDestValues(dest, 3, &dest_offset);

  // Source modulo starts just one shy of incrementing `source_offset`, and `step_size_modulo`
  // increments it. This is the boundary case, exactly where source modulo affects `source_offset`.
  constexpr auto step_size = kFracOneFrame;
  auto step_size_modulo = 1ul;
  auto step_size_denominator = 243ul;
  pos_mgr.SetRateValues(step_size, step_size_modulo, step_size_denominator, 242ul);

  Fixed expected_source_offset = Fixed(2) - Fixed::FromRaw(1);
  EXPECT_TRUE(pos_mgr.CanFrameBeMixed());
  EXPECT_FALSE(pos_mgr.IsSourceConsumed());
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<int16_t>(), &source[1]);
  EXPECT_EQ(pos_mgr.source_offset(), expected_source_offset);

  expected_source_offset = Fixed(3);
  auto received_source_offset = Fixed::FromRaw(pos_mgr.AdvanceFrame());
  EXPECT_EQ(received_source_offset, expected_source_offset);
  EXPECT_EQ(pos_mgr.source_offset(), received_source_offset);

  EXPECT_FALSE(pos_mgr.CanFrameBeMixed());
  EXPECT_TRUE(pos_mgr.IsSourceConsumed());
  EXPECT_EQ(received_source_offset, Fixed(3));
}

TEST(PositionManagerTest, AdvanceFrameSourceModuloAlmostReachesEnd) {
  PositionManager pos_mgr(1, 1, 1, kFracOneFrame);

  float source[3];
  Fixed source_offset = Fixed(2) - Fixed::FromRaw(1);
  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source), &source_offset);

  float dest[3];
  int64_t dest_offset = 1;
  pos_mgr.SetDestValues(dest, 3, &dest_offset);

  constexpr auto step_size = kFracOneFrame;
  auto step_size_modulo = 1ul;
  auto step_size_denominator = 243ul;
  pos_mgr.SetRateValues(step_size, step_size_modulo, step_size_denominator, 241ul);

  // Source modulo starts just two shy of incrementing `source_offset`, and `step_size_modulo`
  // increments it by one. This is the boundary case, one less than where source modulo would affect
  // `source_offset`.
  Fixed expected_source_offset = Fixed(2) - Fixed::FromRaw(1);
  EXPECT_TRUE(pos_mgr.CanFrameBeMixed());
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<float>(), &source[1]);
  EXPECT_EQ(pos_mgr.source_offset(), expected_source_offset);

  expected_source_offset = source_offset + Fixed(1);
  auto received_source_offset = Fixed::FromRaw(pos_mgr.AdvanceFrame());
  EXPECT_EQ(received_source_offset, expected_source_offset);
  EXPECT_EQ(pos_mgr.source_offset(), received_source_offset);

  EXPECT_TRUE(pos_mgr.CanFrameBeMixed());
  EXPECT_FALSE(pos_mgr.IsSourceConsumed());
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<float>(), &source[2]);
  EXPECT_EQ(received_source_offset, Fixed(3) - Fixed::FromRaw(1));
}

TEST(PositionManagerTest, AdvanceFrameDestReachesEnd) {
  PositionManager pos_mgr(1, 1, 1, kFracOneFrame);

  int16_t source[3];
  auto source_offset = Fixed(1);
  const Fixed expected_source_offset = source_offset + Fixed(1);

  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source), &source_offset);

  float dest[2];
  const int64_t dest_frame_count = std::size(dest);
  int64_t dest_offset = 1;
  pos_mgr.SetDestValues(dest, dest_frame_count, &dest_offset);

  constexpr auto step_size = kFracOneFrame;
  pos_mgr.SetRateValues(step_size, 0, 1, 0ul);

  // When `dest_offset` reaches `dest_frame_count`, we can no longer mix a frame, but `source` is
  // not consumed.
  auto received_source_offset = Fixed::FromRaw(pos_mgr.AdvanceFrame());

  EXPECT_EQ(received_source_offset, expected_source_offset);
  EXPECT_EQ(pos_mgr.source_offset(), received_source_offset);

  EXPECT_FALSE(pos_mgr.CanFrameBeMixed());
  EXPECT_FALSE(pos_mgr.IsSourceConsumed());
}

TEST(PositionManagerTest, AdvanceFrameNoRateValues) {
  PositionManager pos_mgr(1, 1, 1, kFracOneFrame);

  int16_t source[3];
  Fixed source_offset = Fixed(2) - Fixed::FromRaw(1);
  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source), &source_offset);

  float dest[3];
  int64_t dest_offset = 1;
  pos_mgr.SetDestValues(dest, 3, &dest_offset);

  // Unity `step_size` (no modulo) should be the default if `SetRateValues` is never called.
  Fixed expected_source_offset = Fixed(2) - Fixed::FromRaw(1);
  EXPECT_TRUE(pos_mgr.CanFrameBeMixed());
  EXPECT_FALSE(pos_mgr.IsSourceConsumed());
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<int16_t>(), &source[1]);
  EXPECT_EQ(pos_mgr.source_offset(), expected_source_offset);

  expected_source_offset = Fixed(3) - Fixed::FromRaw(1);
  auto received_source_offset = Fixed::FromRaw(pos_mgr.AdvanceFrame());
  pos_mgr.UpdateOffsets();

  EXPECT_EQ(received_source_offset, expected_source_offset);
  EXPECT_EQ(pos_mgr.source_offset(), received_source_offset);

  EXPECT_TRUE(pos_mgr.CanFrameBeMixed());
  EXPECT_FALSE(pos_mgr.IsSourceConsumed());
}

TEST(PositionManagerTest, AdvanceToEndDest) {
  PositionManager pos_mgr(1, 1, 1, kFracOneFrame);

  int16_t source[12];
  Fixed source_offset = Fixed(1) - Fixed::FromRaw(1);
  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source), &source_offset);

  float dest[5];
  int64_t dest_offset = 0;
  pos_mgr.SetDestValues(dest, std::size(dest), &dest_offset);

  constexpr auto step_size = kFracOneFrame * 2 - 1;
  auto step_size_denominator = 2ul;
  pos_mgr.SetRateValues(step_size, 0, step_size_denominator, 1ul);

  // `AdvanceToEnd` should be limited by `dest`.
  auto num_source_frame_count_skipped = pos_mgr.AdvanceToEnd();
  EXPECT_EQ(num_source_frame_count_skipped, 10);

  pos_mgr.UpdateOffsets();

  EXPECT_EQ(source_offset, Fixed(11) - Fixed::FromRaw(6));
  EXPECT_EQ(dest_offset, 5);
  EXPECT_EQ(pos_mgr.source_pos_modulo(), 0ul);
  EXPECT_FALSE(pos_mgr.CanFrameBeMixed());
  EXPECT_FALSE(pos_mgr.IsSourceConsumed());
}

TEST(PositionManagerTest, AdvanceToEndSourceBasic) {
  PositionManager pos_mgr(1, 1, kFracHalfFrame + 1, kFracHalfFrame + 1);

  int16_t source[6];
  auto source_offset = Fixed(1);
  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source), &source_offset);

  float dest[13];
  int64_t dest_offset = 0;
  pos_mgr.SetDestValues(dest, std::size(dest), &dest_offset);
  pos_mgr.SetRateValues(kFracHalfFrame, 0, 1, 0ul);

  // `AdvanceToEnd` should be limited by `source`.
  auto num_source_frame_count_skipped = pos_mgr.AdvanceToEnd();
  EXPECT_EQ(num_source_frame_count_skipped, 5);

  Fixed expect_source_offset = Fixed(ffl::FromRatio(11, 2));
  pos_mgr.UpdateOffsets();

  EXPECT_EQ(source_offset, expect_source_offset)
      << std::hex << source_offset.raw_value() << " != " << expect_source_offset.raw_value();
  EXPECT_EQ(dest_offset, 9);
  EXPECT_EQ(pos_mgr.source_pos_modulo(), 0ul);
  EXPECT_FALSE(pos_mgr.CanFrameBeMixed());
  EXPECT_TRUE(pos_mgr.IsSourceConsumed());
}

TEST(PositionManagerTest, AdvanceToEndSourceExactModulo) {
  PositionManager pos_mgr(1, 1, 1, kFracHalfFrame + 1);

  int16_t source[11];
  Fixed source_offset = Fixed(1) - Fixed::FromRaw(1);
  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source), &source_offset);

  float dest[6];
  int64_t dest_offset = 0;
  pos_mgr.SetDestValues(dest, std::size(dest), &dest_offset);

  constexpr auto step_size = 2 * kFracOneFrame;
  auto step_size_modulo = 1ul;
  auto step_size_denominator = 25ul;
  pos_mgr.SetRateValues(step_size, step_size_modulo, step_size_denominator, 20ul);

  // `AdvanceToEnd` should be limited by `source`, where `rate_module` contributes EXACTLY what
  // consumes `source`.
  auto num_source_frame_count_skipped = pos_mgr.AdvanceToEnd();
  EXPECT_EQ(num_source_frame_count_skipped, 11);

  pos_mgr.UpdateOffsets();

  EXPECT_EQ(source_offset, Fixed(11));
  EXPECT_EQ(dest_offset, 5);
  EXPECT_EQ(pos_mgr.source_pos_modulo(), 0ul);
  EXPECT_FALSE(pos_mgr.CanFrameBeMixed());
  EXPECT_TRUE(pos_mgr.IsSourceConsumed());
}

// AdvanceToEnd is limited by source; source_position modulo flows beyond source but by < 1 frame.
TEST(PositionManagerTest, AdvanceToEndSourceExtraModulo) {
  PositionManager pos_mgr(1, 1, 1, kFracHalfFrame + 1);

  int16_t source[11];
  Fixed source_offset = Fixed(1) - Fixed::FromRaw(1);
  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source), &source_offset);

  float dest[6];
  int64_t dest_offset = 0;
  pos_mgr.SetDestValues(dest, std::size(dest), &dest_offset);

  constexpr auto step_size = kFracOneFrame * 2;
  auto step_size_modulo = 1ul;
  auto step_size_denominator = 25ul;
  pos_mgr.SetRateValues(step_size, step_size_modulo, step_size_denominator, 24ul);

  // `AdvanceToEnd` should be limited by `source`, where `source_position_modulo` flows beyond
  // `source` by <1 frame.
  auto num_source_frame_count_skipped = pos_mgr.AdvanceToEnd();
  EXPECT_EQ(num_source_frame_count_skipped, 11);

  pos_mgr.UpdateOffsets();

  EXPECT_EQ(source_offset, Fixed(11));
  EXPECT_EQ(dest_offset, 5);
  EXPECT_EQ(pos_mgr.source_pos_modulo(), 4ul);
  EXPECT_FALSE(pos_mgr.CanFrameBeMixed());
  EXPECT_TRUE(pos_mgr.IsSourceConsumed());
}

TEST(PositionManagerTest, AdvanceToEndExtremeRatesAndWidths) {
  PositionManager pos_mgr(1, 1, (336 << Fixed::Format::FractionalBits) + 1,
                          (336 << Fixed::Format::FractionalBits) + 1);

  float dest[10];
  int64_t dest_offset = 0;
  pos_mgr.SetDestValues(dest, std::size(dest), &dest_offset);

  auto step_size = (24 << Fixed::Format::FractionalBits);
  auto step_size_modulo = 0ul;
  auto step_size_denominator = 1ul;
  pos_mgr.SetRateValues(step_size, step_size_modulo, step_size_denominator, 0ul);

  int16_t source[360];
  auto source_offset = Fixed::FromRaw(-1);
  pos_mgr.SetSourceValues(static_cast<void*>(source), 336, &source_offset);

  // `AdvanceToEnd` should be limited by `source`.
  auto num_source_frame_count_skipped = pos_mgr.AdvanceToEnd();
  EXPECT_EQ(num_source_frame_count_skipped, 24);

  pos_mgr.UpdateOffsets();
  EXPECT_EQ(source_offset.raw_value(), (24 << Fixed::Format::FractionalBits) - 1);
  EXPECT_EQ(dest_offset, 1);
  EXPECT_FALSE(pos_mgr.CanFrameBeMixed());
  EXPECT_TRUE(pos_mgr.IsSourceConsumed());

  // Now try starting from just one subframe further. This should not advance.
  source_offset = Fixed(0);
  pos_mgr.SetSourceValues(static_cast<void*>(source), 336, &source_offset);

  num_source_frame_count_skipped = pos_mgr.AdvanceToEnd();
  EXPECT_EQ(num_source_frame_count_skipped, 0);

  pos_mgr.UpdateOffsets();
  EXPECT_EQ(source_offset, Fixed(0));
  EXPECT_EQ(dest_offset, 1);
  EXPECT_FALSE(pos_mgr.CanFrameBeMixed());
  EXPECT_TRUE(pos_mgr.IsSourceConsumed());

  // Back up position but subtract a frame of source supply: we should not advance.
  source_offset = Fixed::FromRaw(-1);
  pos_mgr.SetSourceValues(static_cast<void*>(source), 335, &source_offset);

  num_source_frame_count_skipped = pos_mgr.AdvanceToEnd();
  EXPECT_EQ(num_source_frame_count_skipped, 0);

  pos_mgr.UpdateOffsets();
  EXPECT_EQ(source_offset, Fixed::FromRaw(-1));
  EXPECT_EQ(dest_offset, 1);
  EXPECT_FALSE(pos_mgr.CanFrameBeMixed());
  EXPECT_TRUE(pos_mgr.IsSourceConsumed());
}

}  // namespace
}  // namespace media_audio

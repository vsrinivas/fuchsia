// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/position_manager.h"

#include <iterator>

#include <fbl/algorithm.h>
#include <gtest/gtest.h>

namespace media::audio::mixer {
namespace {

constexpr int64_t kFracFrame = kOneFrame.raw_value();
constexpr int64_t kFracHalfFrame = kFracFrame / 2;

// Produce the frame pointer (in source format) for the first frame in the source buffer.
TEST(PositionManagerTest, FirstSourceFrame) {
  auto source_chans = 2u;
  mixer::PositionManager pos_mgr(source_chans, 2u, 1, 1);

  int64_t source_frames = 5;

  // Setting this to any non-nullptr value
  void* source_void_ptr = static_cast<void*>(&source_frames);

  auto source_offset = Fixed(3);

  pos_mgr.SetSourceValues(source_void_ptr, source_frames, &source_offset);

  uint8_t* expected_source_frame_u8 = static_cast<uint8_t*>(source_void_ptr);
  int16_t* expected_source_frame_16 = static_cast<int16_t*>(source_void_ptr);
  int32_t* expected_source_frame_32 = static_cast<int32_t*>(source_void_ptr);
  float* expected_source_frame_float = static_cast<float*>(source_void_ptr);

  EXPECT_EQ(pos_mgr.FirstSourceFrame<uint8_t>(), expected_source_frame_u8);
  EXPECT_EQ(pos_mgr.FirstSourceFrame<int16_t>(), expected_source_frame_16);
  EXPECT_EQ(pos_mgr.FirstSourceFrame<int32_t>(), expected_source_frame_32);
  EXPECT_EQ(pos_mgr.FirstSourceFrame<float>(), expected_source_frame_float);
}

// Produce the frame pointer (in source format) for the last frame in the source buffer.
TEST(PositionManagerTest, LastSourceFrame) {
  constexpr auto source_chans = 3u;
  mixer::PositionManager pos_mgr(source_chans, 2u, 1, 1);

  constexpr int64_t source_frames = 5;

  float source[source_chans * source_frames];
  const auto source_void = static_cast<void*>(source);

  auto source_offset = Fixed(1);

  pos_mgr.SetSourceValues(source_void, source_frames, &source_offset);

  auto sample_num_of_last_frame = source_chans * (source_frames - 1);
  uint8_t* expected_source_frame_u8 = static_cast<uint8_t*>(source_void) + sample_num_of_last_frame;
  int16_t* expected_source_frame_16 = static_cast<int16_t*>(source_void) + sample_num_of_last_frame;
  int32_t* expected_source_frame_32 = static_cast<int32_t*>(source_void) + sample_num_of_last_frame;
  float* expected_source_frame_float = static_cast<float*>(source_void) + sample_num_of_last_frame;

  EXPECT_EQ(pos_mgr.LastSourceFrame<uint8_t>(), expected_source_frame_u8);
  EXPECT_EQ(pos_mgr.LastSourceFrame<int16_t>(), expected_source_frame_16);
  EXPECT_EQ(pos_mgr.LastSourceFrame<int32_t>(), expected_source_frame_32);
  EXPECT_EQ(pos_mgr.LastSourceFrame<float>(), expected_source_frame_float);
}

// Produce the frame pointer (in source format) corresponding with the current source offset
// This should take into account both source format container size and num source channels.
TEST(PositionManagerTest, CurrentSourceFrame) {
  constexpr auto source_chans = 2u;
  constexpr auto dest_chans = 1u;
  mixer::PositionManager pos_mgr(source_chans, dest_chans, 1, kFracFrame);

  constexpr int64_t source_frames = 2;

  int16_t source[source_frames * source_chans];
  auto source_void = static_cast<void*>(source);

  auto source_start = 1;
  auto source_offset = Fixed(source_start);

  pos_mgr.SetSourceValues(source_void, source_frames, &source_offset);

  auto expected_source_frame = source + (source_start * source_chans);
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<int16_t>(), expected_source_frame);
  EXPECT_FALSE(pos_mgr.SourceIsConsumed());

  auto expected_source_frame_u8 =
      reinterpret_cast<uint8_t*>(source_void) + (source_start * source_chans);
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<uint8_t>(), expected_source_frame_u8);

  auto expected_source_frame_32 =
      reinterpret_cast<int32_t*>(source_void) + (source_start * source_chans);
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<int32_t>(), expected_source_frame_32);

  auto expected_source_frame_float =
      reinterpret_cast<float*>(source_void) + (source_start * source_chans);
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<float>(), expected_source_frame_float);

  source_start = 0;
  source_offset = Fixed(source_start);
  pos_mgr.SetSourceValues(source_void, source_frames, &source_offset);

  expected_source_frame = source + (source_start * source_chans);
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<int16_t>(), expected_source_frame);
  EXPECT_FALSE(pos_mgr.SourceIsConsumed());
}

// Produce the frame pointer (float*) corresponding with the current destination offset
// This should take into account the number of destination channels.
TEST(PositionManagerTest, CurrentDestFrame) {
  constexpr auto source_chans = 1u;
  constexpr auto dest_chans = 4u;
  mixer::PositionManager pos_mgr(source_chans, dest_chans, 1, kFracFrame);

  constexpr auto dest_frames = 2;
  float dest[dest_frames * dest_chans];
  int64_t dest_offset = 1;

  auto expected_dest_frame = dest + (dest_offset * dest_chans);
  pos_mgr.SetDestValues(dest, dest_frames, &dest_offset);

  EXPECT_EQ(pos_mgr.CurrentDestFrame(), expected_dest_frame);

  dest_offset = 0;
  expected_dest_frame = dest + (dest_offset * dest_chans);
  pos_mgr.SetDestValues(dest, dest_frames, &dest_offset);

  EXPECT_EQ(pos_mgr.CurrentDestFrame(), expected_dest_frame);
}

// Write back the latest values of source offset, dest offset, and source modulo.
// This should overwrite existing values at those locations, and include effects of advances.
TEST(PositionManagerTest, UpdateOffsets) {
  mixer::PositionManager pos_mgr(1, 1, 1, kFracFrame);

  float input;
  void* source_void_ptr = static_cast<void*>(&input);
  constexpr int64_t source_frames = 1;
  auto source_offset = Fixed(0);
  pos_mgr.SetSourceValues(source_void_ptr, source_frames, &source_offset);

  float data;
  float* dest = &data;
  auto dest_frames = 1;
  int64_t dest_offset = 0;
  pos_mgr.SetDestValues(dest, dest_frames, &dest_offset);

  auto step_size = kFracFrame;
  auto rate_modulo = 0ul;
  auto denominator = 1ul;
  auto source_position_modulo = 0ul;
  pos_mgr.SetRateValues(step_size, rate_modulo, denominator, &source_position_modulo);

  source_offset = Fixed::FromRaw(27);
  dest_offset = 42;
  source_position_modulo = 72;
  pos_mgr.UpdateOffsets();

  EXPECT_EQ(source_offset, Fixed(0));
  EXPECT_EQ(dest_offset, 0);
  EXPECT_EQ(source_position_modulo, 72ul);

  // Now that rate_modulo and denominator are non-zero, source_position_modulo should be updated
  rate_modulo = 1ul;
  denominator = 2ul;
  source_position_modulo = 0;
  pos_mgr.SetRateValues(step_size, rate_modulo, denominator, &source_position_modulo);

  source_offset = Fixed::FromRaw(27);
  dest_offset = 42;
  source_position_modulo = 72;
  pos_mgr.UpdateOffsets();

  EXPECT_EQ(source_offset, Fixed(0));
  EXPECT_EQ(dest_offset, 0);
  EXPECT_EQ(source_position_modulo, 0ul);
}

TEST(PositionManagerTest, FrameCanBeMixed) {
  constexpr auto source_chans = 1u;
  constexpr auto dest_chans = 1u;
  auto half = Fixed(ffl::FromRatio(1, 2)).raw_value();
  mixer::PositionManager pos_mgr(source_chans, dest_chans, half + 1, half + 1);

  int16_t source[2 * source_chans];
  const auto source_void = static_cast<void*>(source);
  Fixed source_offset = ffl::FromRatio(3, 2) - Fixed::FromRaw(1);
  pos_mgr.SetSourceValues(source_void, std::size(source), &source_offset);

  float dest[2u * dest_chans];
  int64_t dest_offset = 1;
  pos_mgr.SetDestValues(dest, 2, &dest_offset);
  EXPECT_TRUE(pos_mgr.FrameCanBeMixed());
  EXPECT_FALSE(pos_mgr.SourceIsConsumed());

  source_offset += Fixed::FromRaw(1);
  pos_mgr.SetSourceValues(source_void, std::size(source), &source_offset);
  EXPECT_FALSE(pos_mgr.FrameCanBeMixed());
  EXPECT_TRUE(pos_mgr.SourceIsConsumed());
}

TEST(PositionManagerTest, AdvanceFrame_Basic) {
  mixer::PositionManager pos_mgr(1, 1, 1, kFracFrame);

  uint8_t source[3];
  auto source_offset = Fixed(1);
  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source), &source_offset);

  float dest[3];
  int64_t dest_offset = 1;
  pos_mgr.SetDestValues(dest, 3, &dest_offset);

  auto source_position_modulo = 0ul;
  pos_mgr.SetRateValues(kFracFrame, 0, 1, &source_position_modulo);

  Fixed expected_source_offset = source_offset + Fixed(1);
  auto received_source_offset = Fixed::FromRaw(pos_mgr.AdvanceFrame());
  EXPECT_EQ(received_source_offset, expected_source_offset);
  EXPECT_TRUE(pos_mgr.FrameCanBeMixed());
  EXPECT_FALSE(pos_mgr.SourceIsConsumed());
}

// When source_offset reaches source_frames, we can no longer mix a frame, and source is consumed.
TEST(PositionManagerTest, AdvanceFrame_SourceReachesEnd) {
  mixer::PositionManager pos_mgr(1, 1, 1, kFracFrame);

  int32_t source[2];
  auto source_offset = Fixed(1);
  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source), &source_offset);

  float dest[3u];
  int64_t dest_offset = 2;
  pos_mgr.SetDestValues(dest, 3, &dest_offset);

  auto source_position_modulo = 0ul;
  pos_mgr.SetRateValues(kFracFrame, 0, 1, &source_position_modulo);

  Fixed expected_source_offset = source_offset + Fixed(1);
  auto received_source_offset = Fixed::FromRaw(pos_mgr.AdvanceFrame());
  EXPECT_EQ(received_source_offset, expected_source_offset);
  EXPECT_FALSE(pos_mgr.FrameCanBeMixed());
  EXPECT_TRUE(pos_mgr.SourceIsConsumed());
}

// source_modulo starts just one shy of incrementing source_offset, and rate_modulo increments it
// This is the boundary case, exactly where source_modulo affects source_offset
TEST(PositionManagerTest, AdvanceFrame_SourceModuloReachesEnd) {
  mixer::PositionManager pos_mgr(1, 1, 1, kFracFrame);

  int16_t source[3];
  Fixed source_offset = Fixed(2) - Fixed::FromRaw(1);
  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source), &source_offset);

  float dest[3];
  int64_t dest_offset = 1;
  pos_mgr.SetDestValues(dest, 3, &dest_offset);

  constexpr auto step_size = kFracFrame;
  auto rate_modulo = 1ul;
  auto denominator = 243ul;
  auto source_position_modulo = 242ul;
  pos_mgr.SetRateValues(step_size, rate_modulo, denominator, &source_position_modulo);

  Fixed expected_source_offset = Fixed(2) - Fixed::FromRaw(1);
  EXPECT_TRUE(pos_mgr.FrameCanBeMixed());
  EXPECT_FALSE(pos_mgr.SourceIsConsumed());
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<int16_t>(), &source[1]);
  EXPECT_EQ(pos_mgr.source_offset(), expected_source_offset);

  expected_source_offset = Fixed(3);
  auto received_source_offset = Fixed::FromRaw(pos_mgr.AdvanceFrame());
  EXPECT_EQ(received_source_offset, expected_source_offset);
  EXPECT_EQ(pos_mgr.source_offset(), received_source_offset);

  EXPECT_FALSE(pos_mgr.FrameCanBeMixed());
  EXPECT_TRUE(pos_mgr.SourceIsConsumed());
  EXPECT_EQ(received_source_offset, Fixed(3));
}

// source_modulo starts two shy of incrementing source_offset, and rate_modulo increments it by one.
// This is the boundary case, one less than where source_modulo affects source_offset
TEST(PositionManagerTest, AdvanceFrame_SourceModuloAlmostReachesEnd) {
  mixer::PositionManager pos_mgr(1, 1, 1, kFracFrame);

  float source[3];
  Fixed source_offset = Fixed(2) - Fixed::FromRaw(1);
  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source), &source_offset);

  float dest[3];
  int64_t dest_offset = 1;
  pos_mgr.SetDestValues(dest, 3, &dest_offset);

  constexpr auto step_size = kFracFrame;
  auto rate_modulo = 1ul;
  auto denominator = 243ul;
  auto source_position_modulo = 241ul;
  pos_mgr.SetRateValues(step_size, rate_modulo, denominator, &source_position_modulo);

  Fixed expected_source_offset = Fixed(2) - Fixed::FromRaw(1);
  EXPECT_TRUE(pos_mgr.FrameCanBeMixed());
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<float>(), &source[1]);
  EXPECT_EQ(pos_mgr.source_offset(), expected_source_offset);

  expected_source_offset = source_offset + Fixed(1);
  auto received_source_offset = Fixed::FromRaw(pos_mgr.AdvanceFrame());
  EXPECT_EQ(received_source_offset, expected_source_offset);
  EXPECT_EQ(pos_mgr.source_offset(), received_source_offset);

  EXPECT_TRUE(pos_mgr.FrameCanBeMixed());
  EXPECT_FALSE(pos_mgr.SourceIsConsumed());
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<float>(), &source[2]);
  EXPECT_EQ(received_source_offset, Fixed(3) - Fixed::FromRaw(1));
}

// When dest_offset reaches dest_frames, we can no longer mix a frame, but source is not consumed.
TEST(PositionManagerTest, AdvanceFrame_DestReachesEnd) {
  mixer::PositionManager pos_mgr(1, 1, 1, kFracFrame);

  int16_t source[3];
  auto source_offset = Fixed(1);
  const Fixed expected_source_offset = source_offset + Fixed(1);

  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source), &source_offset);

  float dest[2];
  const int64_t dest_frames = std::size(dest);
  int64_t dest_offset = 1;
  pos_mgr.SetDestValues(dest, dest_frames, &dest_offset);

  constexpr auto step_size = kFracFrame;
  auto source_position_modulo = 0ul;
  pos_mgr.SetRateValues(step_size, 0, 1, &source_position_modulo);

  auto received_source_offset = Fixed::FromRaw(pos_mgr.AdvanceFrame());

  EXPECT_EQ(received_source_offset, expected_source_offset);
  EXPECT_EQ(pos_mgr.source_offset(), received_source_offset);

  EXPECT_FALSE(pos_mgr.FrameCanBeMixed());
  EXPECT_FALSE(pos_mgr.SourceIsConsumed());
}

// Unity step_size (no rate_modulo) is the default if SetRateValues is never called.
TEST(PositionManagerTest, AdvanceFrame_NoRateValues) {
  mixer::PositionManager pos_mgr(1, 1, 1, kFracFrame);

  int16_t source[3];
  Fixed source_offset = Fixed(2) - Fixed::FromRaw(1);
  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source), &source_offset);

  float dest[3];
  int64_t dest_offset = 1;
  pos_mgr.SetDestValues(dest, 3, &dest_offset);

  Fixed expected_source_offset = Fixed(2) - Fixed::FromRaw(1);
  EXPECT_TRUE(pos_mgr.FrameCanBeMixed());
  EXPECT_FALSE(pos_mgr.SourceIsConsumed());
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<int16_t>(), &source[1]);
  EXPECT_EQ(pos_mgr.source_offset(), expected_source_offset);

  expected_source_offset = Fixed(3) - Fixed::FromRaw(1);
  auto received_source_offset = Fixed::FromRaw(pos_mgr.AdvanceFrame());
  pos_mgr.UpdateOffsets();

  EXPECT_EQ(received_source_offset, expected_source_offset);
  EXPECT_EQ(pos_mgr.source_offset(), received_source_offset);

  EXPECT_TRUE(pos_mgr.FrameCanBeMixed());
  EXPECT_FALSE(pos_mgr.SourceIsConsumed());
}

// AdvanceToEnd is limited by dest.
TEST(PositionManagerTest, AdvanceToEnd_Dest) {
  mixer::PositionManager pos_mgr(1, 1, 1, kFracFrame);

  int16_t source[12];
  Fixed source_offset = Fixed(1) - Fixed::FromRaw(1);
  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source), &source_offset);

  float dest[5];
  int64_t dest_offset = 0;
  pos_mgr.SetDestValues(dest, std::size(dest), &dest_offset);

  constexpr auto step_size = kFracFrame * 2 - 1;
  auto source_position_modulo = 1ul;
  auto denominator = 2ul;
  pos_mgr.SetRateValues(step_size, 0, denominator, &source_position_modulo);

  auto num_source_frames_skipped = pos_mgr.AdvanceToEnd();
  EXPECT_EQ(num_source_frames_skipped, 10);

  pos_mgr.UpdateOffsets();

  EXPECT_EQ(source_offset, Fixed(11) - Fixed::FromRaw(6));
  EXPECT_EQ(dest_offset, 5);
  EXPECT_EQ(source_position_modulo, 1ul);
  EXPECT_FALSE(pos_mgr.FrameCanBeMixed());
  EXPECT_FALSE(pos_mgr.SourceIsConsumed());
}

// AdvanceToEnd is limited by source, with no rate_modulo factor
TEST(PositionManagerTest, AdvanceToEnd_SourceBasic) {
  mixer::PositionManager pos_mgr(1, 1, kFracHalfFrame + 1, kFracHalfFrame + 1);

  int16_t source[6];
  auto source_offset = Fixed(1);
  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source), &source_offset);

  float dest[13];
  int64_t dest_offset = 0;
  pos_mgr.SetDestValues(dest, std::size(dest), &dest_offset);

  auto source_position_modulo = 0ul;
  pos_mgr.SetRateValues(kFracHalfFrame, 0, 1, &source_position_modulo);

  auto num_source_frames_skipped = pos_mgr.AdvanceToEnd();
  EXPECT_EQ(num_source_frames_skipped, 5);

  Fixed expect_source_offset = Fixed(ffl::FromRatio(11, 2));
  pos_mgr.UpdateOffsets();

  EXPECT_EQ(source_offset, expect_source_offset)
      << std::hex << source_offset.raw_value() << " != " << expect_source_offset.raw_value();
  EXPECT_EQ(dest_offset, 9);
  EXPECT_EQ(source_position_modulo, 0u);
  EXPECT_FALSE(pos_mgr.FrameCanBeMixed());
  EXPECT_TRUE(pos_mgr.SourceIsConsumed());
}

// AdvanceToEnd is limited by source; rate_modulo contributes EXACTLY what consumes source.
TEST(PositionManagerTest, AdvanceToEnd_SourceExactModulo) {
  mixer::PositionManager pos_mgr(1, 1, 1, kFracHalfFrame + 1);

  int16_t source[11];
  Fixed source_offset = Fixed(1) - Fixed::FromRaw(1);
  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source), &source_offset);

  float dest[6];
  int64_t dest_offset = 0;
  pos_mgr.SetDestValues(dest, std::size(dest), &dest_offset);

  constexpr auto step_size = 2 * kFracFrame;
  auto rate_modulo = 1ul;
  auto denominator = 25ul;
  auto source_position_modulo = 20ul;
  pos_mgr.SetRateValues(step_size, rate_modulo, denominator, &source_position_modulo);

  auto num_source_frames_skipped = pos_mgr.AdvanceToEnd();
  EXPECT_EQ(num_source_frames_skipped, 11);

  pos_mgr.UpdateOffsets();

  EXPECT_EQ(source_offset, Fixed(11));
  EXPECT_EQ(dest_offset, 5);
  EXPECT_EQ(source_position_modulo, 0ul);
  EXPECT_FALSE(pos_mgr.FrameCanBeMixed());
  EXPECT_TRUE(pos_mgr.SourceIsConsumed());
}

// AdvanceToEnd is limited by source; source_position modulo flows beyond source but by < 1 frame.
TEST(PositionManagerTest, AdvanceToEnd_SourceExtraModulo) {
  mixer::PositionManager pos_mgr(1, 1, 1, kFracHalfFrame + 1);

  int16_t source[11];
  Fixed source_offset = Fixed(1) - Fixed::FromRaw(1);
  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source), &source_offset);

  float dest[6];
  int64_t dest_offset = 0;
  pos_mgr.SetDestValues(dest, std::size(dest), &dest_offset);

  constexpr auto step_size = kFracFrame * 2;
  auto rate_modulo = 1ul;
  auto denominator = 25ul;
  auto source_position_modulo = 24ul;
  pos_mgr.SetRateValues(step_size, rate_modulo, denominator, &source_position_modulo);

  auto num_source_frames_skipped = pos_mgr.AdvanceToEnd();
  EXPECT_EQ(num_source_frames_skipped, 11);

  pos_mgr.UpdateOffsets();

  EXPECT_EQ(source_offset, Fixed(11));
  EXPECT_EQ(dest_offset, 5);
  EXPECT_EQ(source_position_modulo, 4ul);
  EXPECT_FALSE(pos_mgr.FrameCanBeMixed());
  EXPECT_TRUE(pos_mgr.SourceIsConsumed());
}

// AdvanceToEnd is limited by source; step_size and filter lengths are high-magnitude.
TEST(PositionManagerTest, AdvanceToEndExtremeRatesAndWidths) {
  mixer::PositionManager pos_mgr(1, 1, (336 << Fixed::Format::FractionalBits) + 1,
                                 (336 << Fixed::Format::FractionalBits) + 1);

  float dest[10];
  int64_t dest_offset = 0;
  pos_mgr.SetDestValues(dest, std::size(dest), &dest_offset);

  auto step_size = (24 << Fixed::Format::FractionalBits);
  auto rate_modulo = 0ul;
  auto denominator = 1ul;
  auto source_position_modulo = 0ul;
  pos_mgr.SetRateValues(step_size, rate_modulo, denominator, &source_position_modulo);

  int16_t source[360];
  auto source_offset = Fixed::FromRaw(-1);
  pos_mgr.SetSourceValues(static_cast<void*>(source), 336, &source_offset);

  auto num_source_frames_skipped = pos_mgr.AdvanceToEnd();
  EXPECT_EQ(num_source_frames_skipped, 24);

  pos_mgr.UpdateOffsets();
  EXPECT_EQ(source_offset.raw_value(), (24 << Fixed::Format::FractionalBits) - 1);
  EXPECT_EQ(dest_offset, 1);
  EXPECT_FALSE(pos_mgr.FrameCanBeMixed());
  EXPECT_TRUE(pos_mgr.SourceIsConsumed());

  // Now try starting from just one subframe further. This should not advance.
  source_offset = Fixed(0);
  pos_mgr.SetSourceValues(static_cast<void*>(source), 336, &source_offset);

  num_source_frames_skipped = pos_mgr.AdvanceToEnd();
  EXPECT_EQ(num_source_frames_skipped, 0);

  pos_mgr.UpdateOffsets();
  EXPECT_EQ(source_offset, Fixed(0));
  EXPECT_EQ(dest_offset, 1);
  EXPECT_FALSE(pos_mgr.FrameCanBeMixed());
  EXPECT_TRUE(pos_mgr.SourceIsConsumed());

  // Back up position but subtract a frame of source supply: we should not advance.
  source_offset = Fixed::FromRaw(-1);
  pos_mgr.SetSourceValues(static_cast<void*>(source), 335, &source_offset);

  num_source_frames_skipped = pos_mgr.AdvanceToEnd();
  EXPECT_EQ(num_source_frames_skipped, 0);

  pos_mgr.UpdateOffsets();
  EXPECT_EQ(source_offset, Fixed::FromRaw(-1));
  EXPECT_EQ(dest_offset, 1);
  EXPECT_FALSE(pos_mgr.FrameCanBeMixed());
  EXPECT_TRUE(pos_mgr.SourceIsConsumed());
}

}  // namespace
}  // namespace media::audio::mixer

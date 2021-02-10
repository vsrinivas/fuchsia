// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/position_manager.h"

#include <iterator>

#include <fbl/algorithm.h>
#include <gtest/gtest.h>

namespace media::audio::mixer {
namespace {

// Produce the frame pointer (in source format) for the first frame in the source buffer.
TEST(PositionManagerTest, FirstSourceFrame) {
  auto source_chans = 2u;
  mixer::PositionManager pos_mgr(source_chans, 2u, 0, 0);

  auto source_frames = 5;
  int32_t frac_source_frames = source_frames * Mixer::FRAC_ONE;

  // Setting this to any non-nullptr value
  void* source_void_ptr = &source_frames;

  int32_t frac_source_offset = Mixer::FRAC_ONE * 3;

  pos_mgr.SetSourceValues(source_void_ptr, frac_source_frames, &frac_source_offset);

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
  mixer::PositionManager pos_mgr(source_chans, 2u, 0, 0);

  constexpr auto source_frames = 5;
  int32_t frac_source_frames = source_frames * Mixer::FRAC_ONE;

  float source[source_chans * source_frames];
  const auto source_void_ptr = static_cast<void*>(source);

  constexpr auto source_offset = 0;
  int32_t frac_source_offset = source_offset * Mixer::FRAC_ONE;

  pos_mgr.SetSourceValues(source_void_ptr, frac_source_frames, &frac_source_offset);

  auto sample_num_of_last_frame = source_chans * (source_frames - 1);
  uint8_t* expected_source_frame_u8 =
      static_cast<uint8_t*>(source_void_ptr) + sample_num_of_last_frame;
  int16_t* expected_source_frame_16 =
      static_cast<int16_t*>(source_void_ptr) + sample_num_of_last_frame;
  int32_t* expected_source_frame_32 =
      static_cast<int32_t*>(source_void_ptr) + sample_num_of_last_frame;
  float* expected_source_frame_float =
      static_cast<float*>(source_void_ptr) + sample_num_of_last_frame;

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
  mixer::PositionManager pos_mgr(source_chans, dest_chans, 0, Mixer::FRAC_ONE - 1);

  constexpr auto source_frames = 2;
  constexpr int32_t frac_source_frames = source_frames * Mixer::FRAC_ONE;

  int16_t source[source_frames * source_chans];
  auto source_void_ptr = static_cast<void*>(source);

  auto source_offset = 1;
  int32_t frac_source_offset = source_offset * Mixer::FRAC_ONE;

  pos_mgr.SetSourceValues(source_void_ptr, frac_source_frames, &frac_source_offset);

  auto expected_source_frame = source + (source_offset * source_chans);
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<int16_t>(), expected_source_frame);
  EXPECT_FALSE(pos_mgr.SourceIsConsumed());

  auto expected_source_frame_u8 =
      reinterpret_cast<uint8_t*>(source_void_ptr) + (source_offset * source_chans);
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<uint8_t>(), expected_source_frame_u8);

  auto expected_source_frame_32 =
      reinterpret_cast<int32_t*>(source_void_ptr) + (source_offset * source_chans);
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<int32_t>(), expected_source_frame_32);

  auto expected_source_frame_float =
      reinterpret_cast<float*>(source_void_ptr) + (source_offset * source_chans);
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<float>(), expected_source_frame_float);

  source_offset = 0;
  frac_source_offset = source_offset * Mixer::FRAC_ONE;
  pos_mgr.SetSourceValues(source_void_ptr, frac_source_frames, &frac_source_offset);

  expected_source_frame = source + (source_offset * source_chans);
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<int16_t>(), expected_source_frame);
  EXPECT_FALSE(pos_mgr.SourceIsConsumed());
}

// Produce the frame pointer (float*) corresponding with the current destination offset
// This should take into account the number of destination channels.
TEST(PositionManagerTest, CurrentDestFrame) {
  constexpr auto source_chans = 1u;
  constexpr auto dest_chans = 4u;
  mixer::PositionManager pos_mgr(source_chans, dest_chans, 0, Mixer::FRAC_ONE - 1);

  constexpr auto dest_frames = 2;
  float dest[dest_frames * dest_chans];
  auto dest_offset = 1u;

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
  mixer::PositionManager pos_mgr(1, 1, 0, Mixer::FRAC_ONE - 1);

  float input;
  void* source_void_ptr = static_cast<void*>(&input);
  auto frac_source_frames = Mixer::FRAC_ONE;
  int32_t frac_source_offset = 0;
  pos_mgr.SetSourceValues(source_void_ptr, frac_source_frames, &frac_source_offset);

  float data;
  float* dest = &data;
  auto dest_frames = 1u;
  uint32_t dest_offset = 0u;
  pos_mgr.SetDestValues(dest, dest_frames, &dest_offset);

  auto step_size = Mixer::FRAC_ONE;
  auto rate_modulo = 0ul;
  auto denominator = 0ul;
  auto source_position_modulo = 0ul;
  pos_mgr.SetRateValues(step_size, rate_modulo, denominator, &source_position_modulo);

  frac_source_offset = 27;
  dest_offset = 42u;
  source_position_modulo = 72;
  pos_mgr.UpdateOffsets();

  EXPECT_EQ(frac_source_offset, 0);
  EXPECT_EQ(dest_offset, 0u);
  EXPECT_EQ(source_position_modulo, 72ul);

  source_position_modulo = 0;
  pos_mgr.SetRateValues(step_size, 1, 2, &source_position_modulo);

  source_position_modulo = 72;
  pos_mgr.UpdateOffsets();

  EXPECT_EQ(source_position_modulo, 0ul);
}

TEST(PositionManagerTest, FrameCanBeMixed) {
  constexpr auto source_chans = 1u;
  constexpr auto dest_chans = 1u;
  mixer::PositionManager pos_mgr(source_chans, dest_chans, Mixer::FRAC_ONE >> 1,
                                 Mixer::FRAC_ONE >> 1);

  int16_t source[2 * source_chans];
  const auto frac_source_frames = 2 * Mixer::FRAC_ONE;
  const auto source_void_ptr = static_cast<void*>(source);
  int32_t frac_source_offset = (3 * (Mixer::FRAC_ONE >> 1)) - 1;
  pos_mgr.SetSourceValues(source_void_ptr, frac_source_frames, &frac_source_offset);

  float dest[2u * dest_chans];
  auto dest_offset = 1u;
  pos_mgr.SetDestValues(dest, 2, &dest_offset);
  EXPECT_TRUE(pos_mgr.FrameCanBeMixed());
  EXPECT_FALSE(pos_mgr.SourceIsConsumed());

  ++frac_source_offset;
  pos_mgr.SetSourceValues(source_void_ptr, frac_source_frames, &frac_source_offset);
  EXPECT_FALSE(pos_mgr.FrameCanBeMixed());
  EXPECT_TRUE(pos_mgr.SourceIsConsumed());
}

TEST(PositionManagerTest, AdvanceFrame_Basic) {
  mixer::PositionManager pos_mgr(1, 1, 0, Mixer::FRAC_ONE - 1);

  uint8_t source[3];
  int32_t frac_source_offset = Mixer::FRAC_ONE;
  pos_mgr.SetSourceValues(static_cast<void*>(source), 3 * Mixer::FRAC_ONE, &frac_source_offset);

  float dest[3];
  auto dest_offset = 1u;
  pos_mgr.SetDestValues(dest, 3, &dest_offset);

  auto source_position_modulo = 0ul;
  pos_mgr.SetRateValues(Mixer::FRAC_ONE, 0, 1, &source_position_modulo);

  int32_t expected_frac_source_off = frac_source_offset + Mixer::FRAC_ONE;
  auto received_frac_source_off = pos_mgr.AdvanceFrame();
  EXPECT_EQ(received_frac_source_off, expected_frac_source_off);
  EXPECT_TRUE(pos_mgr.FrameCanBeMixed());
  EXPECT_FALSE(pos_mgr.SourceIsConsumed());
}

TEST(PositionManagerTest, AdvanceFrame_SourceReachesEnd) {
  mixer::PositionManager pos_mgr(1, 1, 0, Mixer::FRAC_ONE - 1);

  int32_t source[2];
  int32_t frac_source_offset = Mixer::FRAC_ONE;
  pos_mgr.SetSourceValues(static_cast<void*>(source), 2 * Mixer::FRAC_ONE, &frac_source_offset);

  float dest[3u];
  auto dest_offset = 1u;
  pos_mgr.SetDestValues(dest, 3, &dest_offset);

  auto source_position_modulo = 0ul;
  pos_mgr.SetRateValues(Mixer::FRAC_ONE, 0, 1, &source_position_modulo);

  int32_t expected_frac_source_off = frac_source_offset + Mixer::FRAC_ONE;
  auto received_frac_source_off = pos_mgr.AdvanceFrame();
  EXPECT_EQ(received_frac_source_off, expected_frac_source_off);
  EXPECT_FALSE(pos_mgr.FrameCanBeMixed());
  EXPECT_TRUE(pos_mgr.SourceIsConsumed());
}

TEST(PositionManagerTest, AdvanceFrame_SourceModuloReachesEnd) {
  mixer::PositionManager pos_mgr(1, 1, 0, Mixer::FRAC_ONE - 1);

  int16_t source[3];
  int32_t frac_source_offset = (2 * Mixer::FRAC_ONE) - 1;
  pos_mgr.SetSourceValues(static_cast<void*>(source), 3 * Mixer::FRAC_ONE, &frac_source_offset);

  float dest[3];
  auto dest_offset = 1u;
  pos_mgr.SetDestValues(dest, 3, &dest_offset);

  auto step_size = Mixer::FRAC_ONE;
  auto rate_modulo = 1ul;
  auto denominator = 243ul;
  auto source_position_modulo = 242ul;
  pos_mgr.SetRateValues(step_size, rate_modulo, denominator, &source_position_modulo);

  EXPECT_TRUE(pos_mgr.FrameCanBeMixed());
  EXPECT_FALSE(pos_mgr.SourceIsConsumed());
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<int16_t>(), &source[1]);
  EXPECT_TRUE(pos_mgr.frac_source_offset() == (2 * Mixer::FRAC_ONE) - 1);

  int32_t expected_frac_source_off = 3 * Mixer::FRAC_ONE;
  auto received_frac_source_off = pos_mgr.AdvanceFrame();
  EXPECT_EQ(received_frac_source_off, expected_frac_source_off);
  EXPECT_EQ(pos_mgr.frac_source_offset(), received_frac_source_off);

  EXPECT_FALSE(pos_mgr.FrameCanBeMixed());
  EXPECT_TRUE(pos_mgr.SourceIsConsumed());
  EXPECT_TRUE(received_frac_source_off == 3 * Mixer::FRAC_ONE);
}

TEST(PositionManagerTest, AdvanceFrame_SourceModuloAlmostReachesEnd) {
  mixer::PositionManager pos_mgr(1, 1, 0, Mixer::FRAC_ONE - 1);

  float source[3];
  int32_t frac_source_offset = (2 * Mixer::FRAC_ONE) - 1;
  pos_mgr.SetSourceValues(static_cast<void*>(source), 3 * Mixer::FRAC_ONE, &frac_source_offset);

  float dest[3];
  auto dest_offset = 1u;
  pos_mgr.SetDestValues(dest, 3, &dest_offset);

  auto step_size = Mixer::FRAC_ONE;
  auto rate_modulo = 1ul;
  auto denominator = 243ul;
  auto source_position_modulo = 241ul;
  pos_mgr.SetRateValues(step_size, rate_modulo, denominator, &source_position_modulo);

  EXPECT_TRUE(pos_mgr.FrameCanBeMixed());
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<float>(), &source[1]);
  EXPECT_TRUE(pos_mgr.frac_source_offset() == (2 * Mixer::FRAC_ONE) - 1);

  int32_t expected_frac_source_off = frac_source_offset + Mixer::FRAC_ONE;
  auto received_frac_source_off = pos_mgr.AdvanceFrame();
  EXPECT_EQ(received_frac_source_off, expected_frac_source_off);
  EXPECT_EQ(pos_mgr.frac_source_offset(), received_frac_source_off);

  EXPECT_TRUE(pos_mgr.FrameCanBeMixed());
  EXPECT_FALSE(pos_mgr.SourceIsConsumed());
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<float>(), &source[2]);
  EXPECT_TRUE(received_frac_source_off == (3 * Mixer::FRAC_ONE) - 1);
}

TEST(PositionManagerTest, AdvanceFrame_DestReachesEnd) {
  mixer::PositionManager pos_mgr(1, 1, 0, Mixer::FRAC_ONE - 1);

  int16_t source[3];
  const uint32_t frac_source_frames = std::size(source) * Mixer::FRAC_ONE;
  int32_t frac_source_offset = Mixer::FRAC_ONE;
  const int32_t expected_frac_source_off = frac_source_offset + Mixer::FRAC_ONE;

  pos_mgr.SetSourceValues(static_cast<void*>(source), frac_source_frames, &frac_source_offset);

  float dest[2];
  const uint32_t dest_frames = std::size(dest);
  auto dest_offset = 1u;
  pos_mgr.SetDestValues(dest, dest_frames, &dest_offset);

  auto source_position_modulo = 0ul;
  pos_mgr.SetRateValues(Mixer::FRAC_ONE, 0, 1, &source_position_modulo);

  auto received_frac_source_off = pos_mgr.AdvanceFrame();

  EXPECT_EQ(received_frac_source_off, expected_frac_source_off);
  EXPECT_EQ(pos_mgr.frac_source_offset(), received_frac_source_off);

  EXPECT_FALSE(pos_mgr.FrameCanBeMixed());
  EXPECT_FALSE(pos_mgr.SourceIsConsumed());
}

TEST(PositionManagerTest, AdvanceFrame_TemplateNoModulo) {
  mixer::PositionManager pos_mgr(1, 1, 0, Mixer::FRAC_ONE - 1);

  int16_t source[3];
  int32_t frac_source_offset = (2 * Mixer::FRAC_ONE) - 1;
  pos_mgr.SetSourceValues(static_cast<void*>(source), 3 * Mixer::FRAC_ONE, &frac_source_offset);

  float dest[3];
  auto dest_offset = 1u;
  pos_mgr.SetDestValues(dest, 3, &dest_offset);

  auto step_size = Mixer::FRAC_ONE;
  auto rate_modulo = 1ul;
  auto denominator = 243ul;
  auto source_position_modulo = 242ul;
  pos_mgr.SetRateValues(step_size, rate_modulo, denominator, &source_position_modulo);

  EXPECT_TRUE(pos_mgr.FrameCanBeMixed());
  EXPECT_FALSE(pos_mgr.SourceIsConsumed());
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<int16_t>(), &source[1]);
  EXPECT_TRUE(pos_mgr.frac_source_offset() == (2 * Mixer::FRAC_ONE) - 1);

  int32_t expected_frac_source_off = (3 * Mixer::FRAC_ONE) - 1;
  auto received_frac_source_off = pos_mgr.AdvanceFrame<false>();

  EXPECT_EQ(received_frac_source_off, expected_frac_source_off);
  EXPECT_EQ(pos_mgr.frac_source_offset(), received_frac_source_off);

  EXPECT_TRUE(pos_mgr.FrameCanBeMixed());
  EXPECT_FALSE(pos_mgr.SourceIsConsumed());
}

TEST(PositionManagerTest, AdvanceFrame_NoRateValues) {
  mixer::PositionManager pos_mgr(1, 1, 0, Mixer::FRAC_ONE - 1);

  int16_t source[3];
  int32_t frac_source_offset = (2 * Mixer::FRAC_ONE) - 1;
  pos_mgr.SetSourceValues(static_cast<void*>(source), 3 * Mixer::FRAC_ONE, &frac_source_offset);

  float dest[3];
  auto dest_offset = 1u;
  pos_mgr.SetDestValues(dest, 3, &dest_offset);

  EXPECT_TRUE(pos_mgr.FrameCanBeMixed());
  EXPECT_FALSE(pos_mgr.SourceIsConsumed());
  EXPECT_EQ(pos_mgr.CurrentSourceFrame<int16_t>(), &source[1]);
  EXPECT_TRUE(pos_mgr.frac_source_offset() == (2 * Mixer::FRAC_ONE) - 1);

  int32_t expected_frac_source_off = (3 * Mixer::FRAC_ONE) - 1;
  auto received_frac_source_off = pos_mgr.AdvanceFrame<false>();
  pos_mgr.UpdateOffsets();

  EXPECT_EQ(received_frac_source_off, expected_frac_source_off);
  EXPECT_EQ(pos_mgr.frac_source_offset(), received_frac_source_off);

  EXPECT_TRUE(pos_mgr.FrameCanBeMixed());
  EXPECT_FALSE(pos_mgr.SourceIsConsumed());
}

TEST(PositionManagerTest, AdvanceToEnd_Dest) {
  mixer::PositionManager pos_mgr(1, 1, 0, Mixer::FRAC_ONE - 1);

  int16_t source[12];
  const uint32_t frac_source_frames = std::size(source) * Mixer::FRAC_ONE;
  int32_t frac_source_offset = Mixer::FRAC_ONE - 1;
  pos_mgr.SetSourceValues(static_cast<void*>(source), frac_source_frames, &frac_source_offset);

  float dest[5];
  auto dest_offset = 0u;
  pos_mgr.SetDestValues(dest, std::size(dest), &dest_offset);

  auto source_position_modulo = 1ul;
  auto denominator = 2ul;
  pos_mgr.SetRateValues((Mixer::FRAC_ONE << 1) - 1, 0, denominator, &source_position_modulo);

  auto num_source_frames_skipped = pos_mgr.AdvanceToEnd<false>();
  EXPECT_EQ(num_source_frames_skipped, 10u);

  pos_mgr.UpdateOffsets();

  EXPECT_TRUE(frac_source_offset == (11 * Mixer::FRAC_ONE) - 6);
  EXPECT_EQ(dest_offset, 5u);
  EXPECT_EQ(source_position_modulo, 1ul);
  EXPECT_FALSE(pos_mgr.FrameCanBeMixed());
  EXPECT_FALSE(pos_mgr.SourceIsConsumed());
}

TEST(PositionManagerTest, AdvanceToEnd_SourceBasic) {
  mixer::PositionManager pos_mgr(1, 1, Mixer::FRAC_ONE >> 1, Mixer::FRAC_ONE >> 1);

  int16_t source[5];
  auto frac_source_offset = 0;
  pos_mgr.SetSourceValues(static_cast<void*>(source), 5 * Mixer::FRAC_ONE, &frac_source_offset);

  float dest[10];
  auto dest_offset = 0u;
  pos_mgr.SetDestValues(dest, 10, &dest_offset);

  auto source_position_modulo = 0ul;
  pos_mgr.SetRateValues(Mixer::FRAC_ONE >> 1, 0, 1, &source_position_modulo);

  auto num_source_frames_skipped = pos_mgr.AdvanceToEnd();
  EXPECT_EQ(num_source_frames_skipped, 5u);

  pos_mgr.UpdateOffsets();

  EXPECT_TRUE(frac_source_offset == 9 * (Mixer::FRAC_ONE >> 1));
  EXPECT_EQ(dest_offset, 9u);
  EXPECT_EQ(source_position_modulo, 0u);
  EXPECT_FALSE(pos_mgr.FrameCanBeMixed());
  EXPECT_TRUE(pos_mgr.SourceIsConsumed());
}

TEST(PositionManagerTest, AdvanceToEnd_SourceExactModulo) {
  mixer::PositionManager pos_mgr(1, 1, 0, Mixer::FRAC_ONE >> 1);

  int16_t source[11];
  int32_t frac_source_offset = Mixer::FRAC_ONE - 1;
  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source) * Mixer::FRAC_ONE,
                          &frac_source_offset);

  float dest[6];
  auto dest_offset = 0u;
  pos_mgr.SetDestValues(dest, std::size(dest), &dest_offset);

  auto rate_modulo = 1ul;
  auto denominator = 25ul;
  auto source_position_modulo = 20ul;
  pos_mgr.SetRateValues(Mixer::FRAC_ONE << 1, rate_modulo, denominator, &source_position_modulo);

  auto num_source_frames_skipped = pos_mgr.AdvanceToEnd();
  EXPECT_EQ(num_source_frames_skipped, 11u);

  pos_mgr.UpdateOffsets();

  EXPECT_TRUE(frac_source_offset == 11 * Mixer::FRAC_ONE);
  EXPECT_EQ(dest_offset, 5u);
  EXPECT_EQ(source_position_modulo, 0ul);
  EXPECT_FALSE(pos_mgr.FrameCanBeMixed());
  EXPECT_TRUE(pos_mgr.SourceIsConsumed());
}

TEST(PositionManagerTest, AdvanceToEnd_SourceExtraModulo) {
  mixer::PositionManager pos_mgr(1, 1, 0, Mixer::FRAC_ONE >> 1);

  int16_t source[11];
  int32_t frac_source_offset = Mixer::FRAC_ONE - 1;
  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source) * Mixer::FRAC_ONE,
                          &frac_source_offset);

  float dest[6];
  auto dest_offset = 0u;
  pos_mgr.SetDestValues(dest, std::size(dest), &dest_offset);

  auto rate_modulo = 1ul;
  auto denominator = 25ul;
  auto source_position_modulo = 24ul;
  pos_mgr.SetRateValues(Mixer::FRAC_ONE << 1, rate_modulo, denominator, &source_position_modulo);

  auto num_source_frames_skipped = pos_mgr.AdvanceToEnd();
  EXPECT_EQ(num_source_frames_skipped, 11u);

  pos_mgr.UpdateOffsets();

  EXPECT_TRUE(frac_source_offset == 11 * Mixer::FRAC_ONE);
  EXPECT_EQ(dest_offset, 5u);
  EXPECT_EQ(source_position_modulo, 4ul);
  EXPECT_FALSE(pos_mgr.FrameCanBeMixed());
  EXPECT_TRUE(pos_mgr.SourceIsConsumed());
}

// If the template parameter to AdvanceToEnd is <false>, then even a non-zero rate_modulo should not
// affect source position and source_pos_modulo should be unchanged.
TEST(PositionManagerTest, AdvanceToEnd_TemplateFalseShouldNotUseRateModulo) {
  mixer::PositionManager pos_mgr(1, 1, 0, Mixer::FRAC_ONE - 1);

  int16_t source[11];
  int32_t frac_source_offset = Mixer::FRAC_ONE - 1;
  pos_mgr.SetSourceValues(static_cast<void*>(source), std::size(source) * Mixer::FRAC_ONE,
                          &frac_source_offset);

  float dest[7];
  auto dest_offset = 0u;
  pos_mgr.SetDestValues(dest, std::size(dest), &dest_offset);

  auto rate_modulo = 1ul;
  auto denominator = 25ul;
  auto source_position_modulo = 20ul;
  pos_mgr.SetRateValues(Mixer::FRAC_ONE << 1, rate_modulo, denominator, &source_position_modulo);

  // pos_width 0, neg_width 0.5
  // step_size 2.0, source_pos 0.999 of 11, dest_pos 0 of 7
  // ignoring rate_modulo, we should take 6 steps, ending at source_pos 12.999, dest_pos 6
  // after only 5 steps, source_pos is 10.999 and (with pos_width of 0) can be advanced one more.
  // if we erroneously do incorporate rate_modulo, this will be enough to tip source_pos to 11.000,
  // and we would stop after an advance of only 5 dest frames.
  auto num_source_frames_skipped = pos_mgr.AdvanceToEnd<false>();
  EXPECT_EQ(num_source_frames_skipped, 12u);

  // This will be overwritten by the previously-provided unchanged value.
  source_position_modulo = 42;
  pos_mgr.UpdateOffsets();

  EXPECT_TRUE(frac_source_offset == (13 * Mixer::FRAC_ONE) - 1) << std::hex << frac_source_offset;
  EXPECT_EQ(dest_offset, 6u);
  EXPECT_EQ(source_position_modulo, 20ul);
  EXPECT_FALSE(pos_mgr.FrameCanBeMixed());
  EXPECT_TRUE(pos_mgr.SourceIsConsumed());
}

// TODO(mpuryear): unittest to validate the ctor's "frac_bits" param (used only by AdvanceToEnd, and
// only set to the non-default value for testing and debugging).

}  // namespace
}  // namespace media::audio::mixer

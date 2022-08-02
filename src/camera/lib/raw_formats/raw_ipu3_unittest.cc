// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/camera/lib/raw_formats/raw_ipu3.h"

#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/assert.h>

#include <cstdint>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/camera/lib/raw_formats/raw_lookups.h"

namespace camera::raw {
namespace {

TEST(RawIpu3Test, RawIpu3PackingBlockCorrect) {
  static_assert(kIpu3FormatBGGR10.packing_block.size() == 32u);
  static_assert(kIpu3FormatBGGR10.packing_block.num_bits() == 256u);
  static_assert(kIpu3FormatBGGR10.packing_block.num_pixels() == 25u);
  static_assert(kIpu3FormatBGGR10.packing_block.type() == ChunkType::PACKING_BLOCK);
  static_assert(kIpu3FormatBGGR10.packing_block.repeat().type == ChunkRepeatType::FILL_IMAGE);
  static_assert(kIpu3FormatBGGR10.packing_block.chunks().size() == 2u);

  constexpr const PackingBlock& block0 =
      Chunk::get<PackingBlock>(kIpu3FormatBGGR10.packing_block.chunks()[0]);
  static_assert(block0.size() == 5u);
  static_assert(block0.num_bits() == 40u);
  static_assert(block0.num_pixels() == 4);
  static_assert(block0.type() == ChunkType::PACKING_BLOCK);
  static_assert(block0.repeat().type == ChunkRepeatType::FINITE);
  static_assert(block0.repeat().times == 6);

  constexpr const PixelPiece& p80 = Chunk::get<PixelPiece>(block0.chunks()[0]);
  static_assert(p80.pixel_index() == 0);
  static_assert(p80.num_bits() == 8);
  static_assert(p80.shift() == 0);

  constexpr const PixelPiece& p61 = Chunk::get<PixelPiece>(block0.chunks()[1]);
  static_assert(p61.pixel_index() == 1);
  static_assert(p61.num_bits() == 6);
  static_assert(p61.shift() == -2);

  constexpr const PixelPiece& P20 = Chunk::get<PixelPiece>(block0.chunks()[2]);
  static_assert(P20.pixel_index() == 0);
  static_assert(P20.num_bits() == 2);
  static_assert(P20.shift() == 8);

  constexpr const PixelPiece& p42 = Chunk::get<PixelPiece>(block0.chunks()[3]);
  static_assert(p42.pixel_index() == 2);
  static_assert(p42.num_bits() == 4);
  static_assert(p42.shift() == -4);

  constexpr const PixelPiece& P41 = Chunk::get<PixelPiece>(block0.chunks()[4]);
  static_assert(P41.pixel_index() == 1);
  static_assert(P41.num_bits() == 4);
  static_assert(P41.shift() == 6);

  constexpr const PixelPiece& p23 = Chunk::get<PixelPiece>(block0.chunks()[5]);
  static_assert(p23.pixel_index() == 3);
  static_assert(p23.num_bits() == 2);
  static_assert(p23.shift() == -6);

  constexpr const PixelPiece& P62 = Chunk::get<PixelPiece>(block0.chunks()[6]);
  static_assert(P62.pixel_index() == 2);
  static_assert(P62.num_bits() == 6);
  static_assert(P62.shift() == 4);

  constexpr const PixelPiece& P83 = Chunk::get<PixelPiece>(block0.chunks()[7]);
  static_assert(P83.pixel_index() == 3);
  static_assert(P83.num_bits() == 8);
  static_assert(P83.shift() == 2);

  constexpr const PackingBlock& block1 =
      Chunk::get<PackingBlock>(kIpu3FormatBGGR10.packing_block.chunks()[1]);
  static_assert(block1.size() == 2u);
  static_assert(block1.num_bits() == 16u);
  static_assert(block1.num_pixels() == 1);
  static_assert(block1.type() == ChunkType::PACKING_BLOCK);
  static_assert(block1.repeat().type == ChunkRepeatType::FINITE);
  static_assert(block1.repeat().times == 1);

  constexpr const PixelPiece& p80_2 = Chunk::get<PixelPiece>(block1.chunks()[0]);
  static_assert(p80_2.pixel_index() == 0);
  static_assert(p80_2.num_bits() == 8);
  static_assert(p80_2.shift() == 0);

  constexpr const Padding& pad = Chunk::get<Padding>(block1.chunks()[1]);
  static_assert(pad.num_bits() == 6);

  constexpr const PixelPiece& P20_2 = Chunk::get<PixelPiece>(block1.chunks()[2]);
  static_assert(P20_2.pixel_index() == 0);
  static_assert(P20_2.num_bits() == 2);
  static_assert(P20_2.shift() == 8);
}

TEST(RawIpu3Test, InstanceCreationCorrect) {
  RawFormatInstance format_ipu3_bggr10_640x480 = CreateFormatInstance(kIpu3FormatBGGR10, 640, 480);

  EXPECT_EQ(format_ipu3_bggr10_640x480.width, 640u);
  EXPECT_EQ(format_ipu3_bggr10_640x480.height, 480u);
  EXPECT_EQ(format_ipu3_bggr10_640x480.row_stride, std::nullopt);
  EXPECT_EQ(format_ipu3_bggr10_640x480.size, 393216u);

  EXPECT_EQ(format_ipu3_bggr10_640x480.packing_block.size(), 32u);
  EXPECT_EQ(format_ipu3_bggr10_640x480.packing_block.num_bits(), 256u);
  EXPECT_EQ(format_ipu3_bggr10_640x480.packing_block.num_pixels(), 25u);
  EXPECT_EQ(format_ipu3_bggr10_640x480.packing_block.type(), ChunkType::PACKING_BLOCK);
  EXPECT_EQ(format_ipu3_bggr10_640x480.packing_block.repeat().type, ChunkRepeatType::FINITE);
  EXPECT_EQ(format_ipu3_bggr10_640x480.packing_block.repeat().times, 12288u);
  EXPECT_EQ(format_ipu3_bggr10_640x480.packing_block.chunks().size(), 2u);
}

}  // namespace
}  // namespace camera::raw

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/camera/lib/raw_formats/raw10.h"

#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/assert.h>

#include <cstdint>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/camera/lib/raw_formats/raw_lookups.h"

namespace camera::raw {
namespace {

// Used to pack a small sample image.
#define RAW10_PACK_LSB(a, b, c, d) (((d & 3) << 6) + ((c & 3) << 4) + ((b & 3) << 2) + (a & 3))

#define RAW10_PACK(a, b, c, d)                                                \
  (uint8_t)(a >> 2), (uint8_t)(b >> 2), (uint8_t)(c >> 2), (uint8_t)(d >> 2), \
      (uint8_t)RAW10_PACK_LSB(a, b, c, d)

uint8_t kBlueHighBits = 0b10101010;
uint16_t kBlueBits = 0b1010101000;

uint8_t kGreen0HighBits = 0b11001100;
uint16_t kGreen0Bits = 0b1100110001;

uint8_t kBlueGreen0LowBits = 0b01000100;

uint8_t kGreen1HighBits = 0b00110011;
uint16_t kGreen1Bits = 0b0011001110;

int8_t kRedHighBits = 0b01010101;
uint16_t kRedBits = 0b0101010111;

uint8_t kGreen1RedLowBits = 0b11101110;

uint32_t kImageBGGRWidth = 8;
uint32_t kImageBGGRHeight = 8;
uint32_t kImageBGGRStride = 16;
size_t kImageBGGRSize = kImageBGGRStride * kImageBGGRHeight;

uint8_t kImageBGGR[] = {
    // clang-format off
  RAW10_PACK(kBlueBits, kGreen0Bits, kBlueBits, kGreen0Bits), RAW10_PACK(kBlueBits, kGreen0Bits, kBlueBits, kGreen0Bits), 0,0,0,0,0,0,
  RAW10_PACK(kGreen1Bits, kRedBits, kGreen1Bits, kRedBits),   RAW10_PACK(kGreen1Bits, kRedBits, kGreen1Bits, kRedBits),   0,0,0,0,0,0,
  RAW10_PACK(kBlueBits, kGreen0Bits, kBlueBits, kGreen0Bits), RAW10_PACK(kBlueBits, kGreen0Bits, kBlueBits, kGreen0Bits), 0,0,0,0,0,0,
  RAW10_PACK(kGreen1Bits, kRedBits, kGreen1Bits, kRedBits),   RAW10_PACK(kGreen1Bits, kRedBits, kGreen1Bits, kRedBits),   0,0,0,0,0,0,
  RAW10_PACK(kBlueBits, kGreen0Bits, kBlueBits, kGreen0Bits), RAW10_PACK(kBlueBits, kGreen0Bits, kBlueBits, kGreen0Bits), 0,0,0,0,0,0,
  RAW10_PACK(kGreen1Bits, kRedBits, kGreen1Bits, kRedBits),   RAW10_PACK(kGreen1Bits, kRedBits, kGreen1Bits, kRedBits),   0,0,0,0,0,0,
  RAW10_PACK(kBlueBits, kGreen0Bits, kBlueBits, kGreen0Bits), RAW10_PACK(kBlueBits, kGreen0Bits, kBlueBits, kGreen0Bits), 0,0,0,0,0,0,
  RAW10_PACK(kGreen1Bits, kRedBits, kGreen1Bits, kRedBits),   RAW10_PACK(kGreen1Bits, kRedBits, kGreen1Bits, kRedBits),   0,0,0,0,0,0,
    // clang-format on
};

TEST(Raw10Test, TestImagePackedCorrectly) {
  EXPECT_EQ(kImageBGGR[0], kBlueHighBits);
  EXPECT_EQ(kImageBGGR[1], kGreen0HighBits);
  EXPECT_EQ(kImageBGGR[2], kBlueHighBits);
  EXPECT_EQ(kImageBGGR[3], kGreen0HighBits);

  EXPECT_EQ(kImageBGGR[4], kBlueGreen0LowBits);

  EXPECT_EQ(kImageBGGR[16], kGreen1HighBits);
  EXPECT_EQ(kImageBGGR[17], kRedHighBits);
  EXPECT_EQ(kImageBGGR[18], kGreen1HighBits);
  EXPECT_EQ(kImageBGGR[19], kRedHighBits);

  EXPECT_EQ(kImageBGGR[20], kGreen1RedLowBits);
}

TEST(Raw10Test, Raw10PackingBlockCorrect) {
  static_assert(kRaw10FormatBGGR.packing_block.size() == 0u);
  static_assert(kRaw10FormatBGGR.packing_block.num_bits() == 0u);
  static_assert(kRaw10FormatBGGR.packing_block.num_pixels() == 0u);
  static_assert(kRaw10FormatBGGR.packing_block.type() == ChunkType::PACKING_BLOCK);
  static_assert(kRaw10FormatBGGR.packing_block.repeat().type == ChunkRepeatType::FILL_IMAGE);
  static_assert(kRaw10FormatBGGR.packing_block.chunks().size() == 2u);

  constexpr const PackingBlock& pixel_block =
      Chunk::get<PackingBlock>(kRaw10FormatBGGR.packing_block.chunks()[0]);
  static_assert(pixel_block.type() == ChunkType::PACKING_BLOCK);
  static_assert(pixel_block.repeat().type == ChunkRepeatType::FILL_WIDTH);
  static_assert(pixel_block.num_bits() == 40u);
  static_assert(pixel_block.size() == 5u);
  static_assert(pixel_block.num_pixels() == 4u);
  static_assert(pixel_block.chunks().size() == 8u);

  constexpr const PixelPiece& p00 = Chunk::get<PixelPiece>(pixel_block.chunks()[0]);
  static_assert(p00.type() == ChunkType::PIXEL_PIECE);
  static_assert(p00.repeat().type == ChunkRepeatType::FINITE);
  static_assert(p00.repeat().times == 1u);
  static_assert(p00.num_bits() == 8u);
  static_assert(p00.pixel_index() == 0u);

  constexpr const PixelPiece& p10 = Chunk::get<PixelPiece>(pixel_block.chunks()[1]);
  static_assert(p10.type() == ChunkType::PIXEL_PIECE);
  static_assert(p10.repeat().type == ChunkRepeatType::FINITE);
  static_assert(p10.repeat().times == 1u);
  static_assert(p10.num_bits() == 8u);
  static_assert(p10.pixel_index() == 1u);

  constexpr const PixelPiece& p20 = Chunk::get<PixelPiece>(pixel_block.chunks()[2]);
  static_assert(p20.type() == ChunkType::PIXEL_PIECE);
  static_assert(p20.repeat().type == ChunkRepeatType::FINITE);
  static_assert(p20.repeat().times == 1u);
  static_assert(p20.num_bits() == 8u);
  static_assert(p20.pixel_index() == 2u);

  constexpr const PixelPiece& p30 = Chunk::get<PixelPiece>(pixel_block.chunks()[3]);
  static_assert(p30.type() == ChunkType::PIXEL_PIECE);
  static_assert(p30.repeat().type == ChunkRepeatType::FINITE);
  static_assert(p30.repeat().times == 1u);
  static_assert(p30.num_bits() == 8u);
  static_assert(p30.pixel_index() == 3u);

  constexpr const PixelPiece& p31 = Chunk::get<PixelPiece>(pixel_block.chunks()[4]);
  static_assert(p31.type() == ChunkType::PIXEL_PIECE);
  static_assert(p31.repeat().type == ChunkRepeatType::FINITE);
  static_assert(p31.repeat().times == 1u);
  static_assert(p31.num_bits() == 2u);
  static_assert(p31.pixel_index() == 3u);

  constexpr const PixelPiece& p21 = Chunk::get<PixelPiece>(pixel_block.chunks()[5]);
  static_assert(p21.type() == ChunkType::PIXEL_PIECE);
  static_assert(p21.repeat().type == ChunkRepeatType::FINITE);
  static_assert(p21.repeat().times == 1u);
  static_assert(p21.num_bits() == 2u);
  static_assert(p21.pixel_index() == 2u);

  constexpr const PixelPiece& p11 = Chunk::get<PixelPiece>(pixel_block.chunks()[6]);
  static_assert(p11.type() == ChunkType::PIXEL_PIECE);
  static_assert(p11.repeat().type == ChunkRepeatType::FINITE);
  static_assert(p11.repeat().times == 1u);
  static_assert(p11.num_bits() == 2u);
  static_assert(p11.pixel_index() == 1u);

  constexpr const PixelPiece& p01 = Chunk::get<PixelPiece>(pixel_block.chunks()[7]);
  static_assert(p01.type() == ChunkType::PIXEL_PIECE);
  static_assert(p01.repeat().type == ChunkRepeatType::FINITE);
  static_assert(p01.repeat().times == 1u);
  static_assert(p01.num_bits() == 2u);
  static_assert(p01.pixel_index() == 0u);

  constexpr const Padding& padding_block =
      Chunk::get<Padding>(kRaw10FormatBGGR.packing_block.chunks()[1]);
  static_assert(padding_block.type() == ChunkType::PADDING);
  static_assert(padding_block.repeat().type == ChunkRepeatType::FILL_STRIDE);
  static_assert(padding_block.num_bits() == 0u);
}

TEST(Raw10Test, InstanceCreationCorrect) {
  RawFormatInstance formatRaw10_8x8_16 =
      CreateFormatInstance(kRaw10FormatBGGR, kImageBGGRWidth, kImageBGGRHeight, kImageBGGRStride);

  EXPECT_EQ(formatRaw10_8x8_16.width, kImageBGGRWidth);
  EXPECT_EQ(formatRaw10_8x8_16.height, kImageBGGRHeight);
  EXPECT_EQ(formatRaw10_8x8_16.row_stride, kImageBGGRStride);
  EXPECT_EQ(formatRaw10_8x8_16.size, kImageBGGRSize);

  const PackingBlock& packing_block = formatRaw10_8x8_16.packing_block;
  EXPECT_EQ(packing_block.type(), ChunkType::PACKING_BLOCK);
  EXPECT_EQ(packing_block.repeat().type, ChunkRepeatType::FINITE);
  EXPECT_EQ(packing_block.repeat().times, 8u);
  EXPECT_EQ(packing_block.num_bits(), 128u);
  EXPECT_EQ(packing_block.size(), static_cast<uint64_t>(kImageBGGRStride));
  EXPECT_EQ(packing_block.num_pixels(), 8u);
  EXPECT_EQ(packing_block.chunks().size(), 2u);

  const PackingBlock& pixel_block = Chunk::get<PackingBlock>(packing_block.chunks()[0]);
  EXPECT_EQ(pixel_block.type(), ChunkType::PACKING_BLOCK);
  EXPECT_EQ(pixel_block.repeat().type, ChunkRepeatType::FINITE);
  EXPECT_EQ(pixel_block.repeat().times, 2u);
  EXPECT_EQ(pixel_block.num_bits(), 40u);
  EXPECT_EQ(pixel_block.size(), 5u);
  EXPECT_EQ(pixel_block.num_pixels(), 4u);
  EXPECT_EQ(pixel_block.chunks().size(), 8u);

  const PixelPiece& p00 = Chunk::get<PixelPiece>(pixel_block.chunks()[0]);
  EXPECT_EQ(p00.type(), ChunkType::PIXEL_PIECE);
  EXPECT_EQ(p00.repeat().type, ChunkRepeatType::FINITE);
  EXPECT_EQ(p00.repeat().times, 1u);
  EXPECT_EQ(p00.num_bits(), 8u);
  EXPECT_EQ(p00.pixel_index(), 0u);

  const PixelPiece& p10 = Chunk::get<PixelPiece>(pixel_block.chunks()[1]);
  EXPECT_EQ(p10.type(), ChunkType::PIXEL_PIECE);
  EXPECT_EQ(p10.repeat().type, ChunkRepeatType::FINITE);
  EXPECT_EQ(p10.repeat().times, 1u);
  EXPECT_EQ(p10.num_bits(), 8u);
  EXPECT_EQ(p10.pixel_index(), 1u);

  const PixelPiece& p20 = Chunk::get<PixelPiece>(pixel_block.chunks()[2]);
  EXPECT_EQ(p20.type(), ChunkType::PIXEL_PIECE);
  EXPECT_EQ(p20.repeat().type, ChunkRepeatType::FINITE);
  EXPECT_EQ(p20.repeat().times, 1u);
  EXPECT_EQ(p20.num_bits(), 8u);
  EXPECT_EQ(p20.pixel_index(), 2u);

  const PixelPiece& p30 = Chunk::get<PixelPiece>(pixel_block.chunks()[3]);
  EXPECT_EQ(p30.type(), ChunkType::PIXEL_PIECE);
  EXPECT_EQ(p30.repeat().type, ChunkRepeatType::FINITE);
  EXPECT_EQ(p30.repeat().times, 1u);
  EXPECT_EQ(p30.num_bits(), 8u);
  EXPECT_EQ(p30.pixel_index(), 3u);

  const PixelPiece& p31 = Chunk::get<PixelPiece>(pixel_block.chunks()[4]);
  EXPECT_EQ(p31.type(), ChunkType::PIXEL_PIECE);
  EXPECT_EQ(p31.repeat().type, ChunkRepeatType::FINITE);
  EXPECT_EQ(p31.repeat().times, 1u);
  EXPECT_EQ(p31.num_bits(), 2u);
  EXPECT_EQ(p31.pixel_index(), 3u);

  const PixelPiece& p21 = Chunk::get<PixelPiece>(pixel_block.chunks()[5]);
  EXPECT_EQ(p21.type(), ChunkType::PIXEL_PIECE);
  EXPECT_EQ(p21.repeat().type, ChunkRepeatType::FINITE);
  EXPECT_EQ(p21.repeat().times, 1u);
  EXPECT_EQ(p21.num_bits(), 2u);
  EXPECT_EQ(p21.pixel_index(), 2u);

  const PixelPiece& p11 = Chunk::get<PixelPiece>(pixel_block.chunks()[6]);
  EXPECT_EQ(p11.type(), ChunkType::PIXEL_PIECE);
  EXPECT_EQ(p11.repeat().type, ChunkRepeatType::FINITE);
  EXPECT_EQ(p11.repeat().times, 1u);
  EXPECT_EQ(p11.num_bits(), 2u);
  EXPECT_EQ(p11.pixel_index(), 1u);

  const PixelPiece& p01 = Chunk::get<PixelPiece>(pixel_block.chunks()[7]);
  EXPECT_EQ(p01.type(), ChunkType::PIXEL_PIECE);
  EXPECT_EQ(p01.repeat().type, ChunkRepeatType::FINITE);
  EXPECT_EQ(p01.repeat().times, 1u);
  EXPECT_EQ(p01.num_bits(), 2u);
  EXPECT_EQ(p01.pixel_index(), 0u);

  const Padding& padding_block = Chunk::get<Padding>(packing_block.chunks()[1]);
  EXPECT_EQ(padding_block.type(), ChunkType::PADDING);
  EXPECT_EQ(padding_block.repeat().type, ChunkRepeatType::FINITE);
  EXPECT_EQ(padding_block.repeat().times, 1u);
  EXPECT_EQ(padding_block.num_bits(), 48u);
}

TEST(Raw10Test, CheckNoPaddingIfStrideAndWidthAlign) {
  RawFormatInstance formatRaw10_8x8_10 =
      CreateFormatInstance(kRaw10FormatBGGR, kImageBGGRWidth, kImageBGGRHeight, 10u);
  EXPECT_EQ(formatRaw10_8x8_10.packing_block.chunks().size(), 1u);
  const PackingBlock& block =
      Chunk::get<PackingBlock>(formatRaw10_8x8_10.packing_block.chunks()[0]);
  EXPECT_EQ(block.repeat().times, 2u);
  EXPECT_EQ(block.num_pixels(), 4u);
  EXPECT_EQ(block.size(), 5u);
}

TEST(Raw10Test, CheckGetPixelWorking) {
  RawFormatInstance formatRaw10_8x8_16 =
      CreateFormatInstance(kRaw10FormatBGGR, kImageBGGRWidth, kImageBGGRHeight, kImageBGGRStride);

  for (uint32_t j = 0; j < kImageBGGRHeight; ++j) {
    for (uint32_t i = 0; i < kImageBGGRWidth; ++i) {
      uint32_t pixel_index = (kImageBGGRWidth * j) + i;
      uint64_t pixel_val = GetPixel(formatRaw10_8x8_16, pixel_index, kImageBGGR, kImageBGGRSize);

      if (j % 2 == 0 && i % 2 == 0) {
        EXPECT_EQ(pixel_val, static_cast<uint64_t>(kBlueBits));
      } else if (j % 2 == 0 && i % 2 == 1) {
        EXPECT_EQ(pixel_val, static_cast<uint64_t>(kGreen0Bits));
      } else if (j % 2 == 1 && i % 2 == 0) {
        EXPECT_EQ(pixel_val, static_cast<uint64_t>(kGreen1Bits));
      } else if (j % 2 == 1 && i % 2 == 1) {
        EXPECT_EQ(pixel_val, static_cast<uint64_t>(kRedBits));
      }
    }
  }
}

TEST(Raw10Test, SetPixelWorking) {
  RawFormatInstance formatRaw10_8x8_16 =
      CreateFormatInstance(kRaw10FormatBGGR, kImageBGGRWidth, kImageBGGRHeight, kImageBGGRStride);

  // Copy the image we modify to not mess with the other tests.
  uint8_t image[kImageBGGRSize];
  memcpy(&image, kImageBGGR, kImageBGGRSize);

  // Set all the blue pixels to the green0 value.
  for (uint32_t j = 0; j < kImageBGGRHeight; ++j) {
    for (uint32_t i = 0; i < kImageBGGRWidth; ++i) {
      uint32_t pixel_index = (kImageBGGRWidth * j) + i;

      if (j % 2 == 0 && i % 2 == 0) {
        SetPixel(formatRaw10_8x8_16, pixel_index, kGreen0Bits, image, kImageBGGRSize);
      }
    }
  }

  // Check that all the pixels have the expected value.
  for (uint32_t j = 0; j < kImageBGGRHeight; ++j) {
    for (uint32_t i = 0; i < kImageBGGRWidth; ++i) {
      uint32_t pixel_index = (kImageBGGRWidth * j) + i;
      uint64_t pixel_val = GetPixel(formatRaw10_8x8_16, pixel_index, image, kImageBGGRSize);

      if (j % 2 == 0 && i % 2 == 0) {
        EXPECT_EQ(pixel_val, static_cast<uint64_t>(kGreen0Bits));
      } else if (j % 2 == 0 && i % 2 == 1) {
        EXPECT_EQ(pixel_val, static_cast<uint64_t>(kGreen0Bits));
      } else if (j % 2 == 1 && i % 2 == 0) {
        EXPECT_EQ(pixel_val, static_cast<uint64_t>(kGreen1Bits));
      } else if (j % 2 == 1 && i % 2 == 1) {
        EXPECT_EQ(pixel_val, static_cast<uint64_t>(kRedBits));
      }
    }
  }

  // Set all the green0 pixels to the red value.
  for (uint32_t j = 0; j < kImageBGGRHeight; ++j) {
    for (uint32_t i = 0; i < kImageBGGRWidth; ++i) {
      uint32_t pixel_index = (kImageBGGRWidth * j) + i;

      if (j % 2 == 0 && i % 2 == 1) {
        SetPixel(formatRaw10_8x8_16, pixel_index, kRedBits, image, kImageBGGRSize);
      }
    }
  }

  // Check that all the pixels have the expected value.
  for (uint32_t j = 0; j < kImageBGGRHeight; ++j) {
    for (uint32_t i = 0; i < kImageBGGRWidth; ++i) {
      uint32_t pixel_index = (kImageBGGRWidth * j) + i;
      uint64_t pixel_val = GetPixel(formatRaw10_8x8_16, pixel_index, image, kImageBGGRSize);

      if (j % 2 == 0 && i % 2 == 0) {
        EXPECT_EQ(pixel_val, static_cast<uint64_t>(kGreen0Bits));
      } else if (j % 2 == 0 && i % 2 == 1) {
        EXPECT_EQ(pixel_val, static_cast<uint64_t>(kRedBits));
      } else if (j % 2 == 1 && i % 2 == 0) {
        EXPECT_EQ(pixel_val, static_cast<uint64_t>(kGreen1Bits));
      } else if (j % 2 == 1 && i % 2 == 1) {
        EXPECT_EQ(pixel_val, static_cast<uint64_t>(kRedBits));
      }
    }
  }

  // Set all the green1 pixels to the blue value.
  for (uint32_t j = 0; j < kImageBGGRHeight; ++j) {
    for (uint32_t i = 0; i < kImageBGGRWidth; ++i) {
      uint32_t pixel_index = (kImageBGGRWidth * j) + i;

      if (j % 2 == 1 && i % 2 == 0) {
        SetPixel(formatRaw10_8x8_16, pixel_index, kBlueBits, image, kImageBGGRSize);
      }
    }
  }

  // Check that all the pixels have the expected value.
  for (uint32_t j = 0; j < kImageBGGRHeight; ++j) {
    for (uint32_t i = 0; i < kImageBGGRWidth; ++i) {
      uint32_t pixel_index = (kImageBGGRWidth * j) + i;
      uint64_t pixel_val = GetPixel(formatRaw10_8x8_16, pixel_index, image, kImageBGGRSize);

      if (j % 2 == 0 && i % 2 == 0) {
        EXPECT_EQ(pixel_val, static_cast<uint64_t>(kGreen0Bits));
      } else if (j % 2 == 0 && i % 2 == 1) {
        EXPECT_EQ(pixel_val, static_cast<uint64_t>(kRedBits));
      } else if (j % 2 == 1 && i % 2 == 0) {
        EXPECT_EQ(pixel_val, static_cast<uint64_t>(kBlueBits));
      } else if (j % 2 == 1 && i % 2 == 1) {
        EXPECT_EQ(pixel_val, static_cast<uint64_t>(kRedBits));
      }
    }
  }

  // Set all the red pixels to the green1 value.
  for (uint32_t j = 0; j < kImageBGGRHeight; ++j) {
    for (uint32_t i = 0; i < kImageBGGRWidth; ++i) {
      uint32_t pixel_index = (kImageBGGRWidth * j) + i;

      if (j % 2 == 1 && i % 2 == 1) {
        SetPixel(formatRaw10_8x8_16, pixel_index, kGreen1Bits, image, kImageBGGRSize);
      }
    }
  }

  // Check that all the pixels have the expected value.
  for (uint32_t j = 0; j < kImageBGGRHeight; ++j) {
    for (uint32_t i = 0; i < kImageBGGRWidth; ++i) {
      uint32_t pixel_index = (kImageBGGRWidth * j) + i;
      uint64_t pixel_val = GetPixel(formatRaw10_8x8_16, pixel_index, image, kImageBGGRSize);

      if (j % 2 == 0 && i % 2 == 0) {
        EXPECT_EQ(pixel_val, static_cast<uint64_t>(kGreen0Bits));
      } else if (j % 2 == 0 && i % 2 == 1) {
        EXPECT_EQ(pixel_val, static_cast<uint64_t>(kRedBits));
      } else if (j % 2 == 1 && i % 2 == 0) {
        EXPECT_EQ(pixel_val, static_cast<uint64_t>(kBlueBits));
      } else if (j % 2 == 1 && i % 2 == 1) {
        EXPECT_EQ(pixel_val, static_cast<uint64_t>(kGreen1Bits));
      }
    }
  }
}

}  // namespace
}  // namespace camera::raw

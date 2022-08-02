// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Define this symbol before including raw.h so that the introspection functions in PointerList we
// need are compiled in.
#define RAW_FORMATS_POINTER_LIST_TEST

#include "src/camera/lib/raw_formats/raw.h"

#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/assert.h>

#include <cstdint>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/camera/lib/raw_formats/raw_lookups.h"

/*
  This is mostly testing failure cases. The tests for the specific formats (eg. RAW10) are pretty
  complex and serve as a decent exercise for all the happy paths.
*/

namespace camera::raw {
namespace {

// Helper macros for making "assertions" in constexpr unit tests.
#define CEXPECT(statement) \
  if (!(statement))        \
  return false
#define CEXPECT_EQ(a, b) \
  if ((a) != (b))        \
  return false
#define CEXPECT_NE(a, b) \
  if ((a) == (b))        \
  return false

/* The following unit tests are constexpr and forced to run at compile time with static_asserts. The
   advantages to testing this way are twofold. The first is that it ensures all the things we want
   to run at compile time actually can be run at compile time. The second is that the compiler will
   detect memory leaks, invalid memory accesses, and undefined behavior when running constexpr code
   during compilation and halt with a compilation error pinpointing the issue. This allows a much
   higher degree of confidence when running this code at runtime, despite the fact that it uses new
   and delete directly.
*/
constexpr bool DynamicallyCreatePointerListOfChunks() {
  {
    PointerList<Chunk> chunks(8);
    CEXPECT_EQ(chunks.size(), 0);
    CEXPECT_EQ(chunks.capacity(), 8);
    CEXPECT(chunks.owns_memory());
    CEXPECT(!chunks.static_list());
    CEXPECT(chunks.dynamic_list());

    chunks.emplace_back<PixelPiece>(0, static_cast<uint8_t>(0b11111111), static_cast<int8_t>(2));
    chunks.emplace_back<PixelPiece>(1, static_cast<uint8_t>(0b11111111), static_cast<int8_t>(2));
    chunks.emplace_back<PixelPiece>(2, static_cast<uint8_t>(0b11111111), static_cast<int8_t>(2));
    chunks.emplace_back<PixelPiece>(3, static_cast<uint8_t>(0b11111111), static_cast<int8_t>(2));
    chunks.emplace_back<PixelPiece>(3, static_cast<uint8_t>(0b11000000), static_cast<int8_t>(-6));
    chunks.emplace_back<PixelPiece>(2, static_cast<uint8_t>(0b00110000), static_cast<int8_t>(-4));
    chunks.emplace_back<PixelPiece>(1, static_cast<uint8_t>(0b00001100), static_cast<int8_t>(-2));
    chunks.emplace_back<PixelPiece>(0, static_cast<uint8_t>(0b00000011), static_cast<int8_t>(0));
    // This should fail due to being at capacity.
    CEXPECT(!chunks.emplace_back<PixelPiece>(0, static_cast<uint8_t>(0b00000011),
                                             static_cast<int8_t>(0)));

    CEXPECT_EQ(chunks.size(), 8);
    CEXPECT_EQ(chunks.capacity(), 8);
    CEXPECT(chunks.owns_memory());
    CEXPECT(!chunks.static_list());
    CEXPECT(chunks.dynamic_list());

    uint32_t num_pixels = SumPixels(chunks);
    CEXPECT_EQ(num_pixels, 4);
    uint64_t num_bits = SumBits(chunks);
    CEXPECT_EQ(num_bits, (5 * 8));
  }

  return true;
}

constexpr bool DynamicallyCreateAndCopyPointerListOfChunks() {
  {
    PointerList<Chunk> chunks(8);

    chunks.emplace_back<PixelPiece>(0, static_cast<uint8_t>(0b11111111), static_cast<int8_t>(2));
    chunks.emplace_back<PixelPiece>(1, static_cast<uint8_t>(0b11111111), static_cast<int8_t>(2));
    chunks.emplace_back<PixelPiece>(2, static_cast<uint8_t>(0b11111111), static_cast<int8_t>(2));
    chunks.emplace_back<PixelPiece>(3, static_cast<uint8_t>(0b11111111), static_cast<int8_t>(2));
    chunks.emplace_back<PixelPiece>(3, static_cast<uint8_t>(0b11000000), static_cast<int8_t>(-6));
    chunks.emplace_back<PixelPiece>(2, static_cast<uint8_t>(0b00110000), static_cast<int8_t>(-4));
    chunks.emplace_back<PixelPiece>(1, static_cast<uint8_t>(0b00001100), static_cast<int8_t>(-2));
    chunks.emplace_back<PixelPiece>(0, static_cast<uint8_t>(0b00000011), static_cast<int8_t>(0));

    PointerList<Chunk> chunks2(chunks);

    // Make sure chunks2 has (mostly) the same contents.
    CEXPECT_EQ(chunks2.size(), 8);
    CEXPECT_EQ(chunks2.capacity(), 8);
    CEXPECT(chunks2.owns_memory());
    CEXPECT(!chunks2.static_list());
    CEXPECT(chunks2.dynamic_list());

    uint32_t num_pixels = SumPixels(chunks2);
    CEXPECT_EQ(num_pixels, 4);
    uint64_t num_bits = SumBits(chunks2);
    CEXPECT_EQ(num_bits, (5 * 8));

    // The dynamic list address should be different.
    CEXPECT_NE(chunks.dynamic_list(), chunks2.dynamic_list());
  }

  return true;
}

constexpr bool DynamicallyCreateAndMovePointerListOfChunks() {
  {
    PointerList<Chunk> chunks(8);
    auto chunks_dynamic_list_addr = chunks.dynamic_list();

    chunks.emplace_back<PixelPiece>(0, static_cast<uint8_t>(0b11111111), static_cast<int8_t>(2));
    chunks.emplace_back<PixelPiece>(1, static_cast<uint8_t>(0b11111111), static_cast<int8_t>(2));
    chunks.emplace_back<PixelPiece>(2, static_cast<uint8_t>(0b11111111), static_cast<int8_t>(2));
    chunks.emplace_back<PixelPiece>(3, static_cast<uint8_t>(0b11111111), static_cast<int8_t>(2));
    chunks.emplace_back<PixelPiece>(3, static_cast<uint8_t>(0b11000000), static_cast<int8_t>(-6));
    chunks.emplace_back<PixelPiece>(2, static_cast<uint8_t>(0b00110000), static_cast<int8_t>(-4));
    chunks.emplace_back<PixelPiece>(1, static_cast<uint8_t>(0b00001100), static_cast<int8_t>(-2));
    chunks.emplace_back<PixelPiece>(0, static_cast<uint8_t>(0b00000011), static_cast<int8_t>(0));

    PointerList<Chunk> chunks2(std::move(chunks));

    // Make sure chunks has been wiped out.
    CEXPECT_EQ(chunks.size(), 0);
    CEXPECT_EQ(chunks.capacity(), 0);
    CEXPECT(!chunks.owns_memory());
    CEXPECT(!chunks.static_list());
    CEXPECT(!chunks.dynamic_list());

    uint32_t num_pixels = SumPixels(chunks);
    CEXPECT_EQ(num_pixels, 0);
    uint64_t num_bits = SumBits(chunks);
    CEXPECT_EQ(num_bits, 0);

    // Make sure chunks2 has the same contents.
    CEXPECT_EQ(chunks2.size(), 8);
    CEXPECT_EQ(chunks2.capacity(), 8);
    CEXPECT(chunks2.owns_memory());
    CEXPECT(!chunks2.static_list());
    CEXPECT(chunks2.dynamic_list());

    num_pixels = SumPixels(chunks2);
    CEXPECT_EQ(num_pixels, 4);
    num_bits = SumBits(chunks2);
    CEXPECT_EQ(num_bits, (5 * 8));

    // chunks2 should have the same list address as chunks used to have.
    CEXPECT_EQ(chunks2.dynamic_list(), chunks_dynamic_list_addr);
  }

  return true;
}

constexpr bool DynamicallyCreatePointerListOfPixelColorArrays() {
  PointerList<PixelColor> color_array(/*capacity*/ 2, /*element_array_size*/ 2);
  CEXPECT_EQ(color_array.capacity(), 2);
  CEXPECT_EQ(color_array.size(), 0);

  CEXPECT(color_array.push_back(new PixelColor[2]{PixelColor::RED, PixelColor::GREENr}));
  CEXPECT(color_array.push_back(new PixelColor[2]{PixelColor::GREENb, PixelColor::BLUE}));
  CEXPECT_EQ(color_array.size(), 2);
  CEXPECT_EQ(color_array[0][0], PixelColor::RED);
  CEXPECT_EQ(color_array[0][1], PixelColor::GREENr);
  CEXPECT_EQ(color_array[1][0], PixelColor::GREENb);
  CEXPECT_EQ(color_array[1][1], PixelColor::BLUE);

  // This should fail because the list is at capacity.
  PixelColor* temp = new PixelColor[2]{PixelColor::GREENb, PixelColor::BLUE};
  CEXPECT(!color_array.push_back(temp));
  delete[] temp;

  return true;
}

constexpr bool DynamicallyCreateAndCopyPointerListOfPixelColorArrays() {
  PointerList<PixelColor> color_array(/*capacity*/ 2, /*element_array_size*/ 2);
  CEXPECT(color_array.push_back(new PixelColor[2]{PixelColor::RED, PixelColor::GREENr}));
  CEXPECT(color_array.push_back(new PixelColor[2]{PixelColor::GREENb, PixelColor::BLUE}));

  PointerList<PixelColor> color_array2(color_array);

  CEXPECT_EQ(color_array2.capacity(), 2);
  CEXPECT_EQ(color_array2.size(), 2);
  CEXPECT_EQ(color_array2.element_array_size(), 2);
  CEXPECT_EQ(color_array2[0][0], PixelColor::RED);
  CEXPECT_EQ(color_array2[0][1], PixelColor::GREENr);
  CEXPECT_EQ(color_array2[1][0], PixelColor::GREENb);
  CEXPECT_EQ(color_array2[1][1], PixelColor::BLUE);

  return true;
}

constexpr bool DynamicallyCreateAndMovePointerListOfPixelColorArrays() {
  PointerList<PixelColor> color_array(/*capacity*/ 2, /*element_array_size*/ 2);
  CEXPECT(color_array.push_back(new PixelColor[2]{PixelColor::RED, PixelColor::GREENr}));
  CEXPECT(color_array.push_back(new PixelColor[2]{PixelColor::GREENb, PixelColor::BLUE}));

  PointerList<PixelColor> color_array2(std::move(color_array));

  // The original should be cleared out.
  CEXPECT_EQ(color_array.size(), 0);
  CEXPECT_EQ(color_array.capacity(), 0);
  CEXPECT(!color_array.owns_memory());
  CEXPECT(!color_array.static_list());
  CEXPECT(!color_array.dynamic_list());

  CEXPECT_EQ(color_array2.capacity(), 2);
  CEXPECT_EQ(color_array2.size(), 2);
  CEXPECT_EQ(color_array2.element_array_size(), 2);
  CEXPECT_EQ(color_array2[0][0], PixelColor::RED);
  CEXPECT_EQ(color_array2[0][1], PixelColor::GREENr);
  CEXPECT_EQ(color_array2[1][0], PixelColor::GREENb);
  CEXPECT_EQ(color_array2[1][1], PixelColor::BLUE);

  return true;
}

// Force the constexpr tests to run at compile time and check the results.
TEST(RawTest, ExecuteConstexprTests) {
  static_assert(DynamicallyCreatePointerListOfChunks());
  static_assert(DynamicallyCreateAndCopyPointerListOfChunks());
  static_assert(DynamicallyCreateAndMovePointerListOfChunks());

  static_assert(DynamicallyCreatePointerListOfPixelColorArrays());
  static_assert(DynamicallyCreateAndCopyPointerListOfPixelColorArrays());
  static_assert(DynamicallyCreateAndMovePointerListOfPixelColorArrays());
}

TEST(RawTest, CreateInstanceAssertsForNonFillImage) {
  static constexpr PointerList<Chunk> test_chunk_list(nullptr, 0);
  static constexpr PackingBlock test_format_packing(test_chunk_list, ChunkRepeat::fill_width());
  static constexpr RawFormat test_format(test_format_packing, kBayerBGGR, kBayer10DepthMap);

  EXPECT_DEATH(CreateFormatInstance(test_format, 640, 480, std::nullopt),
               "Top level PackingBlock repeat type must be FILL_IMAGE.");
}

TEST(RawTest, CreateInstanceAssertsIfSizeNotWholeBytes) {
  static constexpr PixelPiece piece(0, 0b00000001, 0);
  static constexpr const Chunk* chunks[] = {&piece};
  static constexpr PointerList<Chunk> pixel_chunk_list(chunks, sizeof(chunks) / sizeof(*chunks));
  static constexpr PackingBlock test_format_packing(pixel_chunk_list, ChunkRepeat::fill_image());
  static constexpr RawFormat test_format(test_format_packing, kBayerBGGR, kBayer10DepthMap);

  EXPECT_DEATH(CreateFormatInstance(test_format, 640, 480, std::nullopt),
               "A PackingBlock must contain a whole number of bytes.");
}

TEST(RawTest, CreateInstanceAssertsIfFillWidthBlockDoesNotEvenlyDivideWidth) {
  static constexpr PixelPiece piece0(0, 0b11111111, 0);
  static constexpr PixelPiece piece1(1, 0b11111111, 0);
  static constexpr const Chunk* pixel_chunks[] = {&piece0, &piece1};
  static constexpr PointerList<Chunk> pixel_chunk_list(
      pixel_chunks, sizeof(pixel_chunks) / sizeof(*pixel_chunks));
  static constexpr PackingBlock fill_width_block(pixel_chunk_list, ChunkRepeat::fill_width());
  static constexpr const Chunk* chunks[] = {&fill_width_block};
  static constexpr PointerList<Chunk> chunk_list(chunks, sizeof(chunks) / sizeof(*chunks));
  static constexpr PackingBlock test_format_packing(chunk_list, ChunkRepeat::fill_image());
  static constexpr RawFormat test_format(test_format_packing, kBayerBGGR, kBayer10DepthMap);

  EXPECT_DEATH(CreateFormatInstance(test_format, 3, 1, std::nullopt),
               "A FILL_WIDTH PackingBlock must contain a number of pixels that evenly divides the "
               "width.");
}

TEST(RawTest, CreateInstanceAssertsIfPackingBlockIsFillStride) {
  static constexpr PointerList<Chunk> empty_list(nullptr, 0);
  static constexpr PackingBlock fill_stride_block(empty_list, ChunkRepeat::fill_stride());
  static constexpr const Chunk* chunks[] = {&fill_stride_block};
  static constexpr PointerList<Chunk> chunk_list(chunks, sizeof(chunks) / sizeof(*chunks));
  static constexpr PackingBlock test_format_packing(chunk_list, ChunkRepeat::fill_image());
  static constexpr RawFormat test_format(test_format_packing, kBayerBGGR, kBayer10DepthMap);

  EXPECT_DEATH(CreateFormatInstance(test_format, 3, 1, std::nullopt),
               "PackingBlocks only support FINITE, FILL_WIDTH, and FILL_IMAGE repeat types.");
}

TEST(RawTest, CreateInstanceAssertsIfPaddingInvalid) {
  static constexpr Padding fill_width_padding(0, ChunkRepeat::fill_width());
  static constexpr const Chunk* chunks[] = {&fill_width_padding};
  static constexpr PointerList<Chunk> chunk_list(chunks, sizeof(chunks) / sizeof(*chunks));
  static constexpr PackingBlock test_format_packing(chunk_list, ChunkRepeat::fill_image());
  static constexpr RawFormat test_format(test_format_packing, kBayerBGGR, kBayer10DepthMap);

  EXPECT_DEATH(CreateFormatInstance(test_format, 640, 480, std::nullopt),
               "Padding only supports FINITE and FILL_STRIDE repeat types.");
}

TEST(RawTest, CreateInstanceAssertsIfStrideNeededButNotGiven) {
  static constexpr Padding fill_stride_padding(0, ChunkRepeat::fill_stride());
  static constexpr const Chunk* chunks[] = {&fill_stride_padding};
  static constexpr PointerList<Chunk> chunk_list(chunks, sizeof(chunks) / sizeof(*chunks));
  static constexpr PackingBlock test_format_packing(chunk_list, ChunkRepeat::fill_image());
  static constexpr RawFormat test_format(test_format_packing, kBayerBGGR, kBayer10DepthMap);

  EXPECT_DEATH(CreateFormatInstance(test_format, 640, 480, std::nullopt),
               "Stride needed but not provided.");
}

TEST(RawTest, CreateInstanceAssertsIfNonWholeBytesBeforeStridePadding) {
  static constexpr PixelPiece piece0(0, 0b11111110, 0);
  static constexpr Padding fill_stride_padding(0, ChunkRepeat::fill_stride());
  static constexpr const Chunk* chunks[] = {&piece0, &fill_stride_padding};
  static constexpr PointerList<Chunk> chunk_list(chunks, sizeof(chunks) / sizeof(*chunks));
  static constexpr PackingBlock test_format_packing(chunk_list, ChunkRepeat::fill_image());
  static constexpr RawFormat test_format(test_format_packing, kBayerBGGR, kBayer10DepthMap);

  EXPECT_DEATH(CreateFormatInstance(test_format, 640, 480, 1024),
               "There must be an even number of bytes in a PixelBlock before FILL_STRIDE padding.");
}

TEST(RawTest, CreateInstanceAssertsIfStrideLessThanPixelRowSize) {
  static constexpr PixelPiece piece0(0, 0b11111111, 0);
  static constexpr Padding fill_stride_padding(0, ChunkRepeat::fill_stride());
  static constexpr const Chunk* chunks[] = {&piece0, &fill_stride_padding};
  static constexpr PointerList<Chunk> chunk_list(chunks, sizeof(chunks) / sizeof(*chunks));
  static constexpr PackingBlock test_format_packing(chunk_list, ChunkRepeat::fill_image());
  static constexpr RawFormat test_format(test_format_packing, kBayerBGGR, kBayer10DepthMap);

  EXPECT_DEATH(
      CreateFormatInstance(test_format, 1, 1, 0),
      "Row stride must be greater than or equal to the number of bytes in 'width' pixels.");
}

TEST(RawTest, GetPixelAssertsFromNullBuffer) {
  static constexpr PixelPiece piece0(0, 0b11111111, 0);
  static constexpr const Chunk* pixels[] = {&piece0};
  static constexpr PointerList<Chunk> pixel_list(pixels, sizeof(pixels) / sizeof(*pixels));
  static constexpr PackingBlock pixel_block(pixel_list, ChunkRepeat::fill_width());
  static constexpr Padding fill_stride_padding(0, ChunkRepeat::fill_stride());
  static constexpr const Chunk* chunks[] = {&pixel_block, &fill_stride_padding};
  static constexpr PointerList<Chunk> chunk_list(chunks, sizeof(chunks) / sizeof(*chunks));
  static constexpr PackingBlock test_format_packing(chunk_list, ChunkRepeat::fill_image());
  static constexpr RawFormat test_format(test_format_packing, kBayerBGGR, kBayer10DepthMap);
  RawFormatInstance instance = CreateFormatInstance(test_format, 640, 480, 640);

  EXPECT_DEATH(GetPixel(instance, 0, nullptr, 640 * 480), "GetPixel given null buffer.");
}

TEST(RawTest, GetPixelAssertsFromIndexOOB) {
  static constexpr PixelPiece piece0(0, 0b11111111, 0);
  static constexpr const Chunk* pixels[] = {&piece0};
  static constexpr PointerList<Chunk> pixel_list(pixels, sizeof(pixels) / sizeof(*pixels));
  static constexpr PackingBlock pixel_block(pixel_list, ChunkRepeat::fill_width());
  static constexpr Padding fill_stride_padding(0, ChunkRepeat::fill_stride());
  static constexpr const Chunk* chunks[] = {&pixel_block, &fill_stride_padding};
  static constexpr PointerList<Chunk> chunk_list(chunks, sizeof(chunks) / sizeof(*chunks));
  static constexpr PackingBlock test_format_packing(chunk_list, ChunkRepeat::fill_image());
  static constexpr RawFormat test_format(test_format_packing, kBayerBGGR, kBayer10DepthMap);
  RawFormatInstance instance = CreateFormatInstance(test_format, 640, 480, 640);
  // Make something that looks kind of like a valid pointer.
  uint8_t* fake_buffer = reinterpret_cast<uint8_t*>(0xDEADBEEF);

  EXPECT_DEATH(GetPixel(instance, 640 * 480, fake_buffer, 640 * 480), "pixel_index out of bounds.");
}

TEST(RawTest, GetPixelAssertsIfBufferOverrunDetected) {
  static constexpr PixelPiece piece0(0, 0b11111111, 0);
  static constexpr const Chunk* pixels[] = {&piece0};
  static constexpr PointerList<Chunk> pixel_list(pixels, sizeof(pixels) / sizeof(*pixels));
  static constexpr PackingBlock pixel_block(pixel_list, ChunkRepeat::fill_width());
  static constexpr Padding fill_stride_padding(0, ChunkRepeat::fill_stride());
  static constexpr const Chunk* chunks[] = {&pixel_block, &fill_stride_padding};
  static constexpr PointerList<Chunk> chunk_list(chunks, sizeof(chunks) / sizeof(*chunks));
  static constexpr PackingBlock test_format_packing(chunk_list, ChunkRepeat::fill_image());
  static constexpr RawFormat test_format(test_format_packing, kBayerBGGR, kBayer10DepthMap);
  RawFormatInstance instance = CreateFormatInstance(test_format, 640, 480, 640);
  // Make something that looks kind of like a valid pointer.
  uint8_t* fake_buffer = reinterpret_cast<uint8_t*>(0xDEADBEEF);

  EXPECT_DEATH(GetPixel(instance, 641, fake_buffer, 640),
               "Pixel offset calculated to be greater than buffer size.");
}

TEST(RawTest, SetPixelAssertsFromNullBuffer) {
  static constexpr PixelPiece piece0(0, 0b11111111, 0);
  static constexpr const Chunk* pixels[] = {&piece0};
  static constexpr PointerList<Chunk> pixel_list(pixels, sizeof(pixels) / sizeof(*pixels));
  static constexpr PackingBlock pixel_block(pixel_list, ChunkRepeat::fill_width());
  static constexpr Padding fill_stride_padding(0, ChunkRepeat::fill_stride());
  static constexpr const Chunk* chunks[] = {&pixel_block, &fill_stride_padding};
  static constexpr PointerList<Chunk> chunk_list(chunks, sizeof(chunks) / sizeof(*chunks));
  static constexpr PackingBlock test_format_packing(chunk_list, ChunkRepeat::fill_image());
  static constexpr RawFormat test_format(test_format_packing, kBayerBGGR, kBayer10DepthMap);
  RawFormatInstance instance = CreateFormatInstance(test_format, 640, 480, 640);

  EXPECT_DEATH(SetPixel(instance, 0, 0, nullptr, 640 * 480), "SetPixel given null buffer.");
}

TEST(RawTest, SetPixelAssertsFromIndexOOB) {
  static constexpr PixelPiece piece0(0, 0b11111111, 0);
  static constexpr const Chunk* pixels[] = {&piece0};
  static constexpr PointerList<Chunk> pixel_list(pixels, sizeof(pixels) / sizeof(*pixels));
  static constexpr PackingBlock pixel_block(pixel_list, ChunkRepeat::fill_width());
  static constexpr Padding fill_stride_padding(0, ChunkRepeat::fill_stride());
  static constexpr const Chunk* chunks[] = {&pixel_block, &fill_stride_padding};
  static constexpr PointerList<Chunk> chunk_list(chunks, sizeof(chunks) / sizeof(*chunks));
  static constexpr PackingBlock test_format_packing(chunk_list, ChunkRepeat::fill_image());
  static constexpr RawFormat test_format(test_format_packing, kBayerBGGR, kBayer10DepthMap);
  RawFormatInstance instance = CreateFormatInstance(test_format, 640, 480, 640);
  // Make something that looks kind of like a valid pointer.
  uint8_t* fake_buffer = reinterpret_cast<uint8_t*>(0xDEADBEEF);

  EXPECT_DEATH(SetPixel(instance, 640 * 480, 0, fake_buffer, 640 * 480),
               "pixel_index out of bounds.");
}

TEST(RawTest, SetPixelAssertsIfBufferOverrunDetected) {
  static constexpr PixelPiece piece0(0, 0b11111111, 0);
  static constexpr const Chunk* pixels[] = {&piece0};
  static constexpr PointerList<Chunk> pixel_list(pixels, sizeof(pixels) / sizeof(*pixels));
  static constexpr PackingBlock pixel_block(pixel_list, ChunkRepeat::fill_width());
  static constexpr Padding fill_stride_padding(0, ChunkRepeat::fill_stride());
  static constexpr const Chunk* chunks[] = {&pixel_block, &fill_stride_padding};
  static constexpr PointerList<Chunk> chunk_list(chunks, sizeof(chunks) / sizeof(*chunks));
  static constexpr PackingBlock test_format_packing(chunk_list, ChunkRepeat::fill_image());
  static constexpr RawFormat test_format(test_format_packing, kBayerBGGR, kBayer10DepthMap);
  RawFormatInstance instance = CreateFormatInstance(test_format, 640, 480, 640);
  // Make something that looks kind of like a valid pointer.
  uint8_t* fake_buffer = reinterpret_cast<uint8_t*>(0xDEADBEEF);

  EXPECT_DEATH(SetPixel(instance, 641, 0, fake_buffer, 640),
               "Pixel offset calculated to be greater than buffer size.");
}

}  // namespace
}  // namespace camera::raw

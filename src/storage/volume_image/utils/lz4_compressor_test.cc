// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/utils/lz4_compressor.h"

#include <lib/fit/defer.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <lz4/lz4.h>

#include "src/storage/volume_image/options.h"

namespace storage::volume_image {
namespace {

TEST(Lz4CompressorTest, CreateWithWrongSchemaIsError) {
  CompressionOptions options;
  options.schema = CompressionSchema::kNone;

  auto compressor_result = Lz4Compressor::Create(options);
  ASSERT_TRUE(compressor_result.is_error());
}

TEST(Lz4CompressorTest, CreateWithLz4SchemaOnlyIsOk) {
  CompressionOptions options;
  options.schema = CompressionSchema::kLz4;
  options.options.clear();

  auto compressor_result = Lz4Compressor::Create(options);
  ASSERT_TRUE(compressor_result.is_ok()) << compressor_result.error();

  const auto& preferences = compressor_result.value().GetPreferences();
  EXPECT_EQ(preferences.frameInfo.blockSizeID, LZ4F_max64KB);
  EXPECT_EQ(preferences.compressionLevel, 0);
  EXPECT_EQ(preferences.frameInfo.blockMode, LZ4F_blockIndependent);
}

TEST(Lz4CompressorTest, CreateWithLz4SchemaAndCompressionLevelIsOk) {
  constexpr int kCompressionLevel = 12345;
  CompressionOptions options;
  options.schema = CompressionSchema::kLz4;
  options.options.clear();
  options.options["compression_level"] = kCompressionLevel;

  auto compressor_result = Lz4Compressor::Create(options);
  ASSERT_TRUE(compressor_result.is_ok()) << compressor_result.error();

  const auto& preferences = compressor_result.value().GetPreferences();
  EXPECT_EQ(preferences.frameInfo.blockSizeID, LZ4F_max64KB);
  EXPECT_EQ(preferences.compressionLevel, kCompressionLevel);
  EXPECT_EQ(preferences.frameInfo.blockMode, LZ4F_blockIndependent);
}

struct BlockSizeParam {
  uint64_t block_size = 0;
  LZ4F_blockSizeID_t expected_id = LZ4F_max64KB;
};

class BlockSizeLz4CompressorTest : public testing::TestWithParam<BlockSizeParam> {};

TEST_P(BlockSizeLz4CompressorTest, CreateWithLz4SchemaAndBlockSizeMapsCorrectlyToBlockSizeId) {
  CompressionOptions options;
  const int random_compression_level = testing::UnitTest::GetInstance()->random_seed();

  options.schema = CompressionSchema::kLz4;
  options.options.clear();
  options.options["block_size"] = GetParam().block_size;
  options.options["compression_level"] = random_compression_level;

  auto compressor_result = Lz4Compressor::Create(options);
  ASSERT_TRUE(compressor_result.is_ok()) << compressor_result.error();

  const auto& preferences = compressor_result.value().GetPreferences();
  EXPECT_EQ(preferences.frameInfo.blockSizeID, GetParam().expected_id);
  EXPECT_EQ(preferences.compressionLevel, random_compression_level);
  EXPECT_EQ(preferences.frameInfo.blockMode, LZ4F_blockIndependent);
}

INSTANTIATE_TEST_SUITE_P(
    , BlockSizeLz4CompressorTest,
    testing::Values(BlockSizeParam{.block_size = 0, .expected_id = LZ4F_max64KB},
                    BlockSizeParam{.block_size = 64, .expected_id = LZ4F_max64KB},
                    BlockSizeParam{.block_size = 65, .expected_id = LZ4F_max256KB},
                    BlockSizeParam{.block_size = 256, .expected_id = LZ4F_max256KB},
                    BlockSizeParam{.block_size = 257, .expected_id = LZ4F_max1MB},
                    BlockSizeParam{.block_size = 1024, .expected_id = LZ4F_max1MB},
                    BlockSizeParam{.block_size = 4095, .expected_id = LZ4F_max4MB},
                    BlockSizeParam{.block_size = 4096, .expected_id = LZ4F_max4MB},
                    BlockSizeParam{.block_size = 999999999, .expected_id = LZ4F_max4MB}));

fpromise::result<void, std::string> HandlerReturnsOk(cpp20::span<const uint8_t> /*unused*/) {
  return fpromise::ok();
}

TEST(Lz4CompressorTest, PrepareAfterConstructionIsOk) {
  CompressionOptions options;
  options.schema = CompressionSchema::kLz4;
  options.options.clear();

  auto compressor_result = Lz4Compressor::Create(options);
  ASSERT_TRUE(compressor_result.is_ok()) << compressor_result.error();
  Lz4Compressor compressor = compressor_result.take_value();

  size_t compressed_data_size = 0;
  auto prepare_result = compressor.Prepare([&](auto compressed_data) {
    compressed_data_size = compressed_data.size();
    return fpromise::ok();
  });
  ASSERT_TRUE(prepare_result.is_ok()) << prepare_result.error();
  // Check header size is in valid range.
  EXPECT_GE(compressed_data_size, static_cast<unsigned int>(LZ4F_HEADER_SIZE_MIN));
  EXPECT_LE(compressed_data_size, static_cast<unsigned int>(LZ4F_HEADER_SIZE_MAX));
}

TEST(Lz4CompressorTest, PrepareWithInvalidHandlerIsError) {
  CompressionOptions options;
  options.schema = CompressionSchema::kLz4;
  options.options.clear();

  auto compressor_result = Lz4Compressor::Create(options);
  ASSERT_TRUE(compressor_result.is_ok()) << compressor_result.error();
  Lz4Compressor compressor = compressor_result.take_value();

  EXPECT_TRUE(compressor.Prepare(nullptr).is_error());
}

TEST(Lz4CompressorTest, PrepareWhenAlreadyCalledIsError) {
  CompressionOptions options;
  options.schema = CompressionSchema::kLz4;
  options.options.clear();

  auto compressor_result = Lz4Compressor::Create(options);
  ASSERT_TRUE(compressor_result.is_ok()) << compressor_result.error();
  Lz4Compressor compressor = compressor_result.take_value();

  auto prepare_result = compressor.Prepare(&HandlerReturnsOk);
  ASSERT_TRUE(prepare_result.is_ok()) << prepare_result.error();
  EXPECT_TRUE(compressor.Prepare(&HandlerReturnsOk).is_error());
}

constexpr std::string_view kHandlerError = "This is a handler error";

TEST(Lz4CompressorTest, PrepareForwardsHandlerError) {
  CompressionOptions options;
  options.schema = CompressionSchema::kLz4;
  options.options.clear();

  auto compressor_result = Lz4Compressor::Create(options);
  ASSERT_TRUE(compressor_result.is_ok()) << compressor_result.error();
  Lz4Compressor compressor = compressor_result.take_value();

  auto prepare_result =
      compressor.Prepare([](auto /*unused*/) { return fpromise::error(kHandlerError.data()); });
  ASSERT_TRUE(prepare_result.is_error()) << prepare_result.error();
  EXPECT_EQ(prepare_result.error(), kHandlerError);
}

constexpr std::string_view kData = "123456789123456789";

TEST(Lz4CompressorTest, PreparedAfterCallingCompressIsError) {
  CompressionOptions options;
  options.schema = CompressionSchema::kLz4;
  options.options.clear();

  auto compressor_result = Lz4Compressor::Create(options);
  ASSERT_TRUE(compressor_result.is_ok()) << compressor_result.error();
  Lz4Compressor compressor = compressor_result.take_value();

  auto prepare_result = compressor.Prepare(&HandlerReturnsOk);
  ASSERT_TRUE(prepare_result.is_ok()) << prepare_result.error();
  auto compress_result = compressor.Compress(
      cpp20::span(reinterpret_cast<const uint8_t*>(kData.data()), kData.size()));
  EXPECT_TRUE(compress_result.is_ok());
  EXPECT_TRUE(compressor.Prepare(&HandlerReturnsOk).is_error());
}

TEST(Lz4CompressorTest, PreparedAfterCallingFinalizeIsOk) {
  CompressionOptions options;
  options.schema = CompressionSchema::kLz4;
  options.options.clear();

  auto compressor_result = Lz4Compressor::Create(options);
  ASSERT_TRUE(compressor_result.is_ok()) << compressor_result.error();
  Lz4Compressor compressor = compressor_result.take_value();

  auto prepare_result = compressor.Prepare(&HandlerReturnsOk);
  ASSERT_TRUE(prepare_result.is_ok()) << prepare_result.error();
  auto compress_result = compressor.Compress(
      cpp20::span(reinterpret_cast<const uint8_t*>(kData.data()), kData.size()));
  EXPECT_TRUE(compress_result.is_ok()) << compress_result.error();
  EXPECT_TRUE(compressor.Finalize().is_ok());

  prepare_result = compressor.Prepare(&HandlerReturnsOk);
  ASSERT_TRUE(prepare_result.is_ok()) << prepare_result.error();
}

TEST(Lz4CompressorTest, CompressWithCallingPrepareIsOk) {
  CompressionOptions options;
  options.schema = CompressionSchema::kLz4;
  options.options.clear();

  auto compressor_result = Lz4Compressor::Create(options);
  ASSERT_TRUE(compressor_result.is_ok()) << compressor_result.error();
  Lz4Compressor compressor = compressor_result.take_value();

  auto prepare_result = compressor.Prepare(&HandlerReturnsOk);
  ASSERT_TRUE(prepare_result.is_ok()) << prepare_result.error();
  auto compress_result = compressor.Compress(
      cpp20::span(reinterpret_cast<const uint8_t*>(kData.data()), kData.size()));
  ASSERT_TRUE(compress_result.is_ok()) << compress_result.error();
}

TEST(Lz4CompressorTest, CompressForwardsHandlerError) {
  CompressionOptions options;
  options.schema = CompressionSchema::kLz4;
  options.options.clear();

  auto compressor_result = Lz4Compressor::Create(options);
  ASSERT_TRUE(compressor_result.is_ok()) << compressor_result.error();
  Lz4Compressor compressor = compressor_result.take_value();

  bool should_fail = false;
  auto prepare_result =
      compressor.Prepare([&should_fail](auto /*unused*/) -> fpromise::result<void, std::string> {
        if (should_fail) {
          return fpromise::error(kHandlerError.data());
        }
        return fpromise::ok();
      });
  ASSERT_TRUE(prepare_result.is_ok()) << prepare_result.error();
  should_fail = true;

  auto compress_result = compressor.Compress(
      cpp20::span(reinterpret_cast<const uint8_t*>(kData.data()), kData.size()));
  ASSERT_TRUE(compress_result.is_error());
  EXPECT_EQ(compress_result.error(), kHandlerError);
}

TEST(Lz4CompressorTest, CompressWithoutCallingPrepareIsError) {
  CompressionOptions options;
  options.schema = CompressionSchema::kLz4;
  options.options.clear();

  auto compressor_result = Lz4Compressor::Create(options);
  ASSERT_TRUE(compressor_result.is_ok()) << compressor_result.error();
  Lz4Compressor compressor = compressor_result.take_value();

  auto compress_result = compressor.Compress(
      cpp20::span(reinterpret_cast<const uint8_t*>(kData.data()), kData.size()));
  ASSERT_TRUE(compress_result.is_error());
}

TEST(Lz4CompressorTest, FinalizeWithoutCallingCompressIsError) {
  CompressionOptions options;
  options.schema = CompressionSchema::kLz4;
  options.options.clear();

  auto compressor_result = Lz4Compressor::Create(options);
  ASSERT_TRUE(compressor_result.is_ok()) << compressor_result.error();
  Lz4Compressor compressor = compressor_result.take_value();

  auto prepare_result = compressor.Prepare(&HandlerReturnsOk);
  ASSERT_TRUE(prepare_result.is_ok()) << prepare_result.error();
  auto finalize_result = compressor.Finalize();
  EXPECT_TRUE(finalize_result.is_error());
}

TEST(Lz4CompressorTest, FinalizeForwardsHandlerError) {
  CompressionOptions options;
  options.schema = CompressionSchema::kLz4;
  options.options.clear();

  auto compressor_result = Lz4Compressor::Create(options);
  ASSERT_TRUE(compressor_result.is_ok()) << compressor_result.error();
  Lz4Compressor compressor = compressor_result.take_value();

  bool should_fail = false;
  auto prepare_result =
      compressor.Prepare([&should_fail](auto /*unused*/) -> fpromise::result<void, std::string> {
        if (should_fail) {
          return fpromise::error(kHandlerError.data());
        }
        return fpromise::ok();
      });
  ASSERT_TRUE(prepare_result.is_ok()) << prepare_result.error();
  auto compress_result = compressor.Compress(
      cpp20::span(reinterpret_cast<const uint8_t*>(kData.data()), kData.size()));
  ASSERT_TRUE(compress_result.is_ok());
  should_fail = true;
  auto finalize_result = compressor.Finalize();
  EXPECT_TRUE(finalize_result.is_error());
  EXPECT_EQ(finalize_result.error(), kHandlerError);
}

TEST(Lz4CompressorTest, CompressedDataMatchesUncompressedDataWhenDecompressed) {
  constexpr size_t kUncompressedSize = 4096;
  constexpr size_t kCompressionChunkSize = 512;
  static_assert(kUncompressedSize % kCompressionChunkSize == 0);

  constexpr size_t kCompressionChunkCount = kUncompressedSize / kCompressionChunkSize;

  CompressionOptions options;

  std::vector<uint8_t> uncompressed_data(kUncompressedSize, 0);

  // Fill with pseudo-random data.
  unsigned int seed = testing::UnitTest::GetInstance()->random_seed();
  for (size_t i = 0; i < kUncompressedSize; ++i) {
    uncompressed_data[i] = (rand_r(&seed) + i) % 256;
  }

  options.schema = CompressionSchema::kLz4;
  options.options.clear();

  auto compressor_result = Lz4Compressor::Create(options);
  ASSERT_TRUE(compressor_result.is_ok()) << compressor_result.error();
  Lz4Compressor compressor = compressor_result.take_value();

  size_t max_size = LZ4F_compressBound(kUncompressedSize, &compressor.GetPreferences());
  std::vector<uint8_t> compressed_data(max_size, 0);

  uint64_t offset = 0;
  auto prepare_result =
      compressor.Prepare([&compressed_data, &offset](auto compressed_output_data) {
        memcpy(compressed_data.data() + offset, compressed_output_data.data(),
               compressed_output_data.size());
        offset += compressed_output_data.size();
        return fpromise::ok();
      });

  ASSERT_TRUE(prepare_result.is_ok()) << prepare_result.error();

  // Copy |kCompressionChunkSize| at a time.
  for (size_t i = 0; i < kCompressionChunkCount; ++i) {
    auto compress_result = compressor.Compress(
        cpp20::span(uncompressed_data.data() + (i * kCompressionChunkSize), kCompressionChunkSize));
  }

  auto finalize_result = compressor.Finalize();
  EXPECT_TRUE(finalize_result.is_ok());

  std::vector<uint8_t> decompressed_data(kUncompressedSize, 0);
  LZ4F_decompressionContext_t decompression_context = nullptr;
  auto decompression_context_result =
      LZ4F_createDecompressionContext(&decompression_context, LZ4F_VERSION);
  auto release_decompression_context = fit::defer([&decompression_context]() {
    if (decompression_context != nullptr) {
      LZ4F_freeDecompressionContext(decompression_context);
    }
  });
  ASSERT_FALSE(LZ4F_isError(decompression_context_result));

  size_t decompressed_size = decompressed_data.size();
  size_t consumed_compressed_size = offset;
  auto decompress_result =
      LZ4F_decompress(decompression_context, decompressed_data.data(), &decompressed_size,
                      compressed_data.data(), &consumed_compressed_size, nullptr);
  ASSERT_FALSE(LZ4F_isError(decompress_result));

  EXPECT_EQ(decompressed_size, kUncompressedSize);
  EXPECT_EQ(consumed_compressed_size, offset);
  EXPECT_TRUE(
      memcmp(decompressed_data.data(), uncompressed_data.data(), uncompressed_data.size()) == 0);
}

}  // namespace
}  // namespace storage::volume_image

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/utils/lz4_decompressor.h"

#include <array>
#include <cstdint>
#include <vector>

#include <fbl/auto_call.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <lz4/lz4.h>

#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/utils/lz4_result.h"

namespace storage::volume_image {
namespace {

constexpr std::array<uint8_t, 4096> kData = {0};

fit::result<std::vector<uint8_t>, std::string> GetCompressedData() {
  size_t max_size = LZ4F_compressFrameBound(kData.size(), nullptr);

  std::vector<uint8_t> compressed_data(max_size, 0);

  Lz4Result result = LZ4F_compressFrame(compressed_data.data(), compressed_data.size(),
                                        kData.data(), kData.size(), nullptr);
  if (result.is_error()) {
    return fit::error("Failed to compress |kData|. LZ4 Error: " + std::string(result.error()));
  }
  compressed_data.resize(result.byte_count());

  return fit::ok(std::move(compressed_data));
}

TEST(Lz4DecompressorTest, CreateWithWrongSchemaIsError) {
  CompressionOptions options;
  options.schema = CompressionSchema::kNone;

  ASSERT_TRUE(Lz4Decompressor::Create(options).is_error());
}

TEST(Lz4DecompressorTest, CreateWithCompressionSchemaLz4IsOk) {
  CompressionOptions options;
  options.schema = CompressionSchema::kLz4;

  ASSERT_TRUE(Lz4Decompressor::Create(options).is_ok());
}

TEST(Lz4DecompressorTest, PrepareAfterConstructionIsOk) {
  CompressionOptions options = {.schema = CompressionSchema::kLz4};
  auto decompressor_or = Lz4Decompressor::Create(options);

  ASSERT_TRUE(decompressor_or.is_ok()) << decompressor_or.error();
  auto decompressor = decompressor_or.take_value();

  EXPECT_TRUE(
      decompressor.Prepare([](fbl::Span<const uint8_t> buffer) { return fit::ok(); }).is_ok());
}

TEST(Lz4DecompressorTest, DecompressWithoutPrepareIsError) {
  CompressionOptions options = {.schema = CompressionSchema::kLz4};
  auto decompressor_or = Lz4Decompressor::Create(options);

  ASSERT_TRUE(decompressor_or.is_ok()) << decompressor_or.error();
  auto decompressor = decompressor_or.take_value();

  EXPECT_TRUE(decompressor.Decompress(fbl::Span<const uint8_t>()).is_error());
}

TEST(Lz4DecompressorTest, FinalizeWithoutPrepareIsError) {
  CompressionOptions options = {.schema = CompressionSchema::kLz4};
  auto decompressor_or = Lz4Decompressor::Create(options);

  ASSERT_TRUE(decompressor_or.is_ok()) << decompressor_or.error();
  auto decompressor = decompressor_or.take_value();

  EXPECT_TRUE(decompressor.Finalize().is_error());
}

TEST(Lz4DecompressorTest, DecompressWithPrepareAndSizeHintIsOk) {
  CompressionOptions options = {.schema = CompressionSchema::kLz4};
  auto decompressor_or = Lz4Decompressor::Create(options);

  ASSERT_TRUE(decompressor_or.is_ok()) << decompressor_or.error();

  auto data_or = GetCompressedData();
  ASSERT_TRUE(data_or.is_ok()) << data_or.error();

  auto decompressor = decompressor_or.take_value();
  ASSERT_TRUE(decompressor
                  .Prepare([](fbl::Span<const uint8_t> buffer) {
                    EXPECT_THAT(buffer, testing::ElementsAreArray(kData));
                    return fit::ok();
                  })
                  .is_ok());

  // This should allow us to decompress in one pass.
  decompressor.ProvideSizeHint(kData.size());
  auto decompressor_result = decompressor.Decompress(data_or.value());
  ASSERT_TRUE(decompressor_result.is_ok()) << decompressor_result.error();

  auto [hint, consumed_bytes] = decompressor_result.take_value();
  EXPECT_EQ(hint, static_cast<size_t>(0));
  EXPECT_EQ(consumed_bytes, data_or.value().size());
}

TEST(Lz4DecompressorTest, DecompressOnMultipleStepsIsOk) {
  CompressionOptions options = {.schema = CompressionSchema::kLz4};
  auto decompressor_or = Lz4Decompressor::Create(options);

  ASSERT_TRUE(decompressor_or.is_ok()) << decompressor_or.error();

  auto data_or = GetCompressedData();
  ASSERT_TRUE(data_or.is_ok()) << data_or.error();

  auto decompressor = decompressor_or.take_value();
  size_t total_consumed_bytes = 0;
  size_t decompressed_data_offset = 0;
  ASSERT_TRUE(decompressor
                  .Prepare([&](fbl::Span<const uint8_t> buffer) {
                    EXPECT_THAT(buffer,
                                testing::ElementsAreArray(fbl::Span<const uint8_t>(kData).subspan(
                                    decompressed_data_offset, buffer.size())));
                    decompressed_data_offset += buffer.size();
                    return fit::ok();
                  })
                  .is_ok());

  decompressor.ProvideSizeHint(kData.size() / 4);

  bool is_decompression_done = false;
  while (!is_decompression_done) {
    auto decompression_result =
        decompressor.Decompress(fbl::Span<uint8_t>(data_or.value()).subspan(total_consumed_bytes));
    ASSERT_TRUE(decompression_result.is_ok()) << decompression_result.is_error();

    auto [hint, consumed_bytes] = decompression_result.take_value();
    total_consumed_bytes += consumed_bytes;
    is_decompression_done = hint == 0;
  }

  EXPECT_EQ(total_consumed_bytes, data_or.value().size());
}

TEST(Lz4DecompressorTest, FinalizeWithPrepareIsOk) {
  CompressionOptions options = {.schema = CompressionSchema::kLz4};
  auto decompressor_or = Lz4Decompressor::Create(options);

  ASSERT_TRUE(decompressor_or.is_ok()) << decompressor_or.error();
  auto decompressor = decompressor_or.take_value();
  ASSERT_TRUE(
      decompressor.Prepare([](fbl::Span<const uint8_t> buffer) { return fit::ok(); }).is_ok());

  EXPECT_TRUE(decompressor.Finalize().is_ok());
}

TEST(Lz4DecompressorTest, PrepareAfterFinalizeIsOk) {
  CompressionOptions options = {.schema = CompressionSchema::kLz4};
  auto decompressor_or = Lz4Decompressor::Create(options);

  ASSERT_TRUE(decompressor_or.is_ok()) << decompressor_or.error();
  auto decompressor = decompressor_or.take_value();
  ASSERT_TRUE(
      decompressor.Prepare([](fbl::Span<const uint8_t> buffer) { return fit::ok(); }).is_ok());

  EXPECT_TRUE(decompressor.Finalize().is_ok());

  ASSERT_TRUE(
      decompressor.Prepare([](fbl::Span<const uint8_t> buffer) { return fit::ok(); }).is_ok());
}

}  // namespace
}  // namespace storage::volume_image

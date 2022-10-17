// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <algorithm>
#include <memory>
#include <optional>

#include <gtest/gtest.h>

#include "src/storage/blobfs/compression/blob_compressor.h"
#include "src/storage/blobfs/compression/chunked.h"
#include "src/storage/blobfs/compression/compressor.h"
#include "src/storage/blobfs/compression/seekable_decompressor.h"
#include "src/storage/blobfs/format.h"

namespace blobfs {
namespace {

enum class DataType {
  Compressible,
  Random,
};

std::unique_ptr<char[]> GenerateInput(DataType data_type, unsigned* seed, size_t size) {
  std::unique_ptr<char[]> input(new char[size]);
  switch (data_type) {
    case DataType::Compressible: {
      size_t i = 0;
      while (i < size) {
        size_t run_length = 1 + (rand_r(seed) % (size - i));
        char value = static_cast<char>(rand_r(seed) % std::numeric_limits<char>::max());
        memset(input.get() + i, value, run_length);
        i += run_length;
      }
      break;
    }
    case DataType::Random:
      for (size_t i = 0; i < size; i++) {
        input[i] = static_cast<char>(rand_r(seed));
      }
      break;
    default:
      EXPECT_TRUE(false) << "Bad Data Type";
  }
  return input;
}

void CompressionHelper(CompressionAlgorithm algorithm, const char* input, size_t size, size_t step,
                       std::optional<BlobCompressor>* out) {
  CompressionSettings settings = {
      .compression_algorithm = algorithm,
  };
  auto compressor = BlobCompressor::Create(settings, size);
  ASSERT_TRUE(compressor);

  size_t offset = 0;
  while (offset != size) {
    const void* data = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(input) + offset);
    const size_t incremental_size = std::min(step, size - offset);
    ASSERT_EQ(compressor->Update(data, incremental_size), ZX_OK);
    offset += incremental_size;
  }
  ASSERT_EQ(compressor->End(), ZX_OK);
  EXPECT_GT(compressor->Size(), 0ul);

  *out = std::move(compressor);
}

void DecompressAndVerifyMapping(SeekableDecompressor* decompressor, const uint8_t* compressed_buf,
                                size_t compressed_size, const uint8_t* expected,
                                size_t expected_size, const CompressionMapping& mapping) {
  ASSERT_LE(mapping.decompressed_offset + mapping.decompressed_length, expected_size);
  ASSERT_LE(mapping.compressed_offset + mapping.compressed_length, compressed_size);
  fbl::Array<uint8_t> buf(new uint8_t[mapping.decompressed_length], mapping.decompressed_length);
  size_t sz = mapping.decompressed_length;
  ASSERT_EQ(
      decompressor->DecompressRange(buf.get(), &sz, compressed_buf + mapping.compressed_offset,
                                    mapping.compressed_length, mapping.decompressed_offset),
      ZX_OK);
  EXPECT_EQ(mapping.decompressed_length, sz);
  EXPECT_EQ(memcmp(expected + mapping.decompressed_offset, buf.get(), sz), 0);
}

void DecompressionHelper(SeekableDecompressor* decompressor, unsigned* seed,
                         const void* compressed_buf, size_t compressed_size, const void* expected,
                         size_t expected_size) {
  // 1. Sequential decompression of each range
  size_t offset = 0;
  zx::result<CompressionMapping> mapping = zx::ok(CompressionMapping{});
  while ((mapping = decompressor->MappingForDecompressedRange(offset, 1,
                                                              std::numeric_limits<size_t>::max()))
             .is_ok()) {
    DecompressAndVerifyMapping(decompressor, static_cast<const uint8_t*>(compressed_buf),
                               compressed_size, static_cast<const uint8_t*>(expected),
                               expected_size, mapping.value());
    offset += mapping->decompressed_length;
  }
  // 2. Random offsets
  for (int i = 0; i < 100; ++i) {
    offset = rand_r(seed) % expected_size;
    mapping =
        decompressor->MappingForDecompressedRange(offset, 1, std::numeric_limits<size_t>::max());
    ASSERT_TRUE(mapping.is_ok());
    DecompressAndVerifyMapping(decompressor, static_cast<const uint8_t*>(compressed_buf),
                               compressed_size, static_cast<const uint8_t*>(expected),
                               expected_size, mapping.value());
  }
  // 3. Full range
  mapping = decompressor->MappingForDecompressedRange(0, expected_size,
                                                      std::numeric_limits<size_t>::max());
  ASSERT_TRUE(mapping.is_ok());
  DecompressAndVerifyMapping(decompressor, static_cast<const uint8_t*>(compressed_buf),
                             compressed_size, static_cast<const uint8_t*>(expected), expected_size,
                             mapping.value());
}

// Tests various input combinations for MappingForDecompressedRange(), focusing on the trimming
// logic dictated by |max_decompressed_len|.
void TestDecompressedRangeTrimming(SeekableDecompressor* decompressor, size_t chunk_size,
                                   size_t total_size) {
  // max_decompressed_len = 0
  zx::result<CompressionMapping> mapping = decompressor->MappingForDecompressedRange(0, 1, 0);
  ASSERT_TRUE(mapping.is_error());

  // max_decomressed_len less than a single chunk
  if (chunk_size > 1) {
    mapping = decompressor->MappingForDecompressedRange(0, 1, chunk_size - 1);
    if (chunk_size <= total_size) {
      ASSERT_TRUE(mapping.is_error());
    } else {
      ASSERT_TRUE(mapping.is_ok());
      EXPECT_EQ(mapping.value().decompressed_offset, 0ul);
      EXPECT_EQ(mapping.value().decompressed_length, total_size);
    }
  }

  // Trivial success case.
  mapping = decompressor->MappingForDecompressedRange(0, 1, std::numeric_limits<size_t>::max());
  ASSERT_TRUE(mapping.is_ok());
  const size_t expected_decompressed_len = std::min(chunk_size, total_size);
  EXPECT_EQ(mapping.value().decompressed_length, expected_decompressed_len);
  EXPECT_EQ(mapping.value().decompressed_offset, 0ul);

  // max_decompressed_len larger than a single chunk.
  mapping = decompressor->MappingForDecompressedRange(0, 1, chunk_size + 1);
  ASSERT_TRUE(mapping.is_ok());
  EXPECT_LE(mapping.value().decompressed_length, chunk_size + 1);
  EXPECT_EQ(mapping.value().decompressed_offset, 0ul);

  // max_decompressed_len just large enough for a single chunk.
  mapping = decompressor->MappingForDecompressedRange(0, 1, chunk_size);
  ASSERT_TRUE(mapping.is_ok());
  EXPECT_EQ(mapping.value().decompressed_length, expected_decompressed_len);
  EXPECT_EQ(mapping.value().decompressed_offset, 0ul);

  // max_decompressed_len just large enough for a single chunk. Requested length > 1.
  mapping = decompressor->MappingForDecompressedRange(0, expected_decompressed_len, chunk_size);
  ASSERT_TRUE(mapping.is_ok());
  EXPECT_EQ(mapping.value().decompressed_length, expected_decompressed_len);
  EXPECT_EQ(mapping.value().decompressed_offset, 0ul);
}

// Tests a contained case of compression and decompression.
//
// size: The size of the input buffer.
// step: The step size of updating the compression buffer.
void RunCompressDecompressTest(CompressionAlgorithm algorithm, DataType data_type, size_t size,
                               size_t step) {
  ASSERT_LE(step, size) << "Step size too large";

  unsigned seed = ::testing::UnitTest::GetInstance()->random_seed();

  // Generate input.
  std::unique_ptr<char[]> input(GenerateInput(data_type, &seed, size));

  // Compress a buffer.
  std::optional<BlobCompressor> compressor;
  CompressionHelper(algorithm, input.get(), size, step, &compressor);
  ASSERT_TRUE(compressor);

  // Decompress the buffer.
  std::unique_ptr<SeekableDecompressor> decompressor;
  switch (algorithm) {
    case CompressionAlgorithm::kChunked: {
      ASSERT_EQ(
          SeekableChunkedDecompressor::CreateDecompressor(
              cpp20::span(static_cast<const uint8_t*>(compressor->Data()), compressor->Size()),
              compressor->Size(), &decompressor),
          ZX_OK);
      break;
    }
    default:
      ASSERT_TRUE(false);
  }
  DecompressionHelper(decompressor.get(), &seed, compressor->Data(), compressor->Size(),
                      input.get(), size);

  TestDecompressedRangeTrimming(decompressor.get(), compressor->compressor().GetChunkSize(), size);
}

TEST(SeekableCompressorTest, CompressDecompressChunkCompressible1) {
  RunCompressDecompressTest(CompressionAlgorithm::kChunked, DataType::Random, 1 << 0, 1 << 0);
}

TEST(SeekableCompressorTest, CompressDecompressChunkCompressible2) {
  RunCompressDecompressTest(CompressionAlgorithm::kChunked, DataType::Random, 1 << 1, 1 << 0);
}

TEST(SeekableCompressorTest, CompressDecompressChunkCompressible3) {
  RunCompressDecompressTest(CompressionAlgorithm::kChunked, DataType::Random, 1 << 10, 1 << 5);
}

TEST(SeekableCompressorTest, CompressDecompressChunkCompressible4) {
  RunCompressDecompressTest(CompressionAlgorithm::kChunked, DataType::Random, 1 << 15, 1 << 10);
}

TEST(SeekableCompressorTest, CompressDecompressChunkRandom1) {
  RunCompressDecompressTest(CompressionAlgorithm::kChunked, DataType::Random, 1 << 0, 1 << 0);
}

TEST(SeekableCompressorTest, CompressDecompressChunkRandom2) {
  RunCompressDecompressTest(CompressionAlgorithm::kChunked, DataType::Random, 1 << 1, 1 << 0);
}

TEST(SeekableCompressorTest, CompressDecompressChunkRandom3) {
  RunCompressDecompressTest(CompressionAlgorithm::kChunked, DataType::Random, 1 << 10, 1 << 5);
}

TEST(SeekableCompressorTest, CompressDecompressChunkRandom4) {
  RunCompressDecompressTest(CompressionAlgorithm::kChunked, DataType::Random, 1 << 15, 1 << 10);
}

}  // namespace
}  // namespace blobfs
